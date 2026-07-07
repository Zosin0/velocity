#pragma once
// Edit operations: pure functions (snapshot in → snapshot out) that enforce
// the track invariants. The command/transaction layer (docs/02 §5) wraps
// these in Phase 3; Phase 2 uses them directly.

#include <velocity/engine/model.h>
#include <velocity/foundation/expected.h>

#include <functional>
#include <string>

namespace velocity::engine {

using EditResult = Expected<SnapshotPtr, std::string>;

// Creates an empty sequence snapshot with the given format and track layout.
SnapshotPtr makeSequence(int width, int height, Rational fps, int videoTracks,
                         int audioTracks);

// Inserts a clip; fails if it would overlap an existing clip on that track
// or if the track index/kind is invalid.
EditResult addClip(const SnapshotPtr& seq, size_t trackIdx, Clip clip);

// Splits the clip containing `at` into two clips at that tick. The source
// in-point of the right half advances by the elapsed timeline time converted
// into the clip's source timebase. No-op error if `at` is not strictly inside
// a clip.
EditResult splitClip(const SnapshotPtr& seq, size_t trackIdx, ClipId id, Tick at);

// Removes a clip; later clips do NOT move (lift). Ripple delete is Phase 3.
EditResult removeClip(const SnapshotPtr& seq, size_t trackIdx, ClipId id);

// Moves a clip to a new dstStart on the same track; fails on overlap.
EditResult moveClip(const SnapshotPtr& seq, size_t trackIdx, ClipId id, Tick newStart);

// Trims a clip edge. Trimming the head also advances the source in-point so
// the content under the playhead stays fixed (standard NLE trim semantics).
EditResult trimClipHead(const SnapshotPtr& seq, size_t trackIdx, ClipId id, Tick newStart);
EditResult trimClipTail(const SnapshotPtr& seq, size_t trackIdx, ClipId id, Tick newEnd);

// Generic property edit: copies the clip, lets `mutate` adjust it, then
// revalidates track invariants. Placement fields may be changed too; overlap
// rules still apply. Used by the inspector for gain/fades/transform edits.
EditResult updateClip(const SnapshotPtr& seq, size_t trackIdx, ClipId id,
                      const std::function<void(Clip&)>& mutate);

// Moves a clip to a different track (same kind only); fails on overlap.
EditResult moveClipToTrack(const SnapshotPtr& seq, size_t fromTrack, ClipId id, size_t toTrack,
                           Tick newStart);

// Bounded linear undo/redo over snapshots. Structural sharing keeps retained
// snapshots cheap; the memory-budget valve (docs/02 §5) is future work.
class UndoStack {
public:
    explicit UndoStack(SnapshotPtr initial) { states_.push_back(std::move(initial)); }

    [[nodiscard]] const SnapshotPtr& current() const { return states_[index_]; }
    [[nodiscard]] bool canUndo() const { return index_ > 0; }
    [[nodiscard]] bool canRedo() const { return index_ + 1 < states_.size(); }

    void push(SnapshotPtr next) {
        states_.resize(index_ + 1); // drop redo branch
        states_.push_back(std::move(next));
        ++index_;
    }
    const SnapshotPtr& undo() {
        if (canUndo())
            --index_;
        return current();
    }
    const SnapshotPtr& redo() {
        if (canRedo())
            ++index_;
        return current();
    }

private:
    std::vector<SnapshotPtr> states_;
    size_t index_ = 0;
};

} // namespace velocity::engine
