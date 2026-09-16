#pragma once

#include "../FrameDebuggerData.h"
#include "../FrameDebuggerHistory.h"
#include "../FrameDebuggerPreviewProcessing.h"
#include "../EditorLayer.h" // FrameDebuggerStateSnapshotView - PHASE7.

#include <volk.h>

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
// task_manager/frame-debugger-3 campaign, PHASE4
// (PHASE4_PANEL_REAL_TREE_AND_FRAME_HISTORY_UI.md) - Build() now reads
// PHASE3's m_history's currently-viewed entry instead of
// BuildPlaceholderFrameDebuggerSnapshot() (falling back to that same
// placeholder only when m_history has never captured anything yet - still a
// real, honest, reachable "enabled, but not yet captured" state), a new
// Frame-History mini-toolbar (BuildFrameHistoryToolbarRow()) lets the user
// scrub across up to kCapacity past captured frames, and the RenderTarget
// preview box displays the currently-viewed entry's own real retained
// preview texture (m_previewDescriptor, an ImGui_ImplVulkan_AddTexture()-
// wrapped VkDescriptorSet this class owns itself - see EnsurePreviewDescriptor()'s
// own doc comment for why ownership lives HERE rather than in
// ImGuiEditorLayer, mirroring BoneViewerWindow.h's own "owns its own GPU
// texture/ImGui descriptor" precedent) instead of the "No Texture"
// placeholder.
class FrameDebuggerPanel {
public:
    FrameDebuggerPanel() = default;

    // Releases m_previewDescriptor (see ReleasePreviewDescriptor()'s own doc
    // comment for why this - not just relying on implicit member
    // destruction - matters).
    ~FrameDebuggerPanel();

    FrameDebuggerPanel(const FrameDebuggerPanel&) = delete;
    FrameDebuggerPanel& operator=(const FrameDebuggerPanel&) = delete;

    // `renderer`/`renderGraph`/`gameView` are the CURRENT frame's real,
    // already-rendered collaborators (ImGuiEditorLayer's own m_gameView,
    // the SAME Renderer/RenderGraph Application drives every frame) -
    // stashed for the rest of THIS call only (see TriggerCapture()'s own
    // doc comment), never retained across frames.
    // `compositedGameView` - the CURRENT frame's real, post-atmosphere-composite
    // final Game View output (ImGuiEditorLayer's own m_gameViewComposited), or
    // nullptr on a frame where no composited texture exists yet (mirrors
    // `gameView`'s own "stashed for the rest of THIS call only" contract, just
    // nullable) - see TriggerCapture()'s own doc comment for how this flows into
    // FrameDebuggerHistory::CaptureFrame().
    // `gpuSkinningPassNamesThisFrame` - the CURRENT frame's real
    // AnimationSystem::GpuSkinningDispatchRequest::name values, already
    // resolved to plain strings by the caller (ImGuiEditorLayer::BuildUI())
    // - see PHASE2's BuildRealFrameDebuggerSnapshot() for why this function
    // itself never needs to #include AnimationSystem.h.
    void Build(EditorContext& ctx, Renderer& renderer, const rg::RenderGraph& renderGraph, RenderTexture& gameView,
        RenderTexture* compositedGameView, const std::vector<std::string>& gpuSkinningPassNamesThisFrame);

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

    // PHASE4 - releases m_previewDescriptor (an ImGui_ImplVulkan_AddTexture()
    // descriptor, see EnsurePreviewDescriptor()'s own doc comment), if one
    // currently exists. MUST be called explicitly by ImGuiEditorLayer's own
    // destructor BEFORE ImGui_ImplVulkan_Shutdown() runs (mirroring
    // AssetPreviewMesh::Reset()/AssetPreviewTexture::Reset()/
    // BoneViewerWindow::Reset()'s own identical requirement - see that
    // destructor's own comment) - relying on THIS class's own destructor
    // alone would run too late, since FrameDebuggerPanel is declared (and
    // therefore destroyed, in reverse order) BEFORE ImGuiEditorLayer's own
    // explicit destructor BODY (which calls ImGui_ImplVulkan_Shutdown())
    // even starts. Safe to call repeatedly / on an already-empty instance.
    void ReleasePreviewDescriptor();

