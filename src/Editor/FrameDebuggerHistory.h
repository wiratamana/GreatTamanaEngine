#pragma once

#include "FrameDebuggerData.h"
#include "../Renderer/RenderTexture.h"
#include "../Renderer/VolumeTexturePreviewRenderer.h" // frame-debugger-5, PHASE4 - m_volumePreviewRenderer below.

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
namespace gte {

class Renderer;

// frame-debugger-5 campaign, PHASE3
// (PHASE3_GENERIC_PER_PASS_RETAINED_PREVIEW_CAPTURE.md) - forward-declared
// here (mirroring Panels/FrameDebuggerPanel.h's own identical forward
// declaration) so CaptureFrame() below can take a `const rg::RenderGraph&`
// parameter without this header needing to #include the real,
// heavyweight RenderGraph.h - only FrameDebuggerHistory.cpp needs the real
// definition (it calls renderGraph.LastSnapshot()/DebugTextureSnapshotFor()).
namespace rg {
class RenderGraph;
} // namespace rg

// frame-debugger-5 campaign, PHASE3
// (PHASE3_GENERIC_PER_PASS_RETAINED_PREVIEW_CAPTURE.md) - one real,
// retained GPU copy of ONE compute-dispatch pass's own real 2D-texture
// write, as of one specific real capture. `preview` is ALWAYS populated for
// an entry that exists in FrameDebuggerHistoryEntry::computePassPreviews at
// all (never std::optional here - unlike `preview`/`compositedPreview`
// above, a pass either gets a real entry with a real texture, or no entry
// at all; there is no partially-populated state for this struct). `preview`
// is intentionally NOT copyable (RenderTexture itself is move-only) - this
// struct is therefore also move-only, which is exactly what
// std::vector<FrameDebuggerComputePassPreview>::push_back(std::move(...))
// needs.
struct FrameDebuggerComputePassPreview {
    std::string passName;
    RenderTexture preview;
};

// The one, single retained slot: a fully real, already-resolved
// FrameDebuggerSnapshot (PHASE2's BuildRealFrameDebuggerSnapshot()) plus a
// retained GPU copy of the currently-captured frame's real Game View
// output image. `preview` is std::nullopt only before
// FrameDebuggerCurrentCapture::CaptureFrame() has ever been called at all -
// once written, it is ALWAYS populated (a fresh RenderTexture is created
// for every single real capture, never left std::nullopt again).
// `compositedPreview` (below) instead follows its own, different rule - see
// its own field-level comment. `computePassPreviews` (below) follows a
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

    // (frame-debugger-5 campaign, PHASE3
    // PHASE3_GENERIC_PER_PASS_RETAINED_PREVIEW_CAPTURE.md; WIDENED, PHASE4
    // PHASE4_VOLUME_TEXTURE_RAYMARCH_PREVIEW_REUSE.md) - one retained GPU
    // copy per real compute-dispatch pass THIS capture found with at least
    // one real 2D-texture write (see FrameDebuggerData.h's own
    // CollectComputePassTextureWrites(), and FrameDebuggerData.cpp's
    // generic "Compute Dispatches" tree groups, PHASE2) OR (PHASE4) a real
    // ray-marched thumbnail for a pass whose only visual write is a 3D
    // volume texture (see FrameDebuggerData.h's own
    // CollectComputePassVolumeTextureWrites()). Keyed by the pass's own
    // real, raw name (matches FrameDebuggerEventNode::name /
    // FrameDebuggerEventDetails::passName for that leaf exactly) so
    // EnsurePreviewDescriptor() (Panels/FrameDebuggerPanel.cpp) can look up
    // "does the CURRENTLY SELECTED leaf have its own retained preview" by a
    // simple linear name comparison - from that picking logic's point of
    // view, a volume-derived preview and a plain 2D-texture preview are
    // indistinguishable, both are just "this pass's own retained preview
    // texture" (PHASE4 needed ZERO changes to that logic). A pass with
    // neither a real 2D-texture write NOR a real volume-texture write (e.g.
    // a buffer-only write, GPU Skinning's own output buffer) simply has NO
    // entry here at all - never a fake/empty one. This field's OWN rule (a
    // third, different rule from `preview`'s "once written, always
    // populated" and `compositedPreview`'s "may legitimately go back to
    // std::nullopt") is: entirely REBUILT from scratch on every single real
    // capture (`.clear()`'d, then re-populated) - its size/contents can
    // legitimately differ from one capture to the next (e.g. a pass that
    // ran last capture but was culled this one simply has no entry this
    // time), never assumed stable across captures.
    //
    // NOTE (frame-debugger-7 campaign, PHASE1) - this field is left
    // untouched by this phase on purpose; a future phase (PHASE4 of THIS
    // campaign) is expected to remove/replace the whole per-compute-pass
    // distinct-texture preview mechanism outright (see
    // PHASE0_MASTER_STRATEGY.md's Locked Design Decision #3).
    std::vector<FrameDebuggerComputePassPreview> computePassPreviews;
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
    // `renderGraph` (frame-debugger-5 campaign, PHASE3) - the SAME live
    // RenderGraph FrameDebuggerPanel::TriggerCapture() already has on hand
    // (`*m_frameRenderGraph`). Used to (a) re-fetch THIS SAME frame's own
    // already-built rg::RenderGraphSnapshot (renderGraph.LastSnapshot()) to
    // discover every real compute-dispatch pass's own first Texture-kind
    // write (see FrameDebuggerData.h's CollectComputePassTextureWrites())
    // AND (frame-debugger-5, PHASE4) every real compute-dispatch pass's own
    // first VolumeTexture-kind write (CollectComputePassVolumeTextureWrites()),
    // and (b) resolve each discovered write-texture name into its real,
    // current physical texture + tracked GPU state
    // (renderGraph.DebugTextureSnapshotFor()/DebugVolumeTextureSnapshotFor())
    // - see this method's own .cpp body for the full capture sequence. A
    // volume-texture write additionally goes through this class's own
    // m_volumePreviewRenderer (below) to produce a real ray-marched 2D
    // thumbnail, reusing VolumeTexturePreviewRenderer::RenderPreview() -
    // the EXACT SAME code GET /get_texture already uses to preview a volume
    // texture over HTTP (PHASE4).
    void CaptureFrame(Renderer& renderer, const rg::RenderGraph& renderGraph, const FrameDebuggerSnapshot& snapshot,
        RenderTexture& gameViewSource, RenderTexture* compositedGameViewSource);

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

    // frame-debugger-5 campaign, PHASE4
    // (PHASE4_VOLUME_TEXTURE_RAYMARCH_PREVIEW_REUSE.md, Step 3.2 item 2) -
    // owned directly, mirroring how Application.cpp already owns its own
    // instance for GET /get_texture's identical volume-preview use case
    // (VolumeTexturePreviewRenderer is explicitly designed to be cheap to
    // own one-per-consumer - see its own class comment: "self-contained...
    // this is invoked at most once per network request" - here, at most
    // once per real capture trigger, per volume-writing pass). NOT
    // copyable/movable (see VolumeTexturePreviewRenderer's own class
    // comment) - this is fine, since FrameDebuggerCurrentCapture itself is
    // never copied/moved anywhere either (a plain, in-place member of
    // FrameDebuggerPanel - Panels/FrameDebuggerPanel.h).
    VolumeTexturePreviewRenderer m_volumePreviewRenderer;
};

} // namespace gte
