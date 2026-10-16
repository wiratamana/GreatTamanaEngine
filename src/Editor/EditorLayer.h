#pragma once

#include "../Core/GraphicsHandles.h"

#include "../Math/Mat4.h"
#include "../Math/Vec3.h"
#include "../Renderer/RenderTexture.h"
#include "../Renderer/Culling/GpuDrivenBatchDebugInfo.h"
#include "../Core/Plugins/RenderFeatureDebugEntry.h"
#include "../Renderer/RenderGraph/RenderGraphTypes.h"
// Included at file scope: including it inside `namespace gte` below would create `gte::gte`.
#include "../Core/FrameDebuggerCaptureRecorder.h"
#include "../Core/FrameDebuggerEventSink.h" // Per-draw event capture; see FrameDebuggerGetEventSinkForInstall().

#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <vector>

// Forward-declared so this header needs no SDL dependency.
union SDL_Event;

namespace gte {

class Window;
class Renderer;
class Game;

namespace rg {
class RenderGraph;
class RenderGraphBuilder;
class RenderPassToggleRegistry;
class RenderPassBlackboard;
} // namespace rg

// Forward-declared: this header only passes these pointers through and never dereferences them.
class RenderFeatureCompositor;
class IEngineFeatureModule;
class IProjectLifecycleCapability;
class IAssetScaffoldingCapability;
class IHotReloadDebugCapability;

// Result of ActivateTab(). Kept separate from Application's own outcome type so this header
// never depends on src/Application/; Application::Run() converts between them.
struct TabActivationResult {
    bool tabExists = false;
};

// Result of ImportExternalAssetIntoProject(). Separate from Application's outcome type for the same reason.
struct ProjectAssetImportResult {
    bool projectAvailable = true;
    bool success = false;
    std::string message;
    std::string finalRelativePath;
    std::string finalAbsolutePath;
    std::string guid;
    bool convertedToMeshAsset = false;
    std::string meshSourceFormat;
    bool convertedToKtx2 = false;
    bool convertedToMotionAsset = false;
    std::uint64_t meshVertexCount = 0;
    std::uint64_t meshTriangleCount = 0;
};

// Read-only Frame Debugger state snapshot. Application::Run() copies it into its own outcome type.
struct FrameDebuggerStateSnapshotView {
    bool enabled = false;
    bool windowOpen = false;
    // True when a captured frame exists (only one is ever kept).
    bool hasCapturedFrame = false;
    int totalEventCount = 0;
    int selectedEventIndex = -1;
    std::string channel = "all";
    float levelsBlack = 0.0f;
    float levelsWhite = 1.0f;

    // High-water mark of per-draw event indices for the captured frame (0 before any capture).
    int perDrawEventCount = 0;
};

// One per-draw event's captured image. `pixels` is PNG-encoded, like GET /get_texture.
// `found` is false until that index has been captured; the caller polls again.
struct FrameDebuggerEventTextureResult {
    bool found = false;
    std::vector<std::uint8_t> pixels;
    std::uint32_t width = 0;
    std::uint32_t height = 0;
};

// Result of SpawnGpuDrivenTestBatch(). `editorAvailable` is false when the build has no Editor,
// so callers need not match on `errorMessage` text.
struct GpuDrivenTestBatchSpawnResult {
    bool success = false;
    bool editorAvailable = true;
    std::string errorMessage;
    std::uint32_t instanceCount = 0;
};

// Boundary between engine core and the optional Editor UI; only src/Editor/ ever includes ImGui.
// The Editor observes Game/Renderer through their public accessors; Game never knows the Editor exists.
// ImGuiEditorLayer is the real implementation; NullEditorLayer no-ops everything (render straight to swapchain).
class IEditorLayer {
public:
    virtual ~IEditorLayer() = default;

    // Feeds one raw SDL event to the editor's input handling.
    virtual void ProcessEvent(const SDL_Event& event) = 0;

    // OS window resized. The real implementation ignores it: the Game/Scene views track their panel size.
    virtual void OnWindowResized(int width, int height) = 0;

    // Starts a new UI frame. Call once per frame, before Game's Update()/Render().
    virtual void NewFrame() = 0;

    // Render target for the "Game" panel this frame, or nullptr to render straight to the swapchain
    // (no Editor) or skip the view (panel not visible). Resizes the texture to the panel size first.
    virtual RenderTexture* GameViewTarget() = 0;

