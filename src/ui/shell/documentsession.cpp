#include "documentsession.h"

#include <velocity/media/probe.h>
#include <velocity/foundation/log.h>
#include <spdlog/spdlog.h>

#include <QDir>
#include <QFileInfo>
#include <QImage>
#include <QPainter>
#include <QStandardPaths>
#include <QSvgRenderer>

#include <algorithm>

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
        if (inGesture_ && gestureDirty_)
            undoStack_->replaceTop(result.value());
        else
            undoStack_->push(result.value());
        gestureDirty_ = inGesture_;
        emit snapshotChanged(undoStack_->current());
    } else {
        emit errorOccurred(QString::fromStdString(result.error()));
    }
}

void DocumentSession::beginGesture() {
    inGesture_ = true;
    gestureDirty_ = false;
}

void DocumentSession::endGesture() {
    inGesture_ = false;
    gestureDirty_ = false;
}

engine::ClipPtr DocumentSession::selectedClip() const {
    if (!selectedClipId_ || !selectedTrackIdx_)
        return nullptr;
    auto seq = currentSnapshot();
    if (*selectedTrackIdx_ >= seq->tracks.size())
        return nullptr;
    for (const auto& c : seq->tracks[*selectedTrackIdx_]->clips)
        if (c->id == *selectedClipId_)
            return c;
    return nullptr;
}

void DocumentSession::updateSelectedClip(const std::function<void(engine::Clip&)>& mutate) {
    if (!selectedClipId_ || !selectedTrackIdx_)
        return;
    auto res = engine::updateClip(currentSnapshot(), *selectedTrackIdx_, *selectedClipId_, mutate);
    updateSnapshot(std::move(res));
}

void DocumentSession::moveSelectedClipToTrack(size_t toTrack, velocity::Tick newStart) {
    if (!selectedClipId_ || !selectedTrackIdx_)
        return;
    auto res = engine::moveClipToTrack(currentSnapshot(), *selectedTrackIdx_, *selectedClipId_,
                                       toTrack, newStart);
    if (res)
        selectedTrackIdx_ = toTrack;
    updateSnapshot(std::move(res));
}

namespace {
// Extension-based image detection: predictable, matches the import filter.
bool isImageFile(const std::filesystem::path& path) {
    auto ext = path.extension().wstring();
    std::transform(ext.begin(), ext.end(), ext.begin(), ::towlower);
    return ext == L".png" || ext == L".jpg" || ext == L".jpeg" || ext == L".webp" ||
           ext == L".bmp" || ext == L".svg";
}
} // namespace

