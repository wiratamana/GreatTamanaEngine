#pragma once

#include "../EditorWindow.h"
#include "../FrameDebuggerData.h"
#include "../FrameDebuggerCaptureHandshake.h"
#include "../FrameDebuggerCapturedFrame.h"
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

// The Frame Debugger window: inspects one captured frame's draw calls and render passes. Closed by default.
// Owns the capture context, the retained capture and the preview descriptors.
class FrameDebuggerPanel final : public EditorWindow {
public:
    FrameDebuggerPanel() : EditorWindow("Frame Debugger", WindowDock::Floating, false, 900.0f, 600.0f) {}
    ~FrameDebuggerPanel();

    FrameDebuggerPanel(const FrameDebuggerPanel&) = delete;
    FrameDebuggerPanel& operator=(const FrameDebuggerPanel&) = delete;

    // True while a capture can be served: Enable on and the window open. The Game view must render in that case.
    bool IsCaptureArmed() const noexcept;

    // Resets and returns the per-draw capture context while armed; deactivates it and returns nullptr otherwise.
    FrameDebuggerCaptureContext* PrepareCaptureContextForThisFrame();

    // A single-step happened this frame: requests a new capture while enabled.
    void NotifyStepConsumed() noexcept;

    // First half of the capture handshake: true when this frame must declare replay passes.
    bool ConsumePendingReplayRequest();

    // Core calls this when the offscreen build threw. Fails a request that is in flight.
    void NotifyOffscreenFailed();

    // Releases the step preview descriptor before the UI backend shuts down. Safe to call repeatedly.
    void ReleasePreviewDescriptor();

    // Releases the shader-property preview texture and descriptor (waits for the GPU when one is held).
    void ReleaseShaderPropertyTexturePreview();

    // HTTP entry points; each mirrors the matching piece of hand-driven UI.
    // Same as the "Enable" checkbox; enabling also opens the window.
    void SetEnabledFromCommand(EditorContext& ctx, bool enabled);

    // Requests a capture like the "Capture" button. False (no-op) while not enabled.
    bool CaptureNowFromCommand();

    // Mirrors a tree-row click, clamped to the captured frame's event count.
    void SelectEventFromCommand(int index);

    // Mirrors a channel button. False when `channel` is not one of "all", "r", "g", "b", "a".
    bool SetChannelFromCommand(const std::string& channel);

    // Mirrors the Levels control, including its white > black clamp.
    void SetLevelsFromCommand(float black, float white);

    // Read-only state for GET /frame_debugger/state.
    FrameDebuggerStateSnapshotView BuildStateSnapshotView() const;

    // The session's one per-draw event sink; valid for this object's lifetime.
    FrameDebuggerEventSink* EventSinkForInstall() noexcept { return &m_captureContext; }

    // Backs GET /frame_debugger/get_event_texture.
    FrameDebuggerEventTextureResult GetEventTextureFromCommand(int eventIndex);

protected:
    void OnFrame(EditorFrame& frame) override;
    void OnGui(EditorFrame& frame) override;

private:
    void BuildToolbarRow(EditorContext& ctx);
    void BuildEmptyState();
    void BuildFrameStepperRow(const FrameDebuggerSnapshot& snapshot);
    void BuildEventTreePane(const FrameDebuggerSnapshot& snapshot);
    void RenderEventNode(const FrameDebuggerEventNode& node);
    void BuildInspectorPane(const FrameDebuggerSnapshot& snapshot, const FrameDebuggerCapturedFrame* currentFrame);
    void BuildEventDetailsSection(const std::optional<FrameDebuggerEventDetails>& details);

    // Shared by the checkbox and HTTP. Enable pauses a running game; disable resumes it only if Enable paused it.
    void ApplyEnabledEdge(EditorContext& ctx, bool newEnabled);

    // Keeps m_previewDescriptor on the previewed image view; releases it when there is nothing to preview.
    void EnsurePreviewDescriptor();

