#include "Application.h"

#include "AtmospherePassSequence.h"
#include "EventTranslator.h"
#include "EngineCommandDispatch.h"
#include "MemorySnapshotBuilder.h"
#include "RenderPasses.h"

#include "../Encoding/DepthVisualization.h"
#include "../Encoding/HdrColorVisualization.h"
#include "../Encoding/PixelConversion.h"
#include "../Encoding/PngEncoder.h"
#include "../Memory/SdlMemoryTracker.h"
#include "../Profiling/FrameProfiler.h"
#include "../Profiling/ScopeTimer.h"
#include "../Renderer/Atmosphere/AtmosphereParameters.h"
#include "../Renderer/RenderGraph/RenderGraphBarrierPlanner.h"
#include "../Renderer/RenderGraph/RenderGraphBuilder.h"
#include "../Renderer/RenderGraph/RenderGraphDebugTextureRegistry.h"
#include "ECS/TransformHierarchy.h"

#include <SDL3/SDL.h>

#include <cassert>
#include <cstdint>
#include <cstdio>
#include <stdexcept>

namespace gte {

namespace {

// Aspect ratio (width / height) of a render target, for
// RenderSystem::Draw()/Game::Render() - see Application::Run() below. Falls
// back to a square (1.0f) for a degenerate/zero-height extent (e.g. a
// render target caught mid-resize, or a minimized window) rather than
// dividing by zero.
float AspectRatioOf(int width, int height) noexcept
{
    return height > 0 ? static_cast<float>(width) / static_cast<float>(height) : 1.0f;
}


// Phase 4C (PHASE4_GPU_TIMESTAMP_QUERIES_STRATEGY_v2.md) - the one, tiny
// bridge from Renderer's own (Profiling-free) GpuTimingSample::Status into
// Profiling::GpuSampleStatus - see Application::Run()'s Game/Scene/Present
// blocks below. Trivial by design (a straightforward 1:1 switch), same
// judgment already applied to AspectRatioOf() above - not worth its own
// Tier-1 test file.
Profiling::GpuSampleStatus ToProfilingGpuSampleStatus(GpuTimingSample::Status status) noexcept
{
    switch (status) {
    case GpuTimingSample::Status::Present:
        return Profiling::GpuSampleStatus::Present;
    case GpuTimingSample::Status::Unsupported:
        return Profiling::GpuSampleStatus::Unsupported;
    case GpuTimingSample::Status::Absent:
    default:
        return Profiling::GpuSampleStatus::Absent;
    }
}

// network-impl-2 campaign, Phase 3
// (PHASE3_GAME_VIEW_CAPTURE_AND_GET_GAME_VIEW_ENDPOINT.md) - true for the
// two BGRA channel-order formats VulkanSwapchain.cpp's ChooseSurfaceFormat()
// is actually known to negotiate (it prefers VK_FORMAT_B8G8R8A8_UNORM but
// falls back to formats.front(), i.e. whatever the platform/driver reports
// first, if that exact combination isn't available). An unrecognized
// BGRA-like variant this two-value check doesn't catch would silently
// produce a channel-swapped (red/blue reversed) PNG with no error at all -
// an accepted, narrow risk (see PHASE3's own Non-Goals) - if a future
// "screenshot has wrong colors" report ever shows up, start here.
bool IsBgraFormat(VkFormat format) noexcept
{
    return format == VK_FORMAT_B8G8R8A8_UNORM || format == VK_FORMAT_B8G8R8A8_SRGB;
}

// network-impl-4 campaign, Phase 5 - GET /list_textures' own small,
// human-readable resolution helpers. Both are deliberately narrow (only the
// enumerators/formats this engine's render graph can actually produce
// today), mirroring IsBgraFormat()'s own "accepted narrow risk, documented"
// precedent immediately above - an unrecognized regime can never actually
// occur (ExecuteTimingMode has exactly two enumerators, both handled), and
// an unrecognized VkFormat falls back to a safe, clearly-labeled numeric
// string rather than a crash or a silently-wrong label, mirroring
// src/Editor/MemoryPanelData.cpp's own ToString(VkFormat) fallback
// convention (not reused directly - that function lives under
// GTE_ENABLE_EDITOR-gated src/Editor/, and this call site must work in
// every build configuration, editor or not).
const char* ToDebugTextureRegimeString(rg::ExecuteTimingMode mode)
{
    switch (mode) {
    case rg::ExecuteTimingMode::SynchronousImmediateReadback: return "synchronous";
    case rg::ExecuteTimingMode::PipelinedDeferredReadback: return "pipelined";
    }
    return "unknown"; // Unreachable - every real enumerator handled above.
}

std::string DebugTextureColorFormatName(VkFormat format)
{
    switch (format) {
    case VK_FORMAT_B8G8R8A8_UNORM: return "B8G8R8A8_UNORM";
    case VK_FORMAT_B8G8R8A8_SRGB: return "B8G8R8A8_SRGB";
    case VK_FORMAT_R8G8B8A8_UNORM: return "R8G8B8A8_UNORM";
    case VK_FORMAT_R8G8B8A8_SRGB: return "R8G8B8A8_SRGB";
    default: break;
    }
    char buffer[32];
    std::snprintf(buffer, sizeof(buffer), "VkFormat(%d)", static_cast<int>(format));
    return std::string(buffer);
}

} // namespace

Application::SdlContext::SdlContext()
{
#if GTE_ENABLE_EDITOR
    // Must be installed before SDL_Init() - indeed, before literally any SDL
    // call - see SdlMemoryTracker's own doc comment for why. Gated behind
    // GTE_ENABLE_EDITOR (not installed at all in a release build) since the
    // only consumer of these numbers is the Editor's "Memory" panel - a
    // release build would otherwise pay real per-allocation tracking
    // overhead (an extra pointer-arithmetic header + atomic increment on
    // EVERY SDL_malloc/calloc/realloc/free call, for the rest of the
    // process's lifetime) for a feature nothing in that build can ever
    // display. See AGENTS.md ("CPU Dependency Memory Tracking").
    SdlMemoryTracker::Install();
#endif

    if (!SDL_Init(SDL_INIT_VIDEO)) {
        throw std::runtime_error(std::string("SDL_Init failed: ") + SDL_GetError());
    }
}

Application::SdlContext::~SdlContext()
{
    SDL_Quit();
}

Application::Application(const std::string& title, int width, int height)
    : m_sdlContext()
    , m_window(title, width, height)
    , m_renderer(m_window)
    , m_renderGraph(m_renderer)
    , m_editorLayer(CreateEditorLayer(m_window, m_renderer))
    , m_game()
    // network-impl-2 campaign, Phase 3 - hands FrameCaptureBridge's address
    // into NetworkServer's constructor (a defaulted pointer parameter - see
    // NetworkServer.h) so its /get_game_view route handler can reach it.
    // Safe: m_captureBridge is declared (and thus constructed) before
    // m_networkServer, per Application.h's own member ordering.
    // network-impl-3 campaign, Phase 4 - ALSO hands EngineCommandBridge's
    // address into NetworkServer's constructor (a second, appended
    // defaulted pointer parameter), for the exact same reason -
    // m_commandBridge is likewise declared before m_networkServer.
    , m_networkServer(&m_captureBridge, &m_commandBridge)
    , m_windowWidth(width)
    , m_windowHeight(height)
{
#if GTE_ENABLE_NETWORK
    // Loopback-only (127.0.0.1 is baked into NetworkServer itself - Start()
    // deliberately has no host parameter, see Phase 2), port 8080 - see
    // AGENTS.md, "Networking", and
    // task_manager/network-impl-1/PHASE0_MASTER_STRATEGY.md's locked
    // design decisions for why this is hardcoded rather than configurable
    // yet, and why this is safe to auto-start unconditionally. A bind
    // failure (e.g. another instance of the engine already running and
    // holding port 8080) is logged by NetworkServer itself and is
    // NON-FATAL - the rest of Application still starts normally either way.
    m_networkServer.Start(8080);
#endif
}

Application::~Application() = default;

int Application::Run()
{
    bool running = true;
    // Identifies Application's own main game window among possibly several
    // real SDL windows (once Dear ImGui's multi-viewport feature creates
    // extra ones for panels dragged outside it) - see the mainWindowId doc
    // comment on EventTranslator::Translate(). Fetched once: an SDL window's
    // ID never changes over its lifetime.
    const Uint32 mainWindowId = m_window.Id();
    Uint64 lastTicksNs = SDL_GetTicksNS();

    InputState inputState;

    while (running) {
        // Brackets the WHOLE frame for the Profiling module (src/Profiling/)
        // - see PROFILER_STRATEGY_v2.md, Phase 1. A true no-op (frame count
        // doesn't advance) whenever GTE_ENABLE_PROFILER is off or the
        // runtime capture-enabled flag is false - see
        // Profiling::FrameProfiler.
        Profiling::FrameProfiler::Instance().BeginFrame();

        // Phase 4C (PHASE4_GPU_TIMESTAMP_QUERIES_STRATEGY_v2.md) - the
        // runtime layer of GpuTimingService's two-layer on/off gate (see
        // AGENTS.md, "Profiling", and Renderer::SetGpuTimingCaptureEnabled()'s
        // own doc comment): reuses the Editor's EXISTING "Capture" checkbox
        // rather than adding a new, Profiler-panel-specific control. A
        // plain bool crosses this boundary - Renderer stays completely free
        // of any Profiling/ header either way.
        m_renderer.SetGpuTimingCaptureEnabled(Profiling::FrameProfiler::Instance().IsCaptureEnabled());
        // B.1 (B1_REAL_GPU_TIMING_STRATEGY_v1.md) - the same runtime toggle,
        // now ALSO applied to the render graph's own, independent
        // RenderGraphTimestampPool (m_renderGraph never shares
        // GpuTimingService's pool with Renderer - see RenderGraph.h's own
        // "GPU TIMING NOTE").
        m_renderGraph.SetGpuTimingCaptureEnabled(Profiling::FrameProfiler::Instance().IsCaptureEnabled());

        // Clear last frame's transient "just pressed/released" flags and
        // per-frame mouse/wheel deltas before this frame's events arrive.
        inputState.BeginFrame();

        {
            GTE_PROFILE_SCOPE("Application::PollEvents");
            SDL_Event sdlEvent;
            while (SDL_PollEvent(&sdlEvent)) {
                // Editor gets first look at every raw event (its own SDL3
                // backend tracks mouse/keyboard for ImGui widgets) - a no-op in
                // a release build (NullEditorLayer).
                m_editorLayer->ProcessEvent(sdlEvent);

                const std::optional<Event> event = EventTranslator::Translate(sdlEvent, mainWindowId);
                if (!event.has_value()) {
                    continue;
                }

                if (event->type == EventType::Quit) {
                    running = false;
                } else if (event->type == EventType::WindowResized) {
                    const auto& resized = std::get<WindowResizedEventData>(event->data);
                    m_windowWidth = resized.width;
                    m_windowHeight = resized.height;
                    m_renderer.OnResize(resized.width, resized.height);
                    // Keeps the Editor's Game-view RenderTexture tracking the
                    // window's size (no-op in a release build) - see
                    // ImGuiEditorLayer::OnWindowResized.
                    m_editorLayer->OnWindowResized(resized.width, resized.height);
                }

                // Withhold mouse/keyboard events the Editor UI itself wants this
                // frame (e.g. the cursor is over an ImGui panel, a slider is
                // being dragged, a text field has keyboard focus, ...) from
                // ever reaching gameplay - otherwise clicking/typing into the
                // Editor's own panels would ALSO register as gameplay input
                // underneath them (the classic ImGui-in-a-game-engine
                // "click-through" problem). WantsCaptureMouse()/
                // WantsCaptureKeyboard() are always false for NullEditorLayer,
                // so a release build forwards every event to Game exactly as
                // before this check existed. Quit/WindowResized above are
                // handled unconditionally regardless of this - they aren't
                // input Game reacts to via InputState/OnEvent() in this sense,
                // and Renderer/the Editor's own resize handling must always see
                // them either way.
                const bool isMouseEvent = event->type == EventType::MouseMoved
                    || event->type == EventType::MouseButtonDown
                    || event->type == EventType::MouseButtonUp
                    || event->type == EventType::MouseWheel;
                const bool isKeyboardEvent = event->type == EventType::KeyDown || event->type == EventType::KeyUp;

                const bool consumedByEditorUI = (isMouseEvent && m_editorLayer->WantsCaptureMouse())
                    || (isKeyboardEvent && m_editorLayer->WantsCaptureKeyboard());

                if (!consumedByEditorUI) {
                    inputState.Apply(*event);
                    m_game.OnEvent(*event);
                }
            }
        }

        // network-impl-3 campaign (task_manager/network-impl-3/) - drains at
        // most ONE pending network-issued engine command
        // (instantiate_primitive/delete_entity) per frame, as EARLY as
        // possible - right after input polling, BEFORE Game::Update()/
        // Physics/Animation run this frame - so a freshly spawned/deleted
        // entity is fully consistent for the REST of this exact frame (see
        // PHASE4_ENGINE_COMMAND_BRIDGE_AND_MAIN_LOOP_INTEGRATION.md, and
        // PHASE0_MASTER_STRATEGY.md's own Locked Design Decision #6).
        if (const std::optional<EngineCommandRequest> request = m_commandBridge.TryPeekPendingCommandRequest()) {
            GTE_PROFILE_SCOPE("Application::ExecuteEngineCommand");
            const EngineCommandResult result = ExecuteEngineCommand(m_game, m_renderer, *request);
            m_commandBridge.FulfillCommand(result);
        }

        const Uint64 nowTicksNs = SDL_GetTicksNS();
        const double deltaSeconds = static_cast<double>(nowTicksNs - lastTicksNs) / 1000000000.0;
        lastTicksNs = nowTicksNs;

        m_editorLayer->NewFrame();

        // Clears last frame's queued Submit() draw items before Game gets a
        // chance to queue this frame's - see Renderer::BeginFrame().
        m_renderer.BeginFrame();

        // Game::Update() itself is already wrapped in
        // GTE_PROFILE_SCOPE("Game::Update") (see src/Game/Game.cpp) - not
        // wrapped again here too, since the flat, name-keyed CPU scope
        // aggregation model (see AGENTS.md, "Profiling") would otherwise
        // double-count identically-named nested scopes rather than
        // measuring the same call twice for no reason.
        m_game.Update(deltaSeconds, inputState);

        // Ask the Editor where Game's frame(s) should actually land this
        // frame: an off-screen RenderTexture per visible panel ("Game"
        // and/or "Scene", each independently - an Editor build), or nullptr
        // for either/both meaning "not currently visible, don't bother" (a
        // hidden/inactive dock tab) or "no Editor at all" (a release build -
        // see NullEditorLayer). This is the one seam that decides
        // Unity-style Editor-vs-final-build rendering, and it lives here in
        // Application (the composition root), not in Game.
        //
        // Render Graph migration (Phase 7 -
        // RENDERGRAPH_PHASE7_APPLICATION_MIGRATION_STRATEGY_v2.md): Game
        // view / Scene view / Present are recorded via TWO
        // RenderGraph::Execute() calls per frame - one for the SYNCHRONOUS
        // offscreen regime (Game+Scene together, sharing FramePresenter's
        // own dedicated offscreen command buffer/fence), one for the
        // PIPELINED swapchain regime (Present alone) - see RenderPasses.h
        // and this class's own m_renderGraph member. A dependency-cycle
        // exception from RenderGraphCompiler::Compile() is structurally
        // unreachable through any graph this engine declares today (see
        // RENDERGRAPH_PHASE3_COMPLETION_REPORT.md) but is still caught
        // here, loudly, per RENDERGRAPH_PHASE6_COMPLETION_REPORT.md's own
        // Step 3.5 guidance - never silently swallowed.
        RenderTexture* gameTarget = m_editorLayer->GameViewTarget();
        RenderTexture* sceneTarget = m_editorLayer->SceneViewTarget();

        // network-impl-2 campaign, Phase 3
        // (PHASE3_GAME_VIEW_CAPTURE_AND_GET_GAME_VIEW_ENDPOINT.md) - the
        // FAST-FAIL branch, placed HERE (unconditionally, every frame,
        // BEFORE the `if (gameTarget != nullptr || sceneTarget != nullptr)`
        // block below) rather than inside that block - a release build (or
        // an Editor build with both "Game"/"Scene" hidden) never enters that
        // block at all, so a fast-fail placed inside it would never run in
        // precisely the scenario it exists to handle, silently degrading
        // every such /get_game_view request to the bridge's full 3-second
        // timeout (HTTP 504) instead of an immediate HTTP 409. See that
        // phase document's own "IMPORTANT - a placement gotcha" note.
        if (gameTarget == nullptr && m_captureBridge.IsCaptureRequested(FrameCaptureKind::GameView)) {
            m_captureBridge.FailPendingRequest(FrameCaptureKind::GameView, FrameCaptureFailureReason::TargetNotAvailable);
        }

        // Call 1 of 2: the SYNCHRONOUS offscreen regime - Game view + Scene
        // view together. Runs unconditionally whenever either target is
        // non-null, completely independent of whatever the swapchain is
        // doing this frame (a minimized OS window does not affect this
        // call at all).
        if (gameTarget != nullptr || sceneTarget != nullptr) {
            try {
                GTE_PROFILE_SCOPE("RenderGraph::Execute(Offscreen)");
                const VkCommandBuffer offscreenCmd = m_renderer.BeginOffscreenRenderGraphRecording();
                m_renderGraph.Execute(offscreenCmd, rg::ExecuteTimingMode::SynchronousImmediateReadback,
                    [&](rg::RenderGraphBuilder& b) {
                        std::vector<rg::TextureHandle> outputs;

                        // GPU Vertex Skinning campaign, Phase 5
                        // (GPU_SKINNING_PHASE5_RUNTIME_CPU_GPU_SWITCH_STRATEGY_v2.md,
                        // Step 3.3) - declared FIRST, before either view pass
                        // below, so their own ResourceAccess::VertexBufferRead
                        // declarations (see RenderPasses.h) have a real
                        // BufferHandle to reference. A no-op (empty vector,
                        // nothing declared) in CPU skinning mode or whenever
                        // no rigged model is currently animating.
                        const std::vector<rg::BufferHandle> gpuSkinningBuffers =
                            AddGpuSkinningPasses(b, m_game, m_renderer);

                        // Atmosphere Scattering + Aerial Perspective campaign,
                        // Phase 7 (task_manager/atmosphere-scattering-1/
                        // ATMOSPHERE_PHASE7_SKY_BACKGROUND_AND_COMPOSITE_PASSES_v1.md)
                        // - the REAL, PERMANENT per-frame atmosphere pass
                        // sequence (relocated out of Phases 3-6's own
                        // temporary `// TODO(ATMOSPHERE_PHASE7)` validation
                        // call site - see AtmospherePassSequence.h/.cpp).
                        // Shared LUTs (Transmittance/Multi-Scattering) are
                        // declared ONCE per frame here (view-independent);
                        // the Sky-View LUT/Aerial Perspective volume are
                        // resolved PER VIEW, inside each view's own `if`
                        // block below, now covering BOTH Game View AND
                        // Scene View (Phases 5/6 deliberately left Scene
                        // View unwired - this is what finally wires it up).
                        // Phase 8 (ATMOSPHERE_PHASE8_SUN_ECS_AND_EDITOR_CONTROLS_v1.md)
                        // - m_atmosphereSettings::groundAlbedoTint is folded
                        // into the default Earth parameters here, once per
                        // frame, before the shared LUT passes read it (the
                        // Multi-Scattering LUT's own ground-bounce term is
                        // the only consumer of AtmosphereParametersGpu::
                        // groundAlbedo - see AtmosphereParameters.h). No
                        // dirty-flag optimization (unchanged from every
                        // prior phase's own "What We Will NOT Do").
                        AtmosphereParametersGpu atmosphereParameters = MakeDefaultEarthAtmosphereParameters();
                        atmosphereParameters.groundAlbedo =
                            atmosphereParameters.groundAlbedo * m_atmosphereSettings.groundAlbedoTint;
                        const AtmosphereSharedLutHandles atmosphereSharedLuts =
                            AddAtmosphereSharedLutPasses(b, m_renderer, m_atmosphereLutRenderer, atmosphereParameters);
                        outputs.push_back(atmosphereSharedLuts.transmittanceLutHandle);
                        outputs.push_back(atmosphereSharedLuts.multiScatteringLutHandle);

                        if (gameTarget != nullptr) {
                            const VkExtent2D extent = gameTarget->Extent();
                            const float aspect =
                                AspectRatioOf(static_cast<int>(extent.width), static_cast<int>(extent.height));

                            // Game View's own eye world position (active ECS
                            // Camera) and view-projection matrix (Phase 5's
                            // own helper, now permanently wired here).
                            const Vec3 gameEyeWorldPosition = ResolveActiveCameraWorldPosition(m_game.GetRegistry());
                            const Mat4 gameViewProjection =
                                RenderSystem::ResolveActiveCameraViewProjection(m_game.GetRegistry(), aspect);

                            const AtmosphereViewLutHandles gameAtmosphere = AddAtmosphereViewLutPasses(b, m_renderer,
                                m_atmosphereLutRenderer, m_game.GetRegistry(), atmosphereParameters,
                                atmosphereSharedLuts, gameEyeWorldPosition, gameViewProjection,
                                "AtmosphereSkyViewLut_GameView", "AtmosphereAerialPerspectiveVolume_GameView");
                            outputs.push_back(gameAtmosphere.skyViewLutHandle);
                            // A VolumeTextureHandle can never go into
                            // `outputs` (TextureHandle-only) - see
                            // RenderGraphBuilder::KeepVolumeTextureOutput()'s
                            // own doc comment for why this is a SEPARATE
                            // call, and ATMOSPHERE_PHASE6_COMPLETION_REPORT.md
                            // for the real Phase 2 gap this fixes.
                            b.KeepVolumeTextureOutput(gameAtmosphere.aerialPerspectiveVolumeHandle);

                            // Phase 9 (ATMOSPHERE_PHASE9_VALIDATION_DEBUG_TOOLING_AND_DOCS_v1.md,
                            // Step 3.2) - the volume-texture debug-visibility
                            // gap Phase 2 deliberately deferred: mirrors ONE
                            // Z slice of the Game View's own aerial-
                            // perspective volume into a real, registered 2D
                            // texture, automatically GET /get_texture/
                            // GET /list_textures-capturable with zero
                            // further networking changes. Game View only
                            // (per that method's own doc comment) - a
                            // plain rg::TextureHandle output, so it goes
                            // into the ordinary `outputs` root set, unlike
                            // the VolumeTextureHandle above.
                            const rg::TextureHandle aerialPerspectiveDebugSlice =
                                m_atmosphereLutRenderer.AddAerialPerspectiveVolumeDebugSlicePass(b, m_renderer,
                                    gameAtmosphere.aerialPerspectiveVolumeHandle,
                                    "AtmosphereAerialPerspectiveVolume_GameView",
                                    static_cast<std::uint32_t>(m_atmosphereSettings.aerialPerspectiveDebugSliceIndex),
                                    "AtmosphereAerialPerspectiveVolumeDebugSlice");
                            outputs.push_back(aerialPerspectiveDebugSlice);

                            // 3.2 - the Sky Background pass, issued INSIDE
                            // AddGameViewPass()'s own already-open
                            // vkCmdBeginRendering bracket, right after
                            // Game::Render()'s own real scene geometry draws
                            // (see RenderPasses.h's own updated doc comment).
                            const std::function<void(VkCommandBuffer)> recordGameSkyBackground =
                                MakeRecordSkyBackgroundCallback(m_atmosphereLutRenderer, m_renderer, gameViewProjection,
                                    atmosphereParameters, gameAtmosphere.frameUniforms, "AtmosphereSkyViewLut_GameView",
                                    m_atmosphereSettings.skyExposure);

                            const rg::TextureHandle h =
                                b.ImportTexture("GameView", gameTarget->Target(), VK_IMAGE_LAYOUT_UNDEFINED);
                            AddGameViewPass(b, m_game, m_renderer, h, aspect, gpuSkinningBuffers, recordGameSkyBackground);
                            outputs.push_back(h);

                            // 3.3 - the Aerial Perspective Composite pass -
                            // declared AFTER the GameView pass above (same
                            // builder/Execute() call), reading its own
                            // just-written color+depth (via the new
                            // isDepthResource=true ReadTexture() overload -
                            // see RenderGraphTypes.h) and writing a NEW,
                            // separate "GameViewComposited" output texture -
                            // this is what the Editor's "Game" panel and
                            // `GET /get_game_view` display/capture from now
                            // on (see the finalize block below).
                            const rg::TextureHandle gameComposited = AddAtmosphereCompositePass(b, m_renderer,
                                m_atmosphereLutRenderer, *gameTarget, h, gameAtmosphere.aerialPerspectiveVolumeHandle,
                                "AtmosphereAerialPerspectiveVolume_GameView", gameAtmosphere.frameUniforms,
                                gameEyeWorldPosition, m_atmosphereSettings.aerialPerspectiveStrength, extent,
                                "GameViewComposited");
                            outputs.push_back(gameComposited);
                        }
                        if (sceneTarget != nullptr) {
                            const VkExtent2D extent = sceneTarget->Extent();
                            const float aspect =
                                AspectRatioOf(static_cast<int>(extent.width), static_cast<int>(extent.height));
                            // Unlike the Game view above, the Scene view
                            // renders through the Editor's OWN independently-
                            // orbitable camera (see src/Editor/EditorCamera.h)
                            // rather than whatever ECS entity currently has
                            // the active Camera component - see
                            // IEditorLayer::SceneViewProjection()/
                            // Game::Render()'s viewProjectionOverride
                            // parameter. IEditorLayer::SceneViewCameraWorldPosition()
                            // (new this phase) is this view's own eye
                            // world-space position equivalent.
                            const Mat4 sceneViewProjection = m_editorLayer->SceneViewProjection(aspect);
                            const Vec3 sceneEyeWorldPosition = m_editorLayer->SceneViewCameraWorldPosition();

                            const AtmosphereViewLutHandles sceneAtmosphere = AddAtmosphereViewLutPasses(b, m_renderer,
                                m_atmosphereLutRenderer, m_game.GetRegistry(), atmosphereParameters,
                                atmosphereSharedLuts, sceneEyeWorldPosition, sceneViewProjection,
                                "AtmosphereSkyViewLut_SceneView", "AtmosphereAerialPerspectiveVolume_SceneView");
                            outputs.push_back(sceneAtmosphere.skyViewLutHandle);
                            b.KeepVolumeTextureOutput(sceneAtmosphere.aerialPerspectiveVolumeHandle);

                            const std::function<void(VkCommandBuffer)> recordSceneSkyBackground =
                                MakeRecordSkyBackgroundCallback(m_atmosphereLutRenderer, m_renderer, sceneViewProjection,
                                    atmosphereParameters, sceneAtmosphere.frameUniforms, "AtmosphereSkyViewLut_SceneView",
                                    m_atmosphereSettings.skyExposure);

                            const rg::TextureHandle h =
                                b.ImportTexture("SceneView", sceneTarget->Target(), VK_IMAGE_LAYOUT_UNDEFINED);
                            // The Editor's infinite ground grid (see
                            // task_manager/editor-enchancements-1/PHASE0_MASTER_STRATEGY.md) - a
                            // plain std::function keeps RenderPasses.cpp itself completely
                            // Editor-agnostic (see AGENTS.md, Clean Architecture); only this call
                            // site (which already legitimately holds m_editorLayer) knows the real
                            // callback reaches into IEditorLayer::RenderSceneGrid().
                            const std::function<void(VkCommandBuffer, const Mat4&)> recordSceneGrid =
                                [this](VkCommandBuffer cmd, const Mat4& viewProj) {
                                    m_editorLayer->RenderSceneGrid(m_renderer, cmd, viewProj);
                                };
                            // Sky background BEFORE the grid overlay - see
                            // RenderPasses.h's own AddSceneViewPass() doc
                            // comment for the full ordering reasoning.
                            AddSceneViewPass(b, m_game, m_renderer, h, aspect, sceneViewProjection, gpuSkinningBuffers,
                                recordSceneGrid, recordSceneSkyBackground);
                            outputs.push_back(h);

                            const rg::TextureHandle sceneComposited = AddAtmosphereCompositePass(b, m_renderer,
                                m_atmosphereLutRenderer, *sceneTarget, h, sceneAtmosphere.aerialPerspectiveVolumeHandle,
                                "AtmosphereAerialPerspectiveVolume_SceneView", sceneAtmosphere.frameUniforms,
                                sceneEyeWorldPosition, m_atmosphereSettings.aerialPerspectiveStrength, extent,
                                "SceneViewComposited");
                            outputs.push_back(sceneComposited);

                            // Phase 7 of the compute-shader campaign
                            // (COMPUTE_PHASE7_VALIDATION_TESTING_TOOLING_STRATEGY_v2.md)
                            // - the texture-side validation workload: a
                            // compute box-blur pass reading THIS call's own
                            // just-declared Scene view texture `h` (the
                            // PRE-atmosphere-composite color, unchanged by
                            // this phase - this debug tool has no reason to
                            // move onto the composited output) and writing
                            // the Editor's own persistent blurredSceneOutput
                            // RWTexture, declared into the SAME builder/
                            // Execute() call so the render graph's own
                            // automatic barrier planner synchronizes the
                            // cross-pass read entirely on its own (see
                            // ComputeBlurValidation.h). Declared (and this
                            // handle added to `outputs`) only when the
                            // Editor's own "Show Compute Blur (debug)"
                            // toggle is on and "Scene" is visible - see
                            // IEditorLayer::AddBlurValidationPass()'s own
                            // doc comment; std::nullopt (always the case
                            // for NullEditorLayer) means nothing was
                            // declared at all this call.
                            if (const std::optional<rg::TextureHandle> blurHandle =
                                    m_editorLayer->AddBlurValidationPass(b, m_renderer, h, extent)) {
                                outputs.push_back(*blurHandle);
                            }
                        }

                        return outputs;
                    });

                // Manual finalize: transitions whichever of Game/Scene were
                // actually rendered this call from ColorAttachmentWrite to a
                // real ShaderRead layout, for Dear ImGui's own (render-
                // graph-external) descriptor set to sample during the
                // Present regime call below - see RenderPasses.h's own doc
                // comment on FinalizeRenderTextureForExternalSampling().
                if (gameTarget != nullptr) {
                    FinalizeRenderTextureForExternalSampling(offscreenCmd, *gameTarget);
                    // network-impl-4 campaign, Phase 3 - keeps the debug-texture registry's
                    // OWN idea of "GameView"'s current color state correct across this
                    // graph-external manual transition - see PHASE0_MASTER_STRATEGY.md's
                    // Locked Design Decision 7. Must use the EXACT same ResourceState
                    // FinalizeRenderTextureForExternalSampling() itself just transitioned
                    // to (ShaderRead) - re-derive via the same rg::RequiredStateFor() call,
                    // never hand-guessed.
                    m_renderGraph.NotifyDebugTextureStateOverride(
                        "GameView", rg::RequiredStateFor(rg::ResourceAccess::ShaderRead, false));

                    // Atmosphere Scattering + Aerial Perspective campaign,
                    // Phase 7 - the FIFTH NotifyDebugTextureStateOverride()
                    // call site (see AGENTS.md's "Named Texture Capture"
                    // section - not yet updated there, per this campaign's
                    // own workflow rule that only Phase 9 touches
                    // AGENTS.md/README.md/TODO.md): finalizes
                    // "GameViewComposited" for external (ImGui/
                    // `/get_game_view`/`/get_texture`) sampling, then hands
                    // the Editor a stable pointer to it so "Game" displays
                    // the atmosphere-composited output PERMANENTLY from now
                    // on (see IEditorLayer::SetGameViewCompositedTexture()).
                    m_atmosphereLutRenderer.FinalizeAerialPerspectiveCompositeForSampling(
                        offscreenCmd, "GameViewComposited");
                    m_renderGraph.NotifyDebugTextureStateOverride(
                        "GameViewComposited", rg::RequiredStateFor(rg::ResourceAccess::ShaderRead, false));
                    m_editorLayer->SetGameViewCompositedTexture(m_atmosphereLutRenderer.CompositedOutput("GameViewComposited"));
                }
                if (sceneTarget != nullptr) {
                    FinalizeRenderTextureForExternalSampling(offscreenCmd, *sceneTarget);
                    m_renderGraph.NotifyDebugTextureStateOverride(
                        "SceneView", rg::RequiredStateFor(rg::ResourceAccess::ShaderRead, false));

                    // Same "SceneViewComposited" treatment as "GameViewComposited"
                    // above, for the Scene view.
                    m_atmosphereLutRenderer.FinalizeAerialPerspectiveCompositeForSampling(
                        offscreenCmd, "SceneViewComposited");
                    m_renderGraph.NotifyDebugTextureStateOverride(
                        "SceneViewComposited", rg::RequiredStateFor(rg::ResourceAccess::ShaderRead, false));
                    m_editorLayer->SetSceneViewCompositedTexture(m_atmosphereLutRenderer.CompositedOutput("SceneViewComposited"));
                }
                // Phase 7 of the compute-shader campaign - finalizes the
                // blurred-output texture for external (ImGui) sampling too,
                // a safe no-op whenever AddBlurValidationPass() above
                // didn't actually declare a pass this call - see
                // IEditorLayer::FinalizeBlurValidationForSampling()'s own
                // doc comment.
                m_editorLayer->FinalizeBlurValidationForSampling(offscreenCmd);
                // network-impl-4 campaign, Phase 3 - the "BlurredSceneOutput" correction
                // (Locked Design Decision 7's third of four call sites). Unlike GameView/
                // SceneView above, Application.cpp has NO visibility into whether
                // ComputeBlurValidation::FinalizeForSampling() actually did anything this
                // call (that is tracked purely internally, via its own private
                // m_writtenThisFrame flag - see ComputeBlurValidation.h's own doc comment
                // on why) - so this call is made UNCONDITIONALLY, every frame, rather than
                // guarded by an `if`. This is safe: RenderGraphDebugTextureRegistry::
                // ApplyColorStateOverride() (Phase 1) is a documented no-op when
                // "BlurredSceneOutput" isn't a currently-known name (the debug-blur toggle
                // has never been turned on this session), and idempotent (harmless) when
                // it's already correctly ShaderRead from an earlier frame's correction -
                // there is no code path where calling this "too often" produces a wrong
                // result.
                m_renderGraph.NotifyDebugTextureStateOverride(
                    "BlurredSceneOutput", rg::RequiredStateFor(rg::ResourceAccess::ShaderRead, false));

                m_renderer.EndOffscreenRenderGraphRecording();

                // network-impl-2 campaign, Phase 3
                // (PHASE3_GAME_VIEW_CAPTURE_AND_GET_GAME_VIEW_ENDPOINT.md) -
                // the SUCCESS-path capture. Guarded by `gameTarget !=
                // nullptr`, which only evaluates true on a frame where this
                // whole enclosing `if` block already ran (see this method's
                // own gameTarget/sceneTarget computation above) - so
                // EndOffscreenRenderGraphRecording() above is guaranteed to
                // have already run this frame too. CaptureRenderTexturePixels()
                // uses its OWN separate ImmediateSubmit() call (a fresh
                // command buffer/fence) - never `offscreenCmd`, which is
                // already ended/submitted by the call just above.
                //
                // Atmosphere Scattering + Aerial Perspective campaign,
                // Phase 7 - captures "GameViewComposited" (the atmosphere-
                // composited output) instead of the original, pre-composite
                // `*gameTarget`, now that it's PERMANENTLY what the Game
                // View actually displays - see this phase's own completion
                // report's explicit flag that every subsequent capture
                // includes the atmosphere effect. Falls back to `gameTarget`
                // itself only in the (should-be-unreachable, since the
                // composite pass above always runs whenever gameTarget !=
                // nullptr) case the composited texture somehow doesn't
                // exist yet, so a capture request is never silently dropped.
                if (gameTarget != nullptr && m_captureBridge.IsCaptureRequested(FrameCaptureKind::GameView)) {
                    RenderTexture* captureSource = m_atmosphereLutRenderer.CompositedOutput("GameViewComposited");
                    if (captureSource == nullptr) {
                        captureSource = gameTarget;
                    }
                    Renderer::CapturedRawPixels raw = m_renderer.CaptureRenderTexturePixels(*captureSource);
                    if (IsBgraFormat(raw.format)) {
                        Encoding::ConvertBgraToRgbaInPlace(raw.pixels.data(), raw.width, raw.height);
                    }
                    std::vector<std::uint8_t> png = Encoding::EncodeRgba8ToPng(raw.pixels.data(), raw.width, raw.height);
                    m_captureBridge.FulfillPendingRequest(FrameCaptureKind::GameView,
                        CapturedPngImage{ std::move(png), raw.width, raw.height });
                }

                // B.1 (B1_REAL_GPU_TIMING_STRATEGY_v1.md) - must run
                // immediately after EndOffscreenRenderGraphRecording()
                // returns (i.e. after that call's own fence wait has
                // already completed) - reads back every real GPU
                // timestamp written during the SynchronousImmediateReadback
                // Execute() call just above, for whichever of "GameView"/
                // "SceneView" actually ran this frame.
                m_renderGraph.FinalizeSynchronousGpuTiming();
            } catch (const std::exception& e) {
                std::fprintf(stderr, "RenderGraph offscreen Execute() failed: %s\n", e.what());
                assert(false && "RenderGraph offscreen Execute() threw - see stderr");
            }
        }

        // Not #if GTE_ENABLE_PROFILER-gated - see AGENTS.md's "Profiling"
        // section and this same function's own BeginFrame()/EndFrame()
        // calls, which aren't gated either; only GTE_PROFILE_SCOPE(...)'s
        // own macro body is compile-time-gated. "GameView"/"SceneView" must
        // match RenderPasses.cpp's own AddGameViewPass()/AddSceneViewPass()
        // pass name literals exactly.
        if (gameTarget != nullptr) {
            const rg::PassGpuStats gameViewStats = m_renderGraph.LastKnownStatsFor("GameView");
            Profiling::FrameProfiler::Instance().SetGpuPassDrawStats(Profiling::GpuPass::GameView,
                Profiling::GpuSampleStatus::Present, gameViewStats.drawStats.drawCallCount,
                gameViewStats.drawStats.triangleCount);
            Profiling::FrameProfiler::Instance().SetGpuPassTiming(Profiling::GpuPass::GameView,
                ToProfilingGpuSampleStatus(gameViewStats.timing.status), gameViewStats.timing.milliseconds);
        }
        if (sceneTarget != nullptr) {
            const rg::PassGpuStats sceneViewStats = m_renderGraph.LastKnownStatsFor("SceneView");
            Profiling::FrameProfiler::Instance().SetGpuPassDrawStats(Profiling::GpuPass::SceneView,
                Profiling::GpuSampleStatus::Present, sceneViewStats.drawStats.drawCallCount,
                sceneViewStats.drawStats.triangleCount);
            Profiling::FrameProfiler::Instance().SetGpuPassTiming(Profiling::GpuPass::SceneView,
                ToProfilingGpuSampleStatus(sceneViewStats.timing.status), sceneViewStats.timing.milliseconds);
        }

        // Build every editor panel (Hierarchy/Inspector/Scene/Game/Memory/menu
        // bar) now that the Game/Scene view textures (if any) have this
        // frame's contents. Passes Game itself (Hierarchy/Inspector observe/
        // edit its ECS world via Game::GetRegistry(), and Hierarchy's
        // "Create 3D Object" menu spawns entities via
        // Game::CreatePrimitiveEntity() - see IEditorLayer::BuildUI()) and
        // Renderer itself (Memory - see Renderer::GetMemoryTotals()/
        // GetMemoryResources()).
        {
            GTE_PROFILE_SCOPE("IEditorLayer::BuildUI");
            m_editorLayer->BuildUI(m_game, m_renderer, m_renderGraph, m_atmosphereSettings, m_atmosphereLutRenderer);
        }

        // File > Exit (or any other future programmatic "close" UI action)
        // ends the loop exactly like a Quit event/closing the OS window.
        if (m_editorLayer->WantsExit()) {
            running = false;
        }

        // network-impl-2 campaign, Phase 5
        // (PHASE5_GET_SWAPCHAIN_ENDPOINT_AND_FORMAT_NEGOTIATION_REUSE.md) -
        // must run BEFORE PresentViaRenderGraph() below so
        // SwapchainCaptureService::RecordCaptureIfRequested() (Phase 4) sees
        // m_captureRequested == true in time this frame. Unconditional, at
        // Run()'s own top-level body (never nested inside an
        // if (gameTarget != nullptr || sceneTarget != nullptr) block, unlike
        // the Game-view fast-fail check above) - runs every single frame, in
        // every build configuration, regardless of whether a Game/Scene view
        // exists this frame. Cheap, side-effect-free read per
        // IsCaptureRequested()'s own doc comment; RequestSwapchainCapture()
        // itself is idempotent/safe to call repeatedly while a request is
        // already pending.
        if (m_captureBridge.IsCaptureRequested(FrameCaptureKind::Swapchain)) {
            m_renderer.RequestSwapchainCapture();
        }
        // Call 2 of 2: the PIPELINED swapchain-present regime - Present
        // alone. In an Editor build this draws the editor's own ImGui
        // chrome (which itself displays the Game/Scene views above) via the
        // recordImGui hook; in a release build (or the rare Editor edge
        // case where both "Game"/"Scene" are simultaneously hidden) it ALSO
        // renders Game directly into the swapchain first - see
        // RenderPasses.h's AddPresentPass().
        {
            GTE_PROFILE_SCOPE("Renderer::PresentViaRenderGraph");
            const bool needsDirectGameRender = (gameTarget == nullptr && sceneTarget == nullptr);
            const std::optional<float> directGameRenderAspect = needsDirectGameRender
                ? std::optional<float>(AspectRatioOf(m_windowWidth, m_windowHeight))
                : std::nullopt;

            std::optional<DrawStats> presentStats;
            try {
                presentStats = m_renderer.PresentViaRenderGraph(m_renderGraph, needsDirectGameRender,
                    [&](rg::RenderGraphBuilder& b, rg::TextureHandle swapchainImage) {
                        // GPU Vertex Skinning campaign, Phase 5 - only
                        // meaningful when this pass is ALSO the one drawing
                        // Game directly (needsDirectGameRender); see this
                        // block's own directGameRenderAspect above. The
                        // offscreen regime already dispatched GPU skinning
                        // this frame whenever it ran at all (see the
                        // offscreen build lambda above) - the two are
                        // mutually exclusive per frame, so this never
                        // double-dispatches the same model's compute pass.
                        const std::vector<rg::BufferHandle> gpuSkinningBuffers = needsDirectGameRender
                            ? AddGpuSkinningPasses(b, m_game, m_renderer)
                            : std::vector<rg::BufferHandle>{};
                        AddPresentPass(b, m_game, m_renderer, swapchainImage, directGameRenderAspect,
                            [this](VkCommandBuffer cmd) { m_editorLayer->Render(cmd); }, gpuSkinningBuffers);
                        return std::vector<rg::TextureHandle>{ swapchainImage };
                    });
            } catch (const std::exception& e) {
                std::fprintf(stderr, "RenderGraph Present Execute() failed: %s\n", e.what());
                assert(false && "RenderGraph Present Execute() threw - see stderr");
            }

            // network-impl-2 campaign, Phase 5
            // (PHASE5_GET_SWAPCHAIN_ENDPOINT_AND_FORMAT_NEGOTIATION_REUSE.md) -
            // checked unconditionally right after PresentViaRenderGraph()
            // returns, whether or not it actually recorded/returned a value
            // this frame (a capture requested several frames ago may
            // complete on a frame whose own new Present pass was itself
            // skipped for an unrelated reason) - TakeLastCompletedSwapchainCapture()
            // is a cheap std::exchange() either way. Mirrors Phase 3's own
            // Game-view success-path capture shape exactly (BGRA->RGBA
            // conversion, then PNG encode, then FulfillPendingRequest()).
            if (std::optional<CapturedSwapchainPixels> raw = m_renderer.TakeLastCompletedSwapchainCapture()) {
                if (IsBgraFormat(raw->format)) {
                    Encoding::ConvertBgraToRgbaInPlace(raw->pixels.data(), raw->width, raw->height);
                }
                std::vector<std::uint8_t> png = Encoding::EncodeRgba8ToPng(raw->pixels.data(), raw->width, raw->height);
                m_captureBridge.FulfillPendingRequest(FrameCaptureKind::Swapchain,
                    CapturedPngImage{ std::move(png), raw->width, raw->height });
            }

            if (presentStats.has_value()) {
                Profiling::FrameProfiler::Instance().SetGpuPassDrawStats(Profiling::GpuPass::Present,
                    Profiling::GpuSampleStatus::Present, presentStats->drawCallCount, presentStats->triangleCount);
                // "Present" must match RenderPasses.cpp's own
                // AddPresentPass() pass name literal exactly.
                const GpuTimingSample presentTiming = m_renderGraph.LastKnownStatsFor("Present").timing;
                Profiling::FrameProfiler::Instance().SetGpuPassTiming(Profiling::GpuPass::Present,
                    ToProfilingGpuSampleStatus(presentTiming.status), presentTiming.milliseconds);
            }
            // else: PresentViaRenderGraph() recorded nothing this frame
            // (minimized window, pending resize, or a just-recreated
            // swapchain) - GpuPass::Present's countStatus AND timingStatus
            // both correctly stay at their default GpuSampleStatus::Absent,
            // with no extra code needed.
        }

        // Update/present any panel the user has dragged outside the main OS
        // window (Dear ImGui multi-viewport/"platform windows" - a no-op in
        // a release build, see NullEditorLayer::RenderPlatformWindows()).
        // Deliberately AFTER the main swapchain Present() above: each such
        // window owns its own, completely independent Vulkan swapchain, so
        // there is no ordering requirement against the main window's own
        // present - see IEditorLayer::RenderPlatformWindows().
        m_editorLayer->RenderPlatformWindows();

        // network-impl-4 campaign, Phase 4
        // (task_manager/network-impl-4/PHASE4_FRAMECAPTUREBRIDGE_NAMED_TEXTURE_SUPPORT.md) -
        // GET /get_texture's own capture path. Placed here (unconditionally, once
        // per Run() iteration, AFTER every render-graph Execute() call this frame)
        // so RenderGraph::DebugTextureSnapshotFor() sees the freshest possible
        // registry state, and so WaitForGpuIdle() below waits out every submission
        // this frame - both regimes - before the readback below runs. Cheap,
        // side-effect-free when nothing is pending (IsCaptureRequested() is a
        // plain bool read).
        if (m_captureBridge.IsCaptureRequested(FrameCaptureKind::NamedTexture)) {
            const std::string requestedName = m_captureBridge.RequestedTextureName();
            const DebugTextureChannel requestedChannel = m_captureBridge.RequestedTextureChannel();

            if (const std::optional<rg::DebugTextureSnapshot> snapshot = m_renderGraph.DebugTextureSnapshotFor(requestedName)) {
                const bool wantsDepth = (requestedChannel == DebugTextureChannel::Depth);
                if (wantsDepth && !snapshot->hasDepth) {
                    // Positively known, permanent-for-this-registration failure -
                    // fail fast (409) rather than waiting out the full timeout,
                    // exactly mirroring the Game-view "no panel visible"
                    // fast-fail's own reasoning.
                    m_captureBridge.FailPendingRequest(FrameCaptureKind::NamedTexture, FrameCaptureFailureReason::TargetNotAvailable);
                } else {
                    // PHASE0_MASTER_STRATEGY.md's Locked Design Decision 4 -
                    // computed BEFORE WaitForGpuIdle()/the readback below, from the
                    // snapshot as it was at the moment this request was actually
                    // serviced (never re-queried afterward - a capture that takes a
                    // few extra milliseconds to read back must not report itself
                    // as "0 frames old" merely because the CURRENT counter moved on
                    // in the meantime; it genuinely reflects THIS snapshot's own
                    // last-write frame, compared against "now").
                    const std::uint64_t framesSinceUpdate =
                        m_renderGraph.CurrentDebugTextureFrameCounter() - snapshot->lastUpdatedFrameCounter;

                    m_renderer.WaitForGpuIdle();

                    const VkImage image = wantsDepth ? snapshot->target.depthImage : snapshot->target.image;
                    const VkImageAspectFlags aspect = wantsDepth ? VK_IMAGE_ASPECT_DEPTH_BIT : VK_IMAGE_ASPECT_COLOR_BIT;
                    const VkFormat format = wantsDepth ? snapshot->target.depthFormat : snapshot->target.format;
                    const rg::ResourceState state = wantsDepth ? snapshot->depthState : snapshot->colorState;

                    // Atmosphere Scattering + Aerial Perspective campaign,
                    // Phase 4 (ATMOSPHERE_PHASE4_MULTISCATTERING_LUT_v1.md) -
                    // the FIRST capturable color texture that is genuinely
                    // NOT 4 bytes/pixel (VK_FORMAT_R16G16B16A16_SFLOAT is 8 -
                    // see AtmosphereLutRenderer's own Multi-Scattering LUT
                    // output). Every other capturable texture (color OR
                    // depth) is still exactly 4 bytes/pixel, unchanged - see
                    // Renderer::CaptureImagePixels()'s own doc comment.
                    const bool isHdrColor = !wantsDepth && format == VK_FORMAT_R16G16B16A16_SFLOAT;
                    const int bytesPerPixel = isHdrColor ? 8 : 4;

                    Renderer::CapturedRawPixels raw =
                        m_renderer.CaptureImagePixels(image, aspect, format, snapshot->target.extent, state, bytesPerPixel);

                    // A separate, always-4-bytes/pixel buffer PNG encoding
                    // actually reads from - identical to raw.pixels for
                    // every "traditional" 4-bytes/pixel capture (no copy
                    // needed - encodePixels just points straight at it), but
                    // a genuinely different, freshly-allocated buffer for the
                    // HDR case above (raw.pixels itself stays 8-bytes/pixel
                    // native data - ConvertHdrRgba16fToRgba8() below is an
                    // OUT conversion, not in-place, mirroring
                    // ConvertDepthToGrayscaleRgba8()'s own out-buffer shape).
                    std::vector<std::uint8_t> hdrConvertedPixels;
                    const std::uint8_t* encodePixels = raw.pixels.data();

                    bool ok = true;
                    if (wantsDepth) {
                        // Safe to write in-place into the SAME buffer it reads from
                        // (see PHASE4's own detailed correctness note): every pixel's
                        // 4 input bytes are fully read into a local temporary BEFORE
                        // any of that pixel's own 4 output bytes are written, and
                        // input/output share the exact same per-pixel byte offset
                        // (no shift) - never a different pixel's range.
                        ok = Encoding::ConvertDepthToGrayscaleRgba8(raw.pixels.data(), raw.format, raw.width, raw.height, raw.pixels.data());
                    } else if (isHdrColor) {
                        hdrConvertedPixels.resize(static_cast<std::size_t>(raw.width) * static_cast<std::size_t>(raw.height) * 4);
                        ok = Encoding::ConvertHdrRgba16fToRgba8(
                            raw.pixels.data(), raw.format, raw.width, raw.height, hdrConvertedPixels.data());
                        encodePixels = hdrConvertedPixels.data();
                    } else if (IsBgraFormat(raw.format)) {
                        Encoding::ConvertBgraToRgbaInPlace(raw.pixels.data(), raw.width, raw.height);
                    }

                    if (!ok) {
                        // Depth format this device negotiated isn't one
                        // ConvertDepthToGrayscaleRgba8() recognizes, or the color
                        // format isn't one ConvertHdrRgba16fToRgba8() recognizes -
                        // see PHASE3/this phase's own accepted, documented, narrow
                        // risk.
                        m_captureBridge.FailPendingRequest(FrameCaptureKind::NamedTexture, FrameCaptureFailureReason::TargetNotAvailable);
                    } else {
                        std::vector<std::uint8_t> png = Encoding::EncodeRgba8ToPng(encodePixels, raw.width, raw.height);
                        m_captureBridge.FulfillPendingRequest(FrameCaptureKind::NamedTexture,
                            CapturedPngImage{ std::move(png), raw.width, raw.height, framesSinceUpdate });
                    }
                }
            } else if (const std::optional<rg::DebugVolumeTextureSnapshot> volumeSnapshot =
                           m_renderGraph.DebugVolumeTextureSnapshotFor(requestedName)) {
                // network-impl-6 campaign, Phase 4
                // (task_manager/network-impl-6/PHASE4_NAMED_TEXTURE_ENDPOINT_VOLUME_BRANCH_WIRING.md)
                // - a name that isn't a registered 2D texture but IS a
                // registered volume texture now renders a fresh raymarch
                // thumbnail on demand instead of copying already-rendered
                // pixels (a volume texture has no "existing rendered 2D
                // contents" to copy - see PHASE0_MASTER_STRATEGY.md's Step 2).
                if (requestedChannel == DebugTextureChannel::Depth) {
                    // A volume texture has no depth-companion concept at all
                    // (see VolumeTarget.h) - the same positively-known,
                    // permanent-failure fast-fail (409) the 2D branch above
                    // uses for wantsDepth && !snapshot->hasDepth (see
                    // PHASE0_MASTER_STRATEGY.md's Locked Design Decision 6).
                    m_captureBridge.FailPendingRequest(FrameCaptureKind::NamedTexture, FrameCaptureFailureReason::TargetNotAvailable);
                } else {
                    // Computed BEFORE WaitForGpuIdle()/the render below, from
                    // the snapshot as it was at the moment this request was
                    // actually serviced - exactly mirroring the 2D branch's
                    // own identical reasoning above (Phase 2 deliberately did
                    // not add a second, volume-specific frame counter).
                    const std::uint64_t framesSinceUpdate =
                        m_renderGraph.CurrentDebugTextureFrameCounter() - volumeSnapshot->lastUpdatedFrameCounter;

                    m_renderer.WaitForGpuIdle();

                    const VolumeTexturePreviewRenderer::CapturedRawPixels raw =
                        m_volumeTexturePreviewRenderer.RenderPreview(m_renderer, volumeSnapshot->target, volumeSnapshot->state);

                    // raw.pixels is already tightly-packed RGBA8 (see Phase 3's
                    // own RenderPreview() doc comment) - no BGRA swizzle, no
                    // HDR conversion, no depth-to-grayscale conversion needed
                    // at all, unlike the 2D branch's several format-dependent
                    // branches above.
                    std::vector<std::uint8_t> png = Encoding::EncodeRgba8ToPng(raw.pixels.data(), raw.width, raw.height);
                    m_captureBridge.FulfillPendingRequest(FrameCaptureKind::NamedTexture,
                        CapturedPngImage{ std::move(png), raw.width, raw.height, framesSinceUpdate });
                }
            }
            // else: this name has never been registered as EITHER kind yet this
            // session - leave the request pending; either it starts rendering
            // within the bridge's existing fixed timeout (a later Run()
            // iteration's own check above then succeeds), or the caller
            // eventually gets HTTP 504 - exactly the same accepted "main thread
            // hasn't produced this yet" bucket network-impl-2's own
            // PHASE0_MASTER_STRATEGY.md already documents.
        }

        // network-impl-4 campaign, Phase 5
        // (task_manager/network-impl-4/PHASE5_HTTP_ENDPOINTS_GET_TEXTURE_AND_LIST_TEXTURES.md) -
        // GET /list_textures' only data source. Resolved to plain
        // PublishedTextureListEntry values HERE, never inside FrameCaptureBridge
        // itself - see that struct's own doc comment (FrameCaptureBridge.h) for
        // why. Cheap - see PHASE0_MASTER_STRATEGY.md's own Locked Design Decision
        // 8: O(declared textures), the same order of magnitude as
        // Renderer::GetMemoryResources()'s own existing "safe to call every frame"
        // per-frame snapshot copy.
        {
            const std::vector<rg::DebugTextureSnapshot> snapshots = m_renderGraph.ListDebugTextures();
            const std::uint64_t currentFrameCounter = m_renderGraph.CurrentDebugTextureFrameCounter();

            std::vector<PublishedTextureListEntry> published;
            published.reserve(snapshots.size());
            for (const rg::DebugTextureSnapshot& snap : snapshots) {
                PublishedTextureListEntry entry;
                entry.name = snap.name;
                entry.regime = ToDebugTextureRegimeString(snap.regime);
                entry.format = DebugTextureColorFormatName(snap.target.format);
                entry.width = snap.target.extent.width;
                entry.height = snap.target.extent.height;
                entry.hasDepth = snap.hasDepth;
                // Same subtraction PHASE0_MASTER_STRATEGY.md's own Locked Design
                // Decision 4 defines for /get_texture's single-entry case (Phase 4),
                // just computed here for EVERY known texture at once, once per
                // frame, rather than once per request.
                entry.framesSinceUpdate = currentFrameCounter - snap.lastUpdatedFrameCounter;
                published.push_back(std::move(entry));
            }
            m_captureBridge.PublishTextureList(std::move(published));
        }

        // Phase 5 (GPU memory usage over time) - see PHASE5_GPU_MEMORY_
        // HISTORY_STRATEGY_v2.md: one real GPU memory snapshot per
        // profiler frame, taken as late as possible in the frame (still
        // inside this BeginFrame()/EndFrame() bracket) so it reflects
        // every resource created/destroyed anywhere this frame, including
        // by IEditorLayer::BuildUI()'s own Inspector/Project-panel asset
        // loading above. Unconditional - not #if GTE_ENABLE_PROFILER/
        // GTE_ENABLE_EDITOR-gated, matching this same function's own
        // BeginFrame()/EndFrame()/SetGpuPassDrawStats() calls, none of
        // which are gated either (only GTE_PROFILE_SCOPE(...)'s own macro
        // body is compile-time-gated - see AGENTS.md, "Profiling").
        // Renderer::GetMemoryTotals() is O(1) and always meaningful (no
        // "didn't run this frame" concept, unlike a GpuPass's draw-call
        // count), so this is always GpuSampleStatus::Present.
        Profiling::FrameProfiler::Instance().SetMemorySnapshot(BuildMemorySnapshot(m_renderer.GetMemoryTotals()));

        Profiling::FrameProfiler::Instance().EndFrame();
    }

    return 0;
}

} // namespace gte