void DocumentSession::importMedia(const std::filesystem::path& path, size_t trackIdx,
                                  std::optional<velocity::Tick> at) {
    std::filesystem::path assetPath = path;

    // SVG: rasterize once into the local cache and import the PNG (docs/00 §5
    // — v1 rasterizes SVG at import; the engine never sees vector data).
    if (QString::fromStdWString(path.wstring()).endsWith(".svg", Qt::CaseInsensitive)) {
        QSvgRenderer svg(QString::fromStdWString(path.wstring()));
        if (!svg.isValid()) {
            emit errorOccurred("Cannot read SVG file");
            return;
        }
        QSize sz = svg.defaultSize();
        if (sz.isEmpty())
            sz = QSize(1920, 1080);
        // Scale up small vector art so it stays crisp when composited.
        while (sz.width() < 1024 && sz.height() < 1024)
            sz *= 2;
        QImage img(sz, QImage::Format_ARGB32_Premultiplied);
        img.fill(Qt::transparent);
        QPainter p(&img);
        svg.render(&p);
        p.end();

        const QString cacheDir =
            QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation) +
            "/rasterized";
        QDir().mkpath(cacheDir);
        const QString outFile =
            cacheDir + "/" +
            QFileInfo(QString::fromStdWString(path.wstring())).completeBaseName() + "_" +
            QString::number(qHash(QString::fromStdWString(path.wstring())), 16) + ".png";
        if (!img.save(outFile, "PNG")) {
            emit errorOccurred("Cannot rasterize SVG");
            return;
        }
        assetPath = std::filesystem::path(outFile.toStdWString());
    }

    // 1. Probe the media to get duration and basic properties
    auto probeRes = media::probe(assetPath);
    if (!probeRes) {
        emit errorOccurred(QString::fromStdString("Failed to probe media: " + probeRes.error().message));
        return;
    }

    const auto& info = probeRes.value();
    const bool isImage = isImageFile(assetPath);
    Tick duration = ticksFromSeconds(info.durationSeconds);
    if (isImage || duration <= 0) {
        // Images and unknown durations land as 5 s clips; images resize freely.
        duration = ticksFromSeconds(5.0);
    }

    auto seq = currentSnapshot();

    // Track helpers: first track of a kind, preferring the requested index.
    auto findTrack = [&](engine::TrackKind kind, size_t preferred) -> std::optional<size_t> {
        if (preferred < seq->tracks.size() && seq->tracks[preferred]->kind == kind)
            return preferred;
        for (size_t i = 0; i < seq->tracks.size(); ++i)
            if (seq->tracks[i]->kind == kind)
                return i;
        return std::nullopt;
    };

    // Landing position: requested tick (drop), else end of track content.
    auto nextFreeStart = [&](size_t track) -> Tick {
        if (at)
            return std::max<Tick>(*at, 0);
        const auto& clips = seq->tracks[track]->clips;
        return clips.empty() ? 0 : clips.back()->dstEnd();
    };

    const bool hasVideo = info.bestVideo.has_value();
    const bool hasAudio = info.bestAudio.has_value();

    // 2. Place video (and its linked audio) or audio-only, as ONE undo step.
    engine::EditResult result = makeUnexpected(std::string("no usable stream"));
    if (hasVideo) {
        const auto videoTrack = findTrack(engine::TrackKind::video, trackIdx);
        if (!videoTrack) {
            emit errorOccurred("No video track available");
            return;
        }
        const Tick startPos = nextFreeStart(*videoTrack);

        engine::Clip v;
        v.asset = assetPath;
        v.kind = isImage ? engine::ClipKind::image : engine::ClipKind::video;
        v.dstStart = startPos;
        v.dstLen = duration;
        v.srcTimebase = info.bestVideo->timebase;

        // Video + its audio edit as one linked unit until detached.
        const bool linkAudio = hasAudio && !isImage;
        const engine::LinkGroupId group = linkAudio ? engine::nextLinkGroup() : 0;
        v.linkGroup = group;
        result = engine::addClip(seq, *videoTrack, std::move(v));

        if (result && linkAudio) {
            if (const auto audioTrack = findTrack(engine::TrackKind::audio, trackIdx)) {
                engine::Clip a;
                a.asset = assetPath;
                a.kind = engine::ClipKind::audio;
                a.linkGroup = group;
                a.dstStart = startPos;
                a.dstLen = duration;
                a.srcTimebase = info.bestAudio->timebase;
                if (auto withAudio = engine::addClip(*result, *audioTrack, std::move(a)))
                    result = std::move(withAudio);
                // If the audio lane is occupied the video-only placement stands.
            }
        }
    } else if (hasAudio) {
        const auto audioTrack = findTrack(engine::TrackKind::audio, trackIdx);
        if (!audioTrack) {
            emit errorOccurred("No audio track available");
            return;
        }
        engine::Clip a;
        a.asset = assetPath;
        a.kind = engine::ClipKind::audio;
        a.dstStart = nextFreeStart(*audioTrack);
        a.dstLen = duration;
        a.srcTimebase = info.bestAudio->timebase;
        result = engine::addClip(seq, *audioTrack, std::move(a));
    }

    updateSnapshot(std::move(result));
}