    // "View" button: drops the shown preview, then captures `textureName` once and uploads it.
    // A lookup miss or upload failure sets m_shaderPropertyPreviewLookupFailed.
    void RequestShaderPropertyTexturePreview(const std::string& textureName, rg::ResourceKind kind);

    // The only place m_selectedEventIndex changes; `newIndex` is already clamped. A real change drops the
    // shader-property preview.
    void SetSelectedEventIndex(int newIndex);

    // Runs one capture from the collaborators cached by this frame's OnFrame().
    void TriggerCapture();

    bool m_enabled = false;

    float m_leftPaneWidth = 320.0f; // Event tree pane width, dragged through the splitter.

    int m_selectedEventIndex = -1; // Global index of the selected leaf event, or -1.

    FrameDebuggerCaptureContext m_captureContext;

    FrameDebuggerCurrentCapture m_currentCapture; // Exactly one captured frame at a time.

    // Capture life cycle: Core serves the replay half, OnFrame() the capture half.
    FrameDebuggerCaptureHandshake m_handshake;

    std::string m_captureError; // Reason text while the phase is Failed, else empty.

    // Captures pushed back in a row because the frame ran with stale-size persistent targets.
    int m_resizeDeferrals = 0;

    bool m_pausedByFrameDebugger = false; // True when Enable paused a running game (so Disable resumes it).

    // Channels and levels. The neutral defaults show the retained texture directly, without a compute pass.
    FrameDebuggerPreviewChannel m_channel = FrameDebuggerPreviewChannel::All;
    float m_levelsBlack = 0.0f;
    float m_levelsWhite = 1.0f;

    // Last preview source choice; picks the placeholder text in BuildInspectorPane().
    FrameDebuggerPreviewSourceChoice m_lastPreviewChoice = FrameDebuggerPreviewSourceChoice::None;

    // Compute dispatcher for non-neutral channels/levels. Writes its own scratch texture.
    FrameDebuggerPreviewRenderer m_previewProcessor;

    // What the processed preview was built from; the dispatch re-runs only when one of these changes.
    std::uint64_t m_lastProcessedSourceView = 0;
    FrameDebuggerPreviewChannel m_lastProcessedChannel = FrameDebuggerPreviewChannel::All;
    float m_lastProcessedLevelsBlack = 0.0f;
    float m_lastProcessedLevelsWhite = 1.0f;
    bool m_lastProcessedSceneReferred = false;

    // UI handle of the displayed step preview (raw retained or processed scratch texture), or Invalid.
    UiTextureHandle m_previewDescriptor = UiTextureHandle::Invalid;

    // View identity m_previewDescriptor wraps.
    std::uint64_t m_lastKnownPreviewView = 0;

    // View identity of the capture's raw retained texture. A change means a new capture.
    std::uint64_t m_lastKnownRawPreviewView = 0;

    // Render-graph resource name shown in the preview box instead of the step preview; empty when unused.
    std::string m_shaderPropertyPreviewName;

    // True when the last shader-property lookup or upload failed.
    bool m_shaderPropertyPreviewLookupFailed = false;

    // Owned copy behind m_shaderPropertyPreviewDescriptor. Never points at the graph's pooled textures.
    std::unique_ptr<Texture2D> m_shaderPropertyPreviewTexture;
    UiTextureHandle m_shaderPropertyPreviewDescriptor = UiTextureHandle::Invalid;
    Extent2D m_shaderPropertyPreviewExtent{};

    // Volume raymarch renderer, shared with GET /get_texture.
    VolumeTexturePreviewRenderer m_shaderPropertyVolumeRenderer;

    // Collaborators of the current frame. Non-owning; overwritten first thing in every OnFrame().
    Renderer* m_frameRenderer = nullptr;
    const rg::RenderGraph* m_frameRenderGraph = nullptr;
    RenderTexture* m_frameGameView = nullptr;
    RenderTexture* m_frameGameViewComposited = nullptr; // Nullable.
    rg::RenderPassToggleRegistry* m_frameToggleRegistry = nullptr; // Gates the honesty detector.
    const rg::RenderPassBlackboard* m_frameBlackboard = nullptr; // Gates the side-channel detector.
};

} // namespace gte
