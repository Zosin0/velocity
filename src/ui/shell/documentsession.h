#pragma once

#include <velocity/engine/model.h>
#include <velocity/engine/edits.h>

#include <QObject>
#include <filesystem>
#include <memory>
#include <optional>
#include <string>

namespace velocity::ui {

class DocumentSession : public QObject {
    Q_OBJECT

public:
    explicit DocumentSession(QObject* parent = nullptr);
    ~DocumentSession() override = default;

    // Snapshot Access
    [[nodiscard]] engine::SnapshotPtr currentSnapshot() const;
    [[nodiscard]] bool canUndo() const;
    [[nodiscard]] bool canRedo() const;

    // Playhead & Selection
    [[nodiscard]] velocity::Tick playhead() const { return playhead_; }
    [[nodiscard]] std::optional<engine::ClipId> selectedClipId() const { return selectedClipId_; }
    [[nodiscard]] std::optional<size_t> selectedTrackIdx() const { return selectedTrackIdx_; }

    // Mutating API (adds to UndoStack and emits snapshotChanged)
    void importMedia(const std::filesystem::path& path, size_t trackIdx = 0);
    void splitClipAtPlayhead();
    void deleteSelectedClip();
    void moveSelectedClip(velocity::Tick newStart);
    void trimSelectedClipHead(velocity::Tick newStart);
    void trimSelectedClipTail(velocity::Tick newEnd);

    // Playhead / Selection controls
    void setPlayhead(velocity::Tick tick);
    void selectClip(std::optional<engine::ClipId> clipId, std::optional<size_t> trackIdx);

    // Undo / Redo
    void undo();
    void redo();

signals:
    void snapshotChanged(const velocity::engine::SnapshotPtr& snapshot);
    void playheadChanged(velocity::Tick tick);
    void selectionChanged(std::optional<velocity::engine::ClipId> clipId);
    void errorOccurred(const QString& errorMessage);

private:
    void updateSnapshot(engine::EditResult&& result);

    std::unique_ptr<engine::UndoStack> undoStack_;
    velocity::Tick playhead_ = 0;
    std::optional<engine::ClipId> selectedClipId_;
    std::optional<size_t> selectedTrackIdx_;
};

} // namespace velocity::ui
