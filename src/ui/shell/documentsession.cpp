#include "documentsession.h"

#include <velocity/media/probe.h>
#include <velocity/foundation/log.h>
#include <spdlog/spdlog.h>

namespace velocity::ui {

DocumentSession::DocumentSession(QObject* parent)
    : QObject(parent)
{
    // Create an initial empty sequence with format 1920x1080, 30fps, 3 video tracks, 2 audio tracks
    auto initial = engine::makeSequence(1920, 1080, Rational{30, 1}, 3, 2);
    undoStack_ = std::make_unique<engine::UndoStack>(std::move(initial));
}

engine::SnapshotPtr DocumentSession::currentSnapshot() const {
    return undoStack_->current();
}

bool DocumentSession::canUndo() const {
    return undoStack_->canUndo();
}

bool DocumentSession::canRedo() const {
    return undoStack_->canRedo();
}

void DocumentSession::updateSnapshot(engine::EditResult&& result) {
    if (result) {
        undoStack_->push(result.value());
        emit snapshotChanged(undoStack_->current());
    } else {
        emit errorOccurred(QString::fromStdString(result.error()));
    }
}

void DocumentSession::importMedia(const std::filesystem::path& path, size_t trackIdx) {
    // 1. Probe the media to get duration and basic properties
    auto probeRes = media::probe(path);
    if (!probeRes) {
        emit errorOccurred(QString::fromStdString("Failed to probe media: " + probeRes.error().message));
        return;
    }
    
    const auto& info = probeRes.value();
    Tick duration = ticksFromSeconds(info.durationSeconds);
    if (duration <= 0) {
        duration = ticksFromSeconds(5.0); // default to 5 seconds if duration unknown
    }

    // Determine target track index
    size_t targetTrack = trackIdx;
    if (targetTrack >= currentSnapshot()->tracks.size()) {
        targetTrack = 0;
    }

    // 2. Create the Clip struct
    engine::Clip newClip;
    newClip.id = engine::nextClipId();
    newClip.asset = path;
    
    // Find next empty spot on target track
    Tick startPos = 0;
    const auto& track = currentSnapshot()->tracks[targetTrack];
    if (!track->clips.empty()) {
        startPos = track->clips.back()->dstEnd() + ticksFromSeconds(0.5); // 0.5s gap
    }
    newClip.dstStart = startPos;
    newClip.dstLen = duration;
    
    if (info.bestVideo) {
        newClip.srcTimebase = info.bestVideo->timebase;
        newClip.srcStartPts = 0;
    } else if (info.bestAudio) {
        newClip.srcTimebase = info.bestAudio->timebase;
        newClip.srcStartPts = 0;
    } else {
        newClip.srcTimebase = Rational{1, kTickRate};
        newClip.srcStartPts = 0;
    }

    // 3. Add to track
    auto res = engine::addClip(currentSnapshot(), targetTrack, std::move(newClip));
    updateSnapshot(std::move(res));
}

void DocumentSession::splitClipAtPlayhead() {
    auto seq = currentSnapshot();
    // Find any clip on the current selected track or any track that intersects the playhead
    bool splitDone = false;
    for (size_t trackIdx = 0; trackIdx < seq->tracks.size(); ++trackIdx) {
        // If a track is selected, prioritize it, otherwise search all
        if (selectedTrackIdx_ && *selectedTrackIdx_ != trackIdx) {
            continue;
        }
        
        const auto& track = seq->tracks[trackIdx];
        for (const auto& clip : track->clips) {
            if (clip->contains(playhead_)) {
                auto res = engine::splitClip(seq, trackIdx, clip->id, playhead_);
                if (res) {
                    updateSnapshot(std::move(res));
                    splitDone = true;
                    break;
                }
            }
        }
        if (splitDone) break;
    }
    
    if (!splitDone) {
        // If prioritized track split didn't find anything but we did prioritize, search other tracks
        if (selectedTrackIdx_) {
            for (size_t trackIdx = 0; trackIdx < seq->tracks.size(); ++trackIdx) {
                if (*selectedTrackIdx_ == trackIdx) continue;
                const auto& track = seq->tracks[trackIdx];
                for (const auto& clip : track->clips) {
                    if (clip->contains(playhead_)) {
                        auto res = engine::splitClip(seq, trackIdx, clip->id, playhead_);
                        if (res) {
                            updateSnapshot(std::move(res));
                            splitDone = true;
                            break;
                        }
                    }
                }
                if (splitDone) break;
            }
        }
    }
    
    if (!splitDone) {
        emit errorOccurred("No clip found at playhead to split");
    }
}

void DocumentSession::deleteSelectedClip() {
    if (!selectedClipId_ || !selectedTrackIdx_) {
        emit errorOccurred("No clip selected to delete");
        return;
    }
    auto res = engine::removeClip(currentSnapshot(), *selectedTrackIdx_, *selectedClipId_);
    if (res) {
        selectedClipId_ = std::nullopt;
        selectedTrackIdx_ = std::nullopt;
        emit selectionChanged(std::nullopt);
        updateSnapshot(std::move(res));
    } else {
        emit errorOccurred(QString::fromStdString(res.error()));
    }
}

void DocumentSession::moveSelectedClip(velocity::Tick newStart) {
    if (!selectedClipId_ || !selectedTrackIdx_) return;
    auto res = engine::moveClip(currentSnapshot(), *selectedTrackIdx_, *selectedClipId_, newStart);
    updateSnapshot(std::move(res));
}

void DocumentSession::trimSelectedClipHead(velocity::Tick newStart) {
    if (!selectedClipId_ || !selectedTrackIdx_) return;
    auto res = engine::trimClipHead(currentSnapshot(), *selectedTrackIdx_, *selectedClipId_, newStart);
    updateSnapshot(std::move(res));
}

void DocumentSession::trimSelectedClipTail(velocity::Tick newEnd) {
    if (!selectedClipId_ || !selectedTrackIdx_) return;
    auto res = engine::trimClipTail(currentSnapshot(), *selectedTrackIdx_, *selectedClipId_, newEnd);
    updateSnapshot(std::move(res));
}

void DocumentSession::setPlayhead(velocity::Tick tick) {
    if (tick < 0) tick = 0;
    // Bounded by sequence duration
    Tick maxTick = currentSnapshot()->duration();
    if (maxTick < ticksFromSeconds(10.0)) {
        maxTick = ticksFromSeconds(600.0); // 10 minutes default range
    }
    if (tick > maxTick) tick = maxTick;
    
    if (playhead_ != tick) {
        playhead_ = tick;
        emit playheadChanged(playhead_);
    }
}

void DocumentSession::selectClip(std::optional<engine::ClipId> clipId, std::optional<size_t> trackIdx) {
    if (selectedClipId_ != clipId || selectedTrackIdx_ != trackIdx) {
        selectedClipId_ = clipId;
        selectedTrackIdx_ = trackIdx;
        emit selectionChanged(selectedClipId_);
    }
}

void DocumentSession::undo() {
    if (undoStack_->canUndo()) {
        undoStack_->undo();
        emit snapshotChanged(undoStack_->current());
        // clear selection in case clip was deleted
        selectedClipId_ = std::nullopt;
        selectedTrackIdx_ = std::nullopt;
        emit selectionChanged(std::nullopt);
    }
}

void DocumentSession::redo() {
    if (undoStack_->canRedo()) {
        undoStack_->redo();
        emit snapshotChanged(undoStack_->current());
        // clear selection
        selectedClipId_ = std::nullopt;
        selectedTrackIdx_ = std::nullopt;
        emit selectionChanged(std::nullopt);
    }
}

} // namespace velocity::ui