    // task_manager/frame-debugger-3 campaign, PHASE7
    // (PHASE7_NETWORK_HTTP_AUTOMATION_AND_MAIN_VIEWPORT_PINNING.md) - HTTP
    // automation entry points, called from ImGuiEditorLayer's own
    // FrameDebuggerOpenWindow()/FrameDebuggerSetEnabled()/etc. overrides
    // (EditorLayer.h). Each one mirrors exactly what its corresponding
    // piece of hand-driven UI already does - see each method's own body.

    // Opens the window (a no-op if already open) and arms the one-shot
    // main-viewport pin for the very next Build() call - see
    // m_pinToMainViewportNextOpen's own doc comment below.
    void RequestOpenWindow(EditorContext& ctx);

    // Mirrors the "Enable" checkbox's own false->true/true->false edge
    // detection exactly (see ApplyEnabledEdge()).
    void SetEnabledFromCommand(EditorContext& ctx, bool enabled);

    // Mirrors the "Capture" button's own BeginDisabled(!m_enabled) guard -
    // returns false (a safe no-op) if not currently enabled, true (after
    // performing a real TriggerCapture()) otherwise.
    bool CaptureNowFromCommand();

    // Mirrors a tree-row click - clamps via ClampSelectedEventIndex()
    // against the currently-viewed captured frame's own totalEventCount.
    void SelectEventFromCommand(int index);

    // Mirrors a Frame-History Prev/Next button click.
    void StepFrameHistoryFromCommand(int delta);

    // Mirrors a Channels row button click. Returns false (a safe no-op,
    // defense in depth only - see EditorLayer.h's own doc comment) if
    // `channel` isn't exactly one of "all"/"r"/"g"/"b"/"a".
    bool SetChannelFromCommand(const std::string& channel);

    // Mirrors the Levels DragFloatRange2 control, including its own
    // defensive white > black + 0.001f clamp.
    void SetLevelsFromCommand(float black, float white);

    // Builds the read-only state view GET /frame_debugger/state (and every
    // other /frame_debugger/* response's own "state" field) reports.
    FrameDebuggerStateSnapshotView BuildStateSnapshotView(const EditorContext& ctx) const;

private:
    void BuildToolbarRow(EditorContext& ctx);
    void BuildFrameHistoryToolbarRow();
    void BuildFrameStepperRow(const FrameDebuggerSnapshot& snapshot);
    void BuildEventTreePane(const FrameDebuggerSnapshot& snapshot);
    void RenderEventNode(const FrameDebuggerEventNode& node);
    void BuildInspectorPane(const FrameDebuggerSnapshot& snapshot, const FrameDebuggerHistoryEntry* currentEntry);
    void BuildEventDetailsSection(const std::optional<FrameDebuggerEventDetails>& details);

    // PHASE7 - shared by BOTH the "Enable" checkbox's own edge-detection
    // (BuildToolbarRow()) and SetEnabledFromCommand() above, so hand-driven
    // UI and HTTP automation can never silently diverge in behavior. See
    // BuildToolbarRow()'s own original comment (now here) for why the
    // false->true edge auto-engages Pause and triggers the first capture,
    // and why turning Enable back OFF deliberately does NOT auto-resume.
    void ApplyEnabledEdge(EditorContext& ctx, bool newEnabled);

