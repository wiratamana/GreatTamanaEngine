#pragma once

#include "../FrameDebuggerData.h"
#include "../FrameDebuggerHistory.h"
#include "../FrameDebuggerPreviewProcessing.h"
#include "../EditorLayer.h" // FrameDebuggerStateSnapshotView
#include "../../Core/Extent.h"
#include "../../Core/UiTextureHandle.h"
#include "../../Renderer/VolumeTexturePreviewRenderer.h" // Full type: held by value below.
#include "../../Renderer/Texture2D.h"

#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace gte {

struct EditorContext;
class Renderer;
class RenderTexture;

namespace rg {
class RenderGraph;
class RenderPassToggleRegistry;
class RenderPassBlackboard;
} // namespace rg

// The Editor's "Frame Debugger" window: inspects one captured frame's draw-call and render-pass hierarchy.
// An on-demand floating window opened through EditorContext::frameDebuggerWindowOpen; Build() is a no-op while closed.
// It owns the capture context, the retained capture and the preview descriptors; ImGuiEditorLayer calls it by name.
class FrameDebuggerPanel {
public:
    FrameDebuggerPanel() = default;
    ~FrameDebuggerPanel();

    FrameDebuggerPanel(const FrameDebuggerPanel&) = delete;
    FrameDebuggerPanel& operator=(const FrameDebuggerPanel&) = delete;

    // Builds the window for the current frame. The collaborators are only used during this call.
    // `compositedGameView` is the final Game view output, or nullptr when none exists. `toggleRegistry` and
    // `offscreenBlackboard` may be null; the matching capture-time detectors are then skipped.
    void Build(EditorContext& ctx, Renderer& renderer, const rg::RenderGraph& renderGraph, RenderTexture& gameView,
        RenderTexture* compositedGameView, rg::RenderPassToggleRegistry* toggleRegistry = nullptr,
        const rg::RenderPassBlackboard* offscreenBlackboard = nullptr);

    // Backs IEditorLayer::PrepareFrameDebuggerCaptureContext(): resets and returns the capture context while the
    // window is open and enabled, nullptr otherwise.
    FrameDebuggerCaptureContext* PrepareCaptureContextForThisFrame(EditorContext& ctx);

    // Backs IEditorLayer::NotifyFrameDebuggerStepConsumed(): remembers that a step happened this frame.
    void NotifyStepConsumed() noexcept;

    // Early half of the capture handshake, called while the frame graph is being built: consumes the pending
    // capture request and returns true when this frame must declare replay passes. Build() later in the same
    // frame then runs the real capture.
    bool ConsumePendingReplayRequest();

    // Releases the step preview descriptor. ImGuiEditorLayer calls this before the UI backend shuts down;
    // safe to call repeatedly.
    void ReleasePreviewDescriptor();

    // Releases the shader-property preview texture and its descriptor (waits for the GPU when something is held).
    // Public for the same reason as ReleasePreviewDescriptor(); safe to call repeatedly.
    void ReleaseShaderPropertyTexturePreview();

    // HTTP automation entry points; each mirrors the matching piece of hand-driven UI.

    // Opens the window and pins it once onto the main viewport so swapchain captures can see it.
    void RequestOpenWindow(EditorContext& ctx);

    // Same edge handling as the "Enable" checkbox.
    void SetEnabledFromCommand(EditorContext& ctx, bool enabled);

    // Arms a deferred capture like the "Capture" button; false (no-op) while not enabled.
    bool CaptureNowFromCommand();

    // Mirrors a tree-row click, clamped to the captured frame's event count.
    void SelectEventFromCommand(int index);

    // Mirrors a channel button. False when `channel` is not one of "all", "r", "g", "b", "a".
    bool SetChannelFromCommand(const std::string& channel);

    // Mirrors the Levels control, including its white > black clamp.
    void SetLevelsFromCommand(float black, float white);

    // Read-only state reported by GET /frame_debugger/state.
    FrameDebuggerStateSnapshotView BuildStateSnapshotView(const EditorContext& ctx) const;

    // The session's one per-draw event sink; valid for this object's lifetime.
    FrameDebuggerEventSink* EventSinkForInstall() noexcept { return &m_captureContext; }

    // Backs GET /frame_debugger/get_event_texture.
    FrameDebuggerEventTextureResult GetEventTextureFromCommand(int eventIndex);

private:
    void BuildToolbarRow(EditorContext& ctx);
    void BuildFrameStepperRow(const FrameDebuggerSnapshot& snapshot);
    void BuildEventTreePane(const FrameDebuggerSnapshot& snapshot);
    void RenderEventNode(const FrameDebuggerEventNode& node);
    void BuildInspectorPane(const FrameDebuggerSnapshot& snapshot, const FrameDebuggerHistoryEntry* currentEntry);
    void BuildEventDetailsSection(const std::optional<FrameDebuggerEventDetails>& details);

    // Shared by the "Enable" checkbox and SetEnabledFromCommand(). Enabling pauses playback and arms a deferred
    // capture; disabling clears the capture without resuming playback.
    void ApplyEnabledEdge(EditorContext& ctx, bool newEnabled);

    // Re-creates m_previewDescriptor when the previewed image view changed, and releases it when there is nothing
    // to preview. The panel owns this descriptor itself instead of putting it on the shared EditorContext.
    void EnsurePreviewDescriptor();

