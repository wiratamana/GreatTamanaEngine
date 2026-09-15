#pragma once

#include "../FrameDebuggerData.h"
#include "../FrameDebuggerHistory.h"

#include <string>
#include <vector>

namespace gte {

struct EditorContext;
class Renderer;
class RenderTexture;

namespace rg {
class RenderGraph;
} // namespace rg

// task_manager/frame-debugger-2 campaign (PHASE2) - the Editor's
// "Frame Debugger" window: a Unity-Frame-Debugger-style tool for
// inspecting one captured frame's draw-call/render-pass hierarchy. An
// ON-DEMAND FLOATING WINDOW (like BoneViewerWindow.h), NOT part of the
// default dock layout and NOT listed in EditorPanelCatalog.h (see
// PHASE0_MASTER_STRATEGY.md's Locked Design Decision #6) - opened/closed
// via a checkable "Window > Frame Debugger" menu item
// (DockLayout.cpp's BuildDockspaceAndMenuBar()) that flips
// EditorContext::frameDebuggerWindowOpen directly; Build() below is a
// complete no-op whenever that bool is false, mirroring
// BoneViewerWindow::Build()'s own "no-op unless open" shape exactly,
// just gated on a shared EditorContext bool instead of a private member
// (see this class's own .cpp file for why: the thing that opens this
// window, DockLayout.cpp, has no reference to a FrameDebuggerPanel
// instance to call an Open()-style method on).
//
// task_manager/frame-debugger-3 campaign, PHASE3
// (PHASE3_FRAME_HISTORY_RING_BUFFER_AND_CAPTURE_TRIGGER.md) - this panel now
// owns the real capture context (m_captureContext) and the real multi-frame
// history ring buffer (m_history), and wires the actual capture TRIGGER
// (Enable's false->true edge, a Step while Enabled, and the new explicit
// "Capture" button below) - see TriggerCapture()'s own doc comment. PHASE3
// deliberately does NOT yet switch the displayed event tree/inspector over
// to m_history's real data (that is PHASE4's job) - Build() still reads
// BuildPlaceholderFrameDebuggerSnapshot() for its own on-screen content.
//
// A small STATEFUL CLASS, not a stateless free function - mirrors
// RenderGraphPanel/ProfilerPanel/BoneViewerWindow's own precedent
// (AGENTS.md, "Editor Module Structure" pre-approves this exception):
// this panel owns its own "Enable" toggle, currently-selected event
// index, and splitter width across frames. Still called explicitly BY
// NAME from ImGuiEditorLayer::BuildUI() - no IEditorPanel interface
// introduced.
class FrameDebuggerPanel {
public:
    // `renderer`/`renderGraph`/`gameView` are the CURRENT frame's real,
    // already-rendered collaborators (ImGuiEditorLayer's own m_gameView,
    // the SAME Renderer/RenderGraph Application drives every frame) -
    // stashed for the rest of THIS call only (see TriggerCapture()'s own
    // doc comment), never retained across frames.
    // `gpuSkinningPassNamesThisFrame` - the CURRENT frame's real
    // AnimationSystem::GpuSkinningDispatchRequest::name values, already
    // resolved to plain strings by the caller (ImGuiEditorLayer::BuildUI())
    // - see PHASE2's BuildRealFrameDebuggerSnapshot() for why this function
    // itself never needs to #include AnimationSystem.h.
    void Build(EditorContext& ctx, Renderer& renderer, const rg::RenderGraph& renderGraph, RenderTexture& gameView,
        const std::vector<std::string>& gpuSkinningPassNamesThisFrame);

    // See IEditorLayer::PrepareFrameDebuggerCaptureContext()'s own doc
    // comment (EditorLayer.h) - the real implementation this forwards to.
    // Resets m_captureContext and returns it whenever
    // ctx.frameDebuggerWindowOpen && m_enabled are both true; nullptr
    // otherwise.
    FrameDebuggerCaptureContext* PrepareCaptureContextForThisFrame(EditorContext& ctx);