    // Scene-view equivalent of GameViewTarget(), with its own separate RenderTexture.
    virtual RenderTexture* SceneViewTarget() = 0;

    // Projection * view the Scene view renders with, for a target of the given aspect ratio.
    // Backed by the orbitable EditorCamera; Identity for NullEditorLayer.
    virtual Mat4 SceneViewProjection(float aspectWidthOverHeight) const = 0;

    // World-space eye position of the Scene view's EditorCamera. Zero for NullEditorLayer.
    virtual Vec3 SceneViewCameraWorldPosition() const = 0;

    // Texture the "Game" panel shows instead of its raw view once a feature publishes a composited output.
    // nullptr on frames where the Game view was not rendered; the panel then falls back to its own view.
    virtual void SetGameViewCompositedTexture(RenderTexture* texture) = 0;

    // Scene-view equivalent of SetGameViewCompositedTexture().
    virtual void SetSceneViewCompositedTexture(RenderTexture* texture) = 0;

    // Records the Scene panel's ground grid into `cmd`, inside the open Scene view pass after the scene
    // geometry so the grid is depth-tested. `sceneViewProjection` is the matrix the scene was drawn with.
    // A no-op for NullEditorLayer.
    virtual void RenderSceneGrid(Renderer& renderer, VkCommandBuffer cmd, const Mat4& sceneViewProjection) = 0;

    // Builds every editor panel for this frame in a full-viewport dock space. Call after Game rendered.
    // `gpuDrivenBatchDebugInfo` has one entry per GPU-driven batch (Game view).
    // `renderFeatureEntries` is the resolved order of loaded render-feature plugins; empty when none.
    virtual void BuildUI(Game& game, Renderer& renderer, const rg::RenderGraph& renderGraph,
        const std::vector<GpuDrivenBatchDebugInfo>& gpuDrivenBatchDebugInfo,
        const std::vector<RenderFeatureDebugEntry>& renderFeatureEntries,
        // Never null. The Render Graph panel reads and edits pass toggles through it (main thread only).
        rg::RenderPassToggleRegistry& renderPassToggleRegistry,
        // Null when no render-feature plugin is loaded. The panel edits plugin enable/priority through it.
        RenderFeatureCompositor* renderFeatureCompositor,
        // Never null. The offscreen blackboard of this frame, read by the Frame Debugger panel.
        const rg::RenderPassBlackboard& offscreenBlackboard,
        // Layout of Core's scene-services descriptor set, used by editor spawn paths.
        DescriptorSetLayoutHandle sceneServicesSetLayout) = 0;

    // Records this frame's UI draw data into `cmd`, called while the swapchain image is bound
    // as the current dynamic-rendering color attachment.
    virtual void Render(VkCommandBuffer cmd) = 0;

    // Updates and renders extra OS windows (panels dragged outside the main viewport).
    // Call after Renderer::Present(); a no-op on frames where Render() did not run.
    virtual void RenderPlatformWindows() = 0;

    // True once the user picked File > Exit. Checked each frame to end the main loop.
    virtual bool WantsExit() const = 0;

    // True if the Editor UI wants exclusive mouse input this frame (cursor over a panel, dragging, ...).
    virtual bool WantsCaptureMouse() const = 0;

    // Keyboard equivalent of WantsCaptureMouse().
    virtual bool WantsCaptureKeyboard() const = 0;

    // True while gameplay simulation is paused via the toolbar.
    // Reflects the previous frame's BuildUI(), so it lags the click by one frame.
    virtual bool IsPlaybackPaused() const = 0;

    // Read-and-clear: true once per "Step" click. Called every frame so a stale request is drained.
    virtual bool TryConsumeStepRequest() = 0;

    // Brings the named panel's tab to the front. The caller validates `panelName`.
    // Returns tabExists == false if no live window has that name this frame.
    // Call between NewFrame() and BuildUI() of the same frame.
    virtual TabActivationResult ActivateTab(const std::string& panelName) = 0;

    // Imports one external file into the Project folder at `destinationRelativeFolder` ("" = root).
    // Same path as dropping a file on the Project panel. Returns projectAvailable == false when
    // the build has no Project panel.
    virtual ProjectAssetImportResult ImportExternalAssetIntoProject(
        const std::string& sourceAbsolutePath, const std::string& destinationRelativeFolder) = 0;

    // Returns a freshly reset capture recorder if the Frame Debugger window is open and enabled, else nullptr.
    // Call once per frame before the Game-view render. Reflects the previous frame's UI state.
    virtual IFrameDebuggerCaptureRecorder* PrepareFrameDebuggerCaptureContext() = 0;