    // "View" button handler: drops the shown preview, then captures `textureName` once and uploads it.
    // On a lookup miss or upload failure it sets m_shaderPropertyPreviewLookupFailed and no descriptor.
    // Never called for buffer or texture-array rows (they get no "View" button).
    void RequestShaderPropertyTexturePreview(const std::string& textureName, rg::ResourceKind kind);

    // The only place m_selectedEventIndex changes; `newIndex` is already clamped. A real change also drops
    // any shader-property preview.
    void SetSelectedEventIndex(int newIndex);

    // Runs one capture from this frame's cached collaborators and resets the selection to -1.
    // Called only from the top of Build(), after this frame's rendering and replay passes executed.
    void TriggerCapture();

    bool m_enabled = false; // Off by default, like a freshly opened window in other frame debuggers.

    float m_leftPaneWidth = 320.0f; // Event tree pane width, dragged through the splitter.

    int m_selectedEventIndex = -1; // Global index of the selected leaf event, or -1.

    FrameDebuggerCaptureContext m_captureContext;

    FrameDebuggerCurrentCapture m_currentCapture; // Exactly one captured frame at a time.

    // Channels and levels state. The neutral defaults show the retained texture directly, without a compute pass.
    FrameDebuggerPreviewChannel m_channel = FrameDebuggerPreviewChannel::All;
    float m_levelsBlack = 0.0f;
    float m_levelsWhite = 1.0f;

    // Last preview source choice, so BuildInspectorPane() can pick the right placeholder text.
    FrameDebuggerPreviewSourceChoice m_lastPreviewChoice = FrameDebuggerPreviewSourceChoice::None;

    // On-demand compute dispatcher for non-neutral channels/levels; writes its own scratch texture and never
    // touches the retained capture.
    FrameDebuggerPreviewRenderer m_previewProcessor;

    // What the processed preview was last built from; the dispatch re-runs only when one of these changes.
    std::uint64_t m_lastProcessedSourceView = 0;
    FrameDebuggerPreviewChannel m_lastProcessedChannel = FrameDebuggerPreviewChannel::All;
    float m_lastProcessedLevelsBlack = 0.0f;
    float m_lastProcessedLevelsWhite = 1.0f;

    // UI handle of the displayed step preview (the raw retained texture or the processed scratch texture).
    // Invalid when there is nothing to preview.
    UiTextureHandle m_previewDescriptor = UiTextureHandle::Invalid;

    // View identity m_previewDescriptor wraps; compared against the freshly chosen one every call.
    std::uint64_t m_lastKnownPreviewView = 0;

    // View identity of the capture's raw retained texture only (never the processed one). Build() compares it to
    // detect a new capture and reset the selection.
    std::uint64_t m_lastKnownRawPreviewView = 0;

    // Non-empty while the preview box shows a one-shot shader-property texture instead of the step preview:
    // the render-graph resource name the user clicked "View" on.
    std::string m_shaderPropertyPreviewName;

    rg::ResourceKind m_shaderPropertyPreviewKind = rg::ResourceKind::Texture;

    // True when the last shader-property lookup or upload failed, so the box shows a distinct placeholder.
    bool m_shaderPropertyPreviewLookupFailed = false;

    // The owned copy behind m_shaderPropertyPreviewDescriptor, for both the 2D and the volume case. It never
    // points at the graph's pooled textures, which can be recycled.
    std::unique_ptr<Texture2D> m_shaderPropertyPreviewTexture;
    UiTextureHandle m_shaderPropertyPreviewDescriptor = UiTextureHandle::Invalid;
    Extent2D m_shaderPropertyPreviewExtent{};

    // On-demand volume raymarch renderer (the one GET /get_texture uses for volumes).
    VolumeTexturePreviewRenderer m_shaderPropertyVolumeRenderer;

    // Collaborators of the current Build() call, cached for TriggerCapture() and the toolbar. Non-owning and
    // overwritten at the top of every Build(), before anything reads them.
    Renderer* m_frameRenderer = nullptr;
    const rg::RenderGraph* m_frameRenderGraph = nullptr;
    RenderTexture* m_frameGameView = nullptr;
    RenderTexture* m_frameGameViewComposited = nullptr; // Nullable.
    rg::RenderPassToggleRegistry* m_frameToggleRegistry = nullptr; // Nullable; gates the honesty detector.
    const rg::RenderPassBlackboard* m_frameBlackboard = nullptr; // Nullable; gates the side-channel detector.

    // Set by NotifyStepConsumed(); read and cleared by the next toolbar row.
    bool m_stepCaptureRequested = false;

    // Set by every capture trigger (Enable edge, Step, "Capture" button); consumed by
    // ConsumePendingReplayRequest() early in the next frame.
    bool m_pendingCaptureTrigger = false;

    // Set by ConsumePendingReplayRequest() and consumed at the top of Build() later in the same frame, which then
    // calls TriggerCapture(). A second flag is needed because the same trigger is read at two different points.
    bool m_replayServicedThisFrame = false;

    // ctx.playbackPaused as of the previous Build(); detects "playback resumed while enabled", which clears the
    // capture.
    bool m_wasPlaybackPaused = false;

    // True for the one Build() after RequestOpenWindow() opened the window: pins it onto the main viewport so
    // swapchain captures can see it. It can be dragged away afterwards.
    bool m_pinToMainViewportNextOpen = false;
};

} // namespace gte
