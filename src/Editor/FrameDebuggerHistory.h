#pragma once

#include "FrameDebuggerData.h"
#include "../Renderer/RenderTexture.h"

#include <optional>
#include <string>
#include <vector>

// task_manager/frame-debugger-7 campaign, PHASE1
// (PHASE1_REMOVE_HISTORY_AND_SINGLE_CAPTURE_LIFECYCLE.md) - NOT a
// multi-frame history (frame-debugger-7 campaign removed the old 8-slot
// ring buffer) - exactly ONE captured frame is ever retained, matching
// Unity's own Frame Debugger, which does not save frame history either.
// GTE_ENABLE_EDITOR-only (this whole file only ever compiles under
// GTE_ENABLE_EDITOR, exactly like FrameDebuggerCapture.h/FrameDebuggerData.h
// themselves) - owned by FrameDebuggerPanel (Panels/FrameDebuggerPanel.h),
// never a shared/global object.
//
// task_manager/frame-debugger-7 campaign, PHASE4
// (PHASE4_PREVIEW_WIRING_AND_DATA_MODEL.md, Step 3.3) - the frame-debugger-5
// campaign's own per-compute-pass-distinct-texture preview mechanism
// (`FrameDebuggerComputePassPreview`/`FrameDebuggerHistoryEntry::
// computePassPreviews`/`m_volumePreviewRenderer`/the `#include` of
// `VolumeTexturePreviewRenderer.h`) is REMOVED ENTIRELY from this file - an
// explicit, user-approved breaking change (PHASE0's Locked Design Decision
// #3). Every leaf, a compute pass or an object draw alike, now shows the
// accumulated Game View image as of that exact step instead - see
// `perObjectStepPreviews` below (the compute-pass leaves reuse the
// already-existing `preview`/`compositedPreview` fields via their own new
// `FrameDebuggerStepPreviewKind`, FrameDebuggerData.h). The Render Graph
// panel / `GET /get_texture` retain their OWN, separate, still-needed way
// to inspect a raw named texture (`RenderGraphDebugTextureRegistry`/
// `DebugTextureSnapshotFor()`) - untouched by this removal.
namespace gte {

class Renderer;

// frame-debugger-5 campaign, PHASE3
// (PHASE3_GENERIC_PER_PASS_RETAINED_PREVIEW_CAPTURE.md) - forward-declared
// here (mirroring Panels/FrameDebuggerPanel.h's own identical forward
// declaration) so CaptureFrame() below can take a `const rg::RenderGraph&`
// parameter without this header needing to #include the real,
// heavyweight RenderGraph.h - only FrameDebuggerHistory.cpp needs the real
// definition.
namespace rg {
class RenderGraph;
} // namespace rg

// The one, single retained slot: a fully real, already-resolved
// FrameDebuggerSnapshot (PHASE2's BuildRealFrameDebuggerSnapshot()) plus a
// retained GPU copy of the currently-captured frame's real Game View
// output image. `preview` is std::nullopt only before
// FrameDebuggerCurrentCapture::CaptureFrame() has ever been called at all -
// once written, it is ALWAYS populated (a fresh RenderTexture is created
// for every single real capture, never left std::nullopt again).
// `compositedPreview` (below) instead follows its own, different rule - see
// its own field-level comment. `perObjectStepPreviews` (below) follows a
// THIRD, different rule again - see its own field-level comment.
struct FrameDebuggerHistoryEntry {
    FrameDebuggerSnapshot snapshot;
    std::optional<RenderTexture> preview; // pre-composite "GameView" copy - unchanged behavior/doc comment.

    // (frame-debugger-4 campaign, PHASE1) - a retained GPU copy of this
    // capture's real POST-atmosphere-composite "GameViewComposited"
    // output - the TRUE final image the "Game" panel / GET /get_game_view
    // actually show (see task_manager/frame-debugger-4's own
    // PHASE0_MASTER_STRATEGY.md Step 2 root-cause investigation).
    // std::nullopt whenever CaptureFrame() below was called with
    // compositedGameViewSource == nullptr for THIS capture (e.g. a capture
    // taken before the atmosphere composite pass had ever produced anything
    // yet this session) - a real, honest "not available for this
    // particular captured frame" state, never a bug and never silently
    // substituted with something fake. Once populated, a LATER capture may
    // legitimately go back to std::nullopt again if compositedGameViewSource
    // is null on that later call - this field's std::nullopt-ness is a
    // property of the CAPTURE that most recently ran, not a one-way ratchet.
    std::optional<RenderTexture> compositedPreview;