    // See IEditorLayer::NotifyFrameDebuggerStepConsumed()'s own doc comment
    // - the real implementation this forwards to. Merely records the fact
    // for TriggerCapture()'s own later use this same frame (see
    // BuildToolbarRow()).
    void NotifyStepConsumed() noexcept;

private:
    void BuildToolbarRow(EditorContext& ctx);
    void BuildFrameStepperRow();
    void BuildEventTreePane(const FrameDebuggerSnapshot& snapshot);
    void RenderEventNode(const FrameDebuggerEventNode& node);
    void BuildInspectorPane(const FrameDebuggerSnapshot& snapshot);
    void BuildEventDetailsSection(const std::optional<FrameDebuggerEventDetails>& details);

    // PHASE3's own Step 3.2 - performs ONE real capture: builds PHASE2's
    // real FrameDebuggerSnapshot from THIS frame's already-cached
    // renderer/renderGraph/gameView/gpuSkinningPassNames (see Build()
    // above), hands it (plus the live Game View texture) to
    // m_history.CaptureFrame(), and resets m_selectedEventIndex to -1 (a
    // freshly captured frame has nothing selected yet - matches
    // frame-debugger-2's own "freshly opened window starts with nothing
    // selected" convention). Called from exactly three places, all inside
    // BuildToolbarRow(): the "Enable" checkbox's own false->true edge, the
    // new explicit "Capture" button, and a pending Step-triggered capture
    // (see NotifyStepConsumed() above) - see that method's own body for the
    // one-frame-lag caveat the Enable-edge/Capture-button paths carry (this
    // frame's own m_captureContext was armed based on m_enabled as of the
    // END of last frame - see PrepareCaptureContextForThisFrame()). A safe
    // no-op if Build() was never called this session yet (defensive only -
    // unreachable in practice, since BuildToolbarRow() itself is only ever
    // called from inside Build()).
    void TriggerCapture();

    // The panel's own "Enable" toggle - toggling it ALSO now triggers the
    // real capture TRIGGER machinery (PHASE3) on top of frame-debugger-2's
    // own pre-existing "auto-engage Pause" effect. False by default - a
    // freshly-opened window starts disabled, matching Unity's own Frame
    // Debugger.
    bool m_enabled = false;

    // Persisted (across frames) pixel width of the left-hand event tree
    // pane, adjusted live by dragging the splitter - mirrors
    // ProjectPanel::m_leftPaneWidth's own convention exactly (see
    // Panels/ProjectPanel.cpp).
    float m_leftPaneWidth = 320.0f;

    // The currently-selected leaf event's global index (matches
    // FrameDebuggerEventNode::eventIndex), or -1 if nothing is selected.
    // Set by RenderEventNode()'s own click handling below; read by
    // BuildEventDetailsSection() (PHASE6) via FindEventDetailsByIndex().
    int m_selectedEventIndex = -1;

    // PHASE3 - the real per-frame recorder (PHASE1) this panel owns and
    // arms via PrepareCaptureContextForThisFrame() above.
    FrameDebuggerCaptureContext m_captureContext;

    // PHASE3 - the real multi-frame ring buffer (see FrameDebuggerHistory.h).
    FrameDebuggerHistory m_history;

    // This-frame-only cached context for TriggerCapture()'s own use - see
    // Build() above. Non-owning: every one of these points at a long-lived
    // object Application/ImGuiEditorLayer already owns for this whole
    // frame's remaining lifetime. Reset to null/empty only conceptually
    // (never explicitly cleared at frame end) - always overwritten by the
    // very next Build() call before anything could read a stale value, since
    // TriggerCapture() is only ever invoked from within BuildToolbarRow(),
    // itself only ever called from within THIS SAME Build() call.
    Renderer* m_frameRenderer = nullptr;
    const rg::RenderGraph* m_frameRenderGraph = nullptr;
    RenderTexture* m_frameGameView = nullptr;
    std::vector<std::string> m_frameGpuSkinningPassNames;

    // True for exactly one BuildToolbarRow() call after
    // NotifyStepConsumed() was called (read-and-cleared) - see that
    // method's own doc comment.
    bool m_stepCaptureRequested = false;
};

} // namespace gte