    // PHASE4 - (re)creates m_previewDescriptor whenever the currently-viewed
    // history entry's own retained preview texture's VkImageView differs
    // from whatever m_previewDescriptor currently wraps (a history-cursor
    // move, OR a brand-new capture landing on the same cursor position -
    // either way, FrameDebuggerHistory::CaptureFrame() always creates a
    // FRESH RenderTexture with a genuinely new VkImageView - see that
    // method's own doc comment) - mirrors ImGuiEditorLayer::BuildUI()'s own
    // "only re-wrap when the underlying VkImageView actually changed"
    // gameViewDescriptor/sceneViewDescriptor caching logic exactly (see
    // PHASE0_MASTER_STRATEGY.md's own Step 2 finding), just against
    // PHASE3's retained HISTORICAL copy instead of the live, currently-
    // rendering Game View. Releases (and leaves null) m_previewDescriptor
    // whenever there is currently nothing to preview at all (no history
    // entry yet, or - defensively - an entry whose own preview is
    // std::nullopt), so BuildInspectorPane() can fall back to the "No
    // Texture" placeholder with a single, simple `m_previewDescriptor ==
    // VK_NULL_HANDLE` check.
    //
    // OWNERSHIP CHOICE (this phase's own documented Step 2 finding,
    // PHASE4_PANEL_REAL_TREE_AND_FRAME_HISTORY_UI.md): the wrapping call
    // itself, ImGui_ImplVulkan_AddTexture(), does NOT live in
    // Panels/GamePanel.cpp (that file only ever consumes an ALREADY-WRAPPED
    // VkDescriptorSet, EditorContext::gameViewDescriptor) - it lives in
    // ImGuiEditorLayer.cpp's own BuildUI(). Rather than plumb a THIRD
    // descriptor field onto the shared EditorContext (and a getter back out
    // of FrameDebuggerHistory for ImGuiEditorLayer to read every frame, just
    // to decide whether/what to re-wrap), this new preview descriptor is
    // instead owned directly by FrameDebuggerPanel itself, exactly like
    // BoneViewerWindow already owns its own m_descriptor/m_renderTexture
    // pair (see BoneViewerWindow.h's own class comment: "Owns its GPU
    // buffers/RenderTexture/ImGui descriptor/pipeline for as long as
    // they're needed") - FrameDebuggerPanel is already a stateful class that
    // owns comparable per-frame state (m_captureContext, m_history), so this
    // is the smallest, most self-contained change: no new EditorContext
    // field, no new ImGuiEditorLayer method, and the ImGui/Vulkan wrapping
    // detail stays entirely local to the one class that actually displays
    // it.
    void EnsurePreviewDescriptor();

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

    // PHASE6 (task_manager/frame-debugger-3/PHASE6_CHANNELS_AND_LEVELS_REAL_PREVIEW.md)
    // - the real, persisted Channels/Levels state (Locked Design Decision
    // #8, PHASE0_MASTER_STRATEGY.md). Defaults ("All", [0, 1]) are the
    // neutral/no-op case - EnsurePreviewDescriptor() below displays the RAW
    // retained history texture directly whenever both are still at these
    // defaults, skipping the compute dispatch entirely (a valid, cheap
    // optimization - see that phase's own Step 3.2).
    FrameDebuggerPreviewChannel m_channel = FrameDebuggerPreviewChannel::All;
    float m_levelsBlack = 0.0f;
    float m_levelsWhite = 1.0f;

    // PHASE6 - the dedicated, small, on-demand GPU compute dispatcher this
    // panel owns (see FrameDebuggerPreviewProcessing.h's own class comment)
    // - NEVER touches the retained history entry's own texture in place;
    // always writes into its own separate, persistent scratch texture.
    FrameDebuggerPreviewRenderer m_previewProcessor;

    // PHASE6 - "is the currently-processed preview still up to date"
    // bookkeeping, so EnsurePreviewDescriptor() only actually re-dispatches
    // the compute shader when m_channel/m_levelsBlack/m_levelsWhite/the
    // viewed history entry's own retained texture genuinely changed since
    // the last dispatch - never every single ImGui frame (see PHASE6's own
    // Step 2 "only recompute when dirty" discipline).
    VkImageView m_lastProcessedSourceView = VK_NULL_HANDLE;
    FrameDebuggerPreviewChannel m_lastProcessedChannel = FrameDebuggerPreviewChannel::All;
    float m_lastProcessedLevelsBlack = 0.0f;
    float m_lastProcessedLevelsWhite = 1.0f;

    // PHASE4 - this class's own ImGui-side descriptor for the currently-
    // displayed preview image (PHASE6: either the currently-viewed history
    // entry's RAW retained texture, or m_previewProcessor's own processed
    // scratch texture - see EnsurePreviewDescriptor()'s own doc comment
    // above for exactly which one, and when). VK_NULL_HANDLE whenever there
    // is nothing to preview right now.
    VkDescriptorSet m_previewDescriptor = VK_NULL_HANDLE;