void DocumentSession::splitClipAtPlayhead() {
    auto seq = currentSnapshot();

    // The selected clip wins when the playhead is inside it; otherwise the
    // first clip under the playhead (preferring the selected track). Linked
    // A/V companions split together so cuts never desynchronize audio.
    engine::ClipPtr target = selectedClip();
    if (target && !target->contains(playhead_))
        target = nullptr;

    if (!target) {
        auto scan = [&](size_t trackIdx) -> engine::ClipPtr {
            for (const auto& clip : seq->tracks[trackIdx]->clips)
                if (clip->contains(playhead_))
                    return clip;
            return nullptr;
        };
        if (selectedTrackIdx_ && *selectedTrackIdx_ < seq->tracks.size())
            target = scan(*selectedTrackIdx_);
        for (size_t t = 0; !target && t < seq->tracks.size(); ++t)
            target = scan(t);
    }

    if (!target) {
        emit errorOccurred("No clip found at playhead to split");
        return;
    }
    updateSnapshot(engine::splitClipLinked(seq, target->id, playhead_));
}

void DocumentSession::deleteSelectedClip() {
    if (!selectedClipId_ || !selectedTrackIdx_) {
        emit errorOccurred("No clip selected to delete");
        return;
    }
    auto res = engine::removeClipLinked(currentSnapshot(), *selectedClipId_);
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
    auto res = engine::moveClipLinked(currentSnapshot(), *selectedClipId_, newStart);
    updateSnapshot(std::move(res));
}

void DocumentSession::trimSelectedClipHead(velocity::Tick newStart) {
    if (!selectedClipId_ || !selectedTrackIdx_) return;
    auto res = engine::trimClipLinkedHead(currentSnapshot(), *selectedClipId_, newStart);
    updateSnapshot(std::move(res));
}

void DocumentSession::trimSelectedClipTail(velocity::Tick newEnd) {
    if (!selectedClipId_ || !selectedTrackIdx_) return;
    auto res = engine::trimClipLinkedTail(currentSnapshot(), *selectedClipId_, newEnd);
    updateSnapshot(std::move(res));
}

void DocumentSession::detachAudioFromSelectedClip() {
    if (!selectedClipId_) {
        emit errorOccurred("No clip selected");
        return;
    }
    updateSnapshot(engine::detachLink(currentSnapshot(), *selectedClipId_));
}

void DocumentSession::addTrack(engine::TrackKind kind) {
    // Inserting a video track shifts audio indices; keep selection valid by
    // re-resolving it from the clip id after the edit.
    const auto keepClip = selectedClipId_;
    auto res = engine::addTrack(currentSnapshot(), kind);
    updateSnapshot(std::move(res));
    if (keepClip)
        reselectClip(*keepClip);
}

void DocumentSession::removeTrack(size_t trackIdx) {
    const auto keepClip = selectedClipId_;
    auto res = engine::removeTrack(currentSnapshot(), trackIdx);
    updateSnapshot(std::move(res));
    if (keepClip)
        reselectClip(*keepClip);
}

void DocumentSession::updateTrack(size_t trackIdx,
                                  const std::function<void(engine::Track&)>& mutate) {
    updateSnapshot(engine::updateTrack(currentSnapshot(), trackIdx, mutate));
}

void DocumentSession::reselectClip(engine::ClipId id) {
    auto seq = currentSnapshot();
    for (size_t t = 0; t < seq->tracks.size(); ++t)
        for (const auto& c : seq->tracks[t]->clips)
            if (c->id == id) {
                selectClip(id, t);
                return;
            }
    selectClip(std::nullopt, std::nullopt);
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

void DocumentSession::replaceDocument(engine::SnapshotPtr snapshot) {
    undoStack_ = std::make_unique<engine::UndoStack>(std::move(snapshot));
    selectedClipId_ = std::nullopt;
    selectedTrackIdx_ = std::nullopt;
    playhead_ = 0;
    inGesture_ = false;
    gestureDirty_ = false;
    emit selectionChanged(std::nullopt);
    emit playheadChanged(0);
    emit snapshotChanged(undoStack_->current());
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