    // Records that a Step happened this frame; the capture itself runs later this frame inside BuildUI().
    virtual void NotifyFrameDebuggerStepConsumed() = 0;

    // Read-and-clear: true for the one frame after a capture trigger, so that frame can declare replay passes.
    // Call once per frame before the "GameView" pass is declared.
    virtual bool ConsumePendingFrameDebuggerReplayRequest() = 0;

    // Opens the Frame Debugger window (idempotent) and pins it to the main viewport for that opening,
    // so GET /get_swapchain can see it immediately.
    virtual void FrameDebuggerOpenWindow() = 0;

    // Mirrors the "Enable" checkbox edges, including auto-pause on enable.
    // The capture itself is deferred to the next Build().
    virtual void FrameDebuggerSetEnabled(bool enabled) = 0;

    // Hands the ImGui implementation the capability behind the "New Project..." window.
    // Called once from EditorHost's constructor.
    virtual void SetProjectLifecycleCapability(IProjectLifecycleCapability* capability) = 0;

    // Hands the ImGui implementation the capability behind the "Create New Asset" window.
    // Called once from EditorHost's constructor.
    virtual void SetAssetScaffoldingCapability(IAssetScaffoldingCapability* capability) = 0;

    // Hands the ImGui implementation the capability behind the "Project > Compile" menu item.
    // Called once from EditorHost's constructor.
    virtual void SetHotReloadDebugCapability(IHotReloadDebugCapability* capability) = 0;

    // Requests a capture. Returns false if the Frame Debugger is not enabled.
    // Arms a deferred trigger; the capture happens on the next frame.
    virtual bool FrameDebuggerCaptureNow() = 0;

    // Selects the leaf event with this global index (clamped to the captured frame's event count).
    virtual void FrameDebuggerSelectEvent(int index) = 0;

    // Sets the active channel: lowercase "all"/"r"/"g"/"b"/"a". Returns false for any other string.
    virtual bool FrameDebuggerSetChannel(const std::string& channel) = 0;

    // Sets the Levels black/white points, keeping white > black + 0.001.
    virtual void FrameDebuggerSetLevels(float black, float white) = 0;

    // Read-only status snapshot for GET /frame_debugger/state. Default-valued for NullEditorLayer.
    virtual FrameDebuggerStateSnapshotView FrameDebuggerGetState() const = 0;

    // The session's single FrameDebuggerEventSink, installed once on the live RenderGraph. nullptr for NullEditorLayer.
    virtual FrameDebuggerEventSink* FrameDebuggerGetEventSinkForInstall() = 0;

    // Returns the image retained for per-draw event `eventIndex`, and re-arms capture for it if needed.
    // Poll until `found`; always not found for NullEditorLayer.
    virtual FrameDebuggerEventTextureResult FrameDebuggerGetEventTexture(int eventIndex) = 0;

    // Spawns `instanceCount` entities sharing one indexed mesh and pipeline, forming a GPU-driven batch.
    // Always unavailable (success == false) for NullEditorLayer.
    // `sceneServicesSetLayout` is the layout of Core's scene-services descriptor set.
    virtual GpuDrivenTestBatchSpawnResult SpawnGpuDrivenTestBatch(
        Game& game, Renderer& renderer, std::uint32_t instanceCount, DescriptorSetLayoutHandle sceneServicesSetLayout) = 0;

    // Hands every built-in feature module to the Editor once; modules with a registered
    // Editor panel factory get their panel built.
    virtual void AttachBuiltinFeatureModules(const std::vector<std::unique_ptr<IEngineFeatureModule>>& modules,
        Renderer& renderer, const rg::RenderGraph& renderGraph) = 0;

    // Sets the Render Graph panel's regime: true = "Present", false = "Offscreen (Game/Scene View)".
    // Returns true if the panel was open when applied. Always false for NullEditorLayer.
    virtual bool SetRenderGraphDisplayedRegime(bool present) = 0;
};

// Creates the real ImGui-backed editor layer (defined in src/Editor/ImGuiEditorLayer.cpp).
std::unique_ptr<IEditorLayer> CreateEditorLayer(Window& window, Renderer& renderer);

// Creates a no-op editor layer (defined in src/Editor/NullEditorLayer.cpp) for hosts without the Editor module.
std::unique_ptr<IEditorLayer> CreateNullEditorLayer(Window& window, Renderer& renderer);

} // namespace gte