    // Which VkImageView m_previewDescriptor currently wraps - compared
    // against EnsurePreviewDescriptor()'s own freshly-computed "desired"
    // view every call to decide whether it needs to re-wrap (mirrors
    // ImGuiEditorLayer's own m_lastKnownGameView/m_lastKnownSceneView
    // convention exactly). PHASE6: this is now DELIBERATELY DIFFERENT from
    // "which raw history entry is being viewed" (see
    // m_lastKnownRawPreviewView below) - a non-neutral Channels/Levels
    // state means this wraps m_previewProcessor's own scratch VkImageView
    // instead of the raw retained one.
    VkImageView m_lastKnownPreviewView = VK_NULL_HANDLE;

    // PHASE6 - a SEPARATE piece of bookkeeping from m_lastKnownPreviewView
    // above, tracking ONLY the currently-viewed history entry's own RAW
    // retained texture VkImageView (never the processed scratch one) -
    // Build()'s own "did the viewed history entry itself change underneath
    // us" detection (which resets m_selectedEventIndex to -1 on a
    // Frame-History Prev/Next navigation) MUST compare against this, not
    // m_lastKnownPreviewView - reusing m_lastKnownPreviewView for that check
    // would incorrectly reset the selection on EVERY SINGLE frame whenever
    // a non-neutral Channels/Levels state is active (since it then wraps
    // the processed view, which never equals the raw entry's own view by
    // construction). Refreshed unconditionally once per Build() call,
    // before EnsurePreviewDescriptor() runs.
    VkImageView m_lastKnownRawPreviewView = VK_NULL_HANDLE;

    // The live VkDevice, refreshed unconditionally every Build() call from
    // `renderer.GetVulkanContextInfo().device` (cheap - a few field reads) -
    // used only by this class's own destructor (see ~FrameDebuggerPanel())
    // to safely wait for the GPU to be idle before its final
    // ImGui_ImplVulkan_RemoveTexture() call, mirroring BoneViewerWindow's own
    // m_device precedent.
    VkDevice m_device = VK_NULL_HANDLE;

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
    RenderTexture* m_frameGameViewComposited = nullptr; // NEW - PHASE1 (frame-debugger-4). Nullable: mirrors
        // ImGuiEditorLayer::m_gameViewComposited's own "nullptr until the atmosphere
        // composite pass has produced something at least once this session" contract exactly
        // - see Build()'s own new parameter doc comment.
    std::vector<std::string> m_frameGpuSkinningPassNames;

    // True for exactly one BuildToolbarRow() call after
    // NotifyStepConsumed() was called (read-and-cleared) - see that
    // method's own doc comment.
    bool m_stepCaptureRequested = false;

    // task_manager/frame-debugger-3 campaign, PHASE7
    // (PHASE7_NETWORK_HTTP_AUTOMATION_AND_MAIN_VIEWPORT_PINNING.md, Step
    // 3.4) - true for exactly the NEXT Build() call after RequestOpenWindow()
    // ran and actually flipped ctx.frameDebuggerWindowOpen from false to
    // true PROGRAMMATICALLY. That one Build() call forces
    // ImGui::SetNextWindowViewport(main viewport)/SetNextWindowPos()/
    // SetNextWindowSize() before ImGui::Begin(), so the window can never be
    // classified as an independent OS-level platform window before
    // GET /get_swapchain has had a chance to see it at least once (see
    // SwapchainCaptureService.h - it only ever reads back the MAIN
    // window's own swapchain image). Cleared back to false immediately
    // after that one Build() call - a ONE-SHOT pin, never a permanent
    // lock: a human is still completely free to drag the window away
    // afterwards, exactly like every other floating window in this
    // codebase (BoneViewerWindow, ...). Ordinary manual "Window > Frame
    // Debugger" use (DockLayout.cpp's own checkable menu item) never
    // touches this bool at all - it flips ctx.frameDebuggerWindowOpen
    // directly, with no reference to this class to call RequestOpenWindow()
    // on, so manual use keeps exactly the same drag-anywhere freedom it
    // already had before this phase.
    bool m_pinToMainViewportNextOpen = false;
};

} // namespace gte
