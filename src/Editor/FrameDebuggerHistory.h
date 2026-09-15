#pragma once

#include "FrameDebuggerData.h"
#include "../Renderer/RenderTexture.h"

#include <array>
#include <optional>

// task_manager/frame-debugger-3 campaign, PHASE3
// (PHASE3_FRAME_HISTORY_RING_BUFFER_AND_CAPTURE_TRIGGER.md) - a real,
// in-memory, multi-slot history of past captured frames. GTE_ENABLE_EDITOR-
// only (this whole file only ever compiles under GTE_ENABLE_EDITOR, exactly
// like FrameDebuggerCapture.h/FrameDebuggerData.h themselves) - owned by
// FrameDebuggerPanel (Panels/FrameDebuggerPanel.h), never a shared/global
// object.
namespace gte {

class Renderer;

// One ring-buffer slot: a fully real, already-resolved FrameDebuggerSnapshot
// (PHASE2's BuildRealFrameDebuggerSnapshot()) plus a retained GPU copy of
// that historical frame's real Game View output image. `preview` is
// std::nullopt only for a slot index FrameDebuggerHistory::CaptureFrame()
// has never written into at all (see that method's own "lazy allocation"
// comment) - once written, it is ALWAYS populated (a fresh RenderTexture is
// created for every single real capture, never left std::nullopt again).
struct FrameDebuggerHistoryEntry {
    FrameDebuggerSnapshot snapshot;
    std::optional<RenderTexture> preview;
};

// Pure, plain-int bookkeeping for a fixed-capacity circular ring buffer -
// Tier-1-testable with no live Renderer/RenderTexture at all (see
// tests/Editor/FrameDebuggerHistoryTests.cpp). `count` is how many slots are
// currently populated (0..capacity); `nextWriteIndex` is which physical slot
// the NEXT real capture writes into.
struct FrameDebuggerHistoryWriteState {
    int count = 0;
    int nextWriteIndex = 0;
};

// Advances `state` by exactly one more real capture, into a ring buffer of
// `capacity` slots: `nextWriteIndex` always advances circularly (mod
// capacity), and `count` grows by one until it reaches `capacity`, then
// stays pinned there forever - every further call is exactly the real
// eviction behavior FrameDebuggerHistory::CaptureFrame() itself relies on
// (the OLDEST populated entry's own storage slot is always exactly
// whichever one `nextWriteIndex` pointed at right before this call).
FrameDebuggerHistoryWriteState AdvanceFrameDebuggerHistoryWriteState(
    FrameDebuggerHistoryWriteState state, int capacity) noexcept;

// Pure clamp: keeps a 0-based "which captured frame is currently being
// viewed" cursor inside [0, count - 1] - never wraps at either end (a no-op
// at either boundary), and always 0 for an empty history (count <= 0),
// matching FrameDebuggerHistory::CurrentEntry()'s own "nullptr if Count() ==
// 0" contract (a clamped-to-0 cursor on an empty history is simply never
// read as a real storage index by CurrentEntry() either way).
int ClampFrameDebuggerHistoryCursor(int cursor, int count) noexcept;

// A real, in-memory, multi-slot ring buffer of past captured frames - see
// this file's own top-of-file comment. Owned by FrameDebuggerPanel.
class FrameDebuggerHistory {
public:
    // Tunable - 8 chosen as "a handful of recent frames is plenty for a
    // debugging aid, without unbounded GPU memory growth" (each populated
    // slot owns one full-resolution retained RenderTexture).
    static constexpr int kCapacity = 8;

    // Called once per real capture trigger (Enable edge / Step / an
    // explicit "Capture" button - see FrameDebuggerPanel::TriggerCapture()).
    // Copies `snapshot` in verbatim, and makes a fresh, retained GPU-to-GPU
    // copy of `gameViewSource`'s CURRENT contents into this slot's own
    // dedicated RenderTexture - see FrameDebuggerHistory.cpp's own comment
    // for why this texture is always freshly (re)created rather than
    // Resize()d in place. Evicts the OLDEST entry once kCapacity is
    // exceeded (a plain circular index, never a full container shift - see
    // AdvanceFrameDebuggerHistoryWriteState() above) and moves the viewing
    // cursor onto this brand-new, just-captured frame (the natural "I just
    // captured something, show me that" behavior).
    void CaptureFrame(Renderer& renderer, const FrameDebuggerSnapshot& snapshot, RenderTexture& gameViewSource);

    // How many real captures exist right now (0..kCapacity).
    int Count() const noexcept { return m_writeState.count; }

    // Which one is currently being VIEWED - a 0-based index into the real,
    // currently-populated range only (0 == oldest retained frame,
    // Count() - 1 == newest/most-recently-captured frame).
    int CursorIndex() const noexcept { return m_cursor; }

    // Prev (negative delta, e.g. -1) / Next (positive delta, e.g. +1) -
    // PHASE4's new Frame-History mini-toolbar buttons call this directly.
    // Clamps to [0, Count() - 1] - a no-op at either end, never wraps.
    void StepCursor(int delta) noexcept;

    // nullptr if Count() == 0 (a fresh, never-captured-into history).
    const FrameDebuggerHistoryEntry* CurrentEntry() const noexcept;

private:
    int StorageIndexForLogicalIndex(int logicalIndex) const noexcept;

    std::array<FrameDebuggerHistoryEntry, kCapacity> m_entries;
    FrameDebuggerHistoryWriteState m_writeState;
    int m_cursor = 0;
};

} // namespace gte