    // task_manager/frame-debugger-7 campaign, PHASE4
    // (PHASE4_PREVIEW_WIRING_AND_DATA_MODEL.md, Step 3.2) - REPLACES the
    // old, now-deleted `computePassPreviews` field outright (an explicit,
    // user-approved breaking change - PHASE0's Locked Design Decision #3).
    // One real, retained GPU `RenderTexture` per real object drawn this
    // capture's "GameView" pass, in the SAME order as
    // `FrameDebuggerCaptureContext::DrawRecords()` (index `i` holds the
    // real, accumulated Game View image exactly as it looked after objects
    // `[0..i]` were redrawn from scratch this frame - see PHASE3 of this
    // campaign's `AddFrameDebuggerReplayPasses()`,
    // `src/Application/RenderPasses.h/.cpp`). Moved here, PERMANENTLY, from
    // `FrameDebuggerCaptureContext::ReplayStepPreviews()` (which only ever
    // lives from mid-frame-N to early-frame-N+1's own `Reset()` call)
    // inside `CaptureFrame()` below, strictly BEFORE that `Reset()` call
    // ever runs - the Step 3.0 two-bool handshake (Phase 3) already
    // guarantees `CaptureFrame()` runs later the SAME frame the replay
    // passes were declared/executed. Entirely REBUILT from scratch on every
    // single real capture (a plain `std::move()` assignment below
    // overwrites whatever this vector held before) - never assumed stable
    // across captures. Empty whenever this capture's own "GameView" pass
    // drew zero objects (an honestly empty Game View) - never a fake entry.
    // Indexed by a selected event node's own
    // `FrameDebuggerEventDetails::stepPreviewIndex` (only meaningful when
    // `stepPreviewKind == PerObjectStep` - see FrameDebuggerData.h).
    std::vector<RenderTexture> perObjectStepPreviews;
};

// A real, in-memory, SINGLE-CAPTURE slot - see this file's own top-of-file
// comment. Owned by FrameDebuggerPanel. Deliberately named
// `FrameDebuggerCurrentCapture` (not `...History`) - there is no "which one
// of several past frames" concept anymore, just "is there a currently
// captured frame, and what is it".
class FrameDebuggerCurrentCapture {
public:
    // Called once per real capture trigger (Enable edge / Step / an
    // explicit "Capture" button - see FrameDebuggerPanel::TriggerCapture()).
    // Copies `snapshot` in verbatim, and makes a fresh, retained GPU-to-GPU
    // copy of `gameViewSource`'s CURRENT contents into this capture's own
    // dedicated RenderTexture - see FrameDebuggerHistory.cpp's own comment
    // for why this texture is always freshly (re)created rather than
    // Resize()d in place. Simply overwrites whatever the previous capture
    // held (there is only ever one slot now - see Clear() for the OTHER way
    // this data goes away).
    // `compositedGameViewSource` is the CURRENT frame's real, post-atmosphere-
    // composite final output (ImGuiEditorLayer's own m_gameViewComposited), or
    // nullptr when no composited texture exists yet this session (see this
    // method's own doc comment on FrameDebuggerHistoryEntry::compositedPreview
    // above) - nullptr is a completely safe, ordinary input, never dereferenced.
    //
    // `renderGraph` (frame-debugger-5 campaign, PHASE3) - kept as a
    // parameter for call-site/signature stability, but as of
    // task_manager/frame-debugger-7 campaign, PHASE4
    // (PHASE4_PREVIEW_WIRING_AND_DATA_MODEL.md, Step 3.3), this method's own
    // body no longer reads it at all - the old compute-pass-texture-write
    // discovery (`renderGraph.LastSnapshot()`/`DebugTextureSnapshotFor()`/
    // `DebugVolumeTextureSnapshotFor()`) that used to consume it was removed
    // outright along with `computePassPreviews` (PHASE0's Locked Design
    // Decision #3). Removing the parameter itself was out of this phase's
    // scope (only ADDING the new, LAST `capture` parameter below was
    // authorized - see PHASE4_PREVIEW_WIRING_AND_DATA_MODEL.md's own
    // corrected Step 3.2) - a future phase may choose to drop it for real
    // if nothing else ever needs it again.
    //
    // `capture` (task_manager/frame-debugger-7 campaign, PHASE4, Step 3.2) -
    // NEW, LAST parameter. The SAME live `FrameDebuggerCaptureContext`
    // `FrameDebuggerPanel::TriggerCapture()` already has on hand
    // (`m_captureContext`) - its own `ReplayStepPreviews()` (Phase 3) is
    // `std::move()`'d out into `entry.perObjectStepPreviews` (above) here,
    // strictly BEFORE the next armed frame's own `Reset()` call would
    // otherwise wipe it (see `FrameDebuggerCaptureContext::
    // SetReplayStepPreviews()`'s own doc comment, FrameDebuggerCapture.h,
    // for the exact ordering requirement this satisfies).
    void CaptureFrame(Renderer& renderer, const rg::RenderGraph& renderGraph, const FrameDebuggerSnapshot& snapshot,
        RenderTexture& gameViewSource, RenderTexture* compositedGameViewSource, FrameDebuggerCaptureContext& capture);

    // Is there currently a captured frame at all? Replaces the old
    // ring-buffer's `Count()` - nothing needs "how many", only "is there
    // one".
    bool HasCapture() const noexcept { return m_current.has_value(); }

    // Releases every retained GPU resource this capture owns (RAII, via
    // std::optional::reset() - see AGENTS.md's own RAII rule) and goes back
    // to HasCapture() == false. Called on the Enable-checkbox's true->false
    // edge, and whenever playback resumes while still Enabled (see
    // Panels/FrameDebuggerPanel.cpp's own new clearing rule).
    void Clear() noexcept { m_current.reset(); }

    // nullptr if HasCapture() == false (nothing ever captured yet, or the
    // last capture was just Clear()'d).
    const FrameDebuggerHistoryEntry* CurrentEntry() const noexcept { return m_current ? &*m_current : nullptr; }

private:
    std::optional<FrameDebuggerHistoryEntry> m_current;
};

} // namespace gte
