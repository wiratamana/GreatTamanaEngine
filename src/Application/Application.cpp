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
// render-pass-3 campaign, PHASE2 (PHASE2_GPU_SKINNING_OPAQUE_BLACKBOARD_PROOF.md)
// - the "GpuSkinning" RenderPipeline provider (RegisterOffscreenRenderPipelineProviders()
// below) is a direct, literal translation of RenderPasses.cpp's own
// AddGpuSkinningPasses() loop body - needs the same two headers that file
// already includes for the exact same reason (ComputeGroupCount()/
// kSkinningLocalSizeX, GpuSkinningPipelines).
#include "../Renderer/ComputeDispatch.h"
#include "../Renderer/GpuSkinning/GpuSkinningPipelines.h"
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

// frame-debugger-1 campaign (task_manager/frame-debugger-1/
// PHASE0_MASTER_STRATEGY.md, Locked Design Decision #4) - the fixed,
// deterministic amount of simulated time a single Step (PHASE3/PHASE4)
// advances by, and also what a resume-from-pause frame is clamped to (see
// Time::Advance()'s own doc comment, Locked Design Decision #11). A plain
// 1/60s, never derived from real elapsed time.
constexpr double kFixedStepSeconds = 1.0 / 60.0;

// render-pass-3 campaign, PHASE2 (PHASE2_GPU_SKINNING_OPAQUE_BLACKBOARD_PROOF.md)
// - the ONE rg::RenderPassBlackboard key GPU Skinning's "GpuSkinning"
// provider Publish()es its resulting std::vector<rg::BufferHandle> under,
// and "RenderOpaque"'s own provider Fetch()es it back from - a real,
// concrete instance of PHASE1's generic cross-provider hand-off mechanism
// (RenderPassBlackboard), replacing the OLD hand-threaded
// `gpuSkinningOutputBuffers` parameter for THIS one call site only (see
// PHASE0_MASTER_STRATEGY.md's Locked Design Decision 3). `using
// rg::operator""_passId;` brings PHASE1's own consteval literal operator
// into scope (it lives in `namespace gte::rg`, not `gte` itself).
using rg::operator""_passId;
constexpr rg::RenderPassId kGpuSkinningOutputsKey = "GpuSkinning.OutputBuffers"_passId;

// render-pass-3 campaign, PHASE3 (PHASE3_FULL_PRODUCTION_PASS_MIGRATION_AND_VIEW_UNIFICATION.md,
// Step 3.3) - every remaining blackboard key this phase's own provider set
// needs, following the exact same "`using rg::operator""_passId;` + file-
// scope `constexpr rg::RenderPassId`" convention kGpuSkinningOutputsKey
// above already established.
//
// "AtmosphereSharedLut" publishes ONE AtmosphereSharedLutBlackboardEntry
// (this frame's already-folded-with-groundAlbedoTint AtmosphereParametersGpu
// PLUS the Transmittance/Multi-Scattering LUT handles) under this ONE key -
// every per-view Atmosphere-sequence provider below fetches it back.
constexpr rg::RenderPassId kAtmosphereSharedLutKey = "Atmosphere.SharedLuts"_passId;

// "AtmosphereViewLut" publishes its own per-view AtmosphereViewLutHandles
// (Sky-View LUT + Aerial Perspective volume handles + this view's own
// AtmosphereFrameUniforms) under ONE of these two keys, picked purely by
// which named view is currently being declared (Application already knows
// there are only ever "Game"/"Scene" real views - Locked Design Decision 1 -
// so two fixed, distinct compile-time keys are the simplest correct
// resolution the phase doc's own Step 3.3 table explicitly permits, rather
// than inventing a runtime-hashed-into-RenderPassId scheme for no real
// benefit at today's exactly-two-views scale).
constexpr rg::RenderPassId kAtmosphereViewLutGameKey = "Atmosphere.ViewLut.Game"_passId;
constexpr rg::RenderPassId kAtmosphereViewLutSceneKey = "Atmosphere.ViewLut.Scene"_passId;

// Step 3.7 - "DrawSkyBackground" republishes the exact
// std::function<void(VkCommandBuffer)> it just built (via
// MakeRecordSkyBackgroundCallback()) for its OWN execute lambda, but ONLY
// for the Game View, so Application::Run() can Fetch() it back out AFTER
// m_offscreenRenderPipeline.DeclareInto() returns, for
// AddFrameDebuggerReplayPasses()'s still-unmigrated call site (Locked Design
// Decision 4) - this is the "or the raw ingredients... whichever is
// simpler" option the phase doc's own Step 3.3 table explicitly allows.
constexpr rg::RenderPassId kGameSkyBackgroundCallbackKey = "Atmosphere.GameSkyBackgroundCallback"_passId;

// render-pass-3 campaign, PHASE3 (Step 3.3) - "AtmosphereSharedLut"'s own
// blackboard payload: bundles the Transmittance/Multi-Scattering LUT handles
// together with THIS frame's own AtmosphereParametersGpu (built fresh, once,
// inside that one provider, folding in m_atmosphereSettings.groundAlbedoTint
// exactly like Application::Run() used to do inline) - every per-view
// Atmosphere-sequence provider needs BOTH.
struct AtmosphereSharedLutBlackboardEntry {
    AtmosphereSharedLutHandles handles;
    AtmosphereParametersGpu parameters;
};


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
    // network-impl-7 campaign, Phase 3 - ALSO hands EditorUiCommandBridge's
    // address into NetworkServer's constructor (a third, appended defaulted
    // pointer parameter), for the exact same reason - m_uiCommandBridge is
    // likewise declared before m_networkServer.
    // task_manager/frame-debugger-3 campaign, PHASE7 - ALSO hands
    // FrameDebuggerCommandBridge's address into NetworkServer's constructor
    // (a fourth, appended defaulted pointer parameter), for the exact same
    // reason - m_frameDebuggerCommandBridge is likewise declared before
    // m_networkServer.
    // task_manager/stl-parser-2 campaign, PHASE1 - ALSO hands
    // AssetImportCommandBridge's address into NetworkServer's constructor
    // (a fifth, appended defaulted pointer parameter), for the exact same
    // reason - m_assetImportCommandBridge is likewise declared before
    // m_networkServer.
    , m_networkServer(&m_captureBridge, &m_commandBridge, &m_uiCommandBridge, &m_frameDebuggerCommandBridge,
          &m_assetImportCommandBridge)
    , m_windowWidth(width)
    , m_windowHeight(height)
{
    // render-pass-3 campaign, PHASE2/PHASE3 - registers both
    // m_offscreenRenderPipeline (every remaining production pass) and
    // m_presentRenderPipeline ("Present" alone) once, here, at construction
    // time - see each method's own definition below.
    RegisterOffscreenRenderPipelineProviders();
    RegisterPresentRenderPipelineProvider();

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

// render-pass-3 campaign, PHASE3 (Step 3.1) - a small, linear scan (the
// realistic view count is 0-2, so this is never worth a hashed lookup).
const RenderPassViewData* Application::FindViewData(rg::RenderViewId view) const noexcept
{
    for (const RenderPassViewData& viewData : m_currentViewDataThisFrame) {
        if (viewData.id == view) {
            return &viewData;
        }
    }
    return nullptr;
}

// render-pass-3 campaign, PHASE2/PHASE3 - registers every remaining
// production pass onto m_offscreenRenderPipeline. Registered in the SAME
// order the old code declared them (readability only - DeclareInto()'s own
// `.order`-based sort, plus each Atmosphere-wrapping provider's own
// immediate `frame.builder` calls, are what actually fix declaration order -
// see PHASE3_FULL_PRODUCTION_PASS_MIGRATION_AND_VIEW_UNIFICATION.md's own
// Step 3.3b for the full "why").
void Application::RegisterOffscreenRenderPipelineProviders()
{
    m_offscreenRenderPipeline.SetLegacyViewScopeTranslator(&TranslateLegacyViewScope);

    // "AtmosphereSharedLut" - ProviderScope::Once, BeforeEverything. A
    // deliberate EXCEPTION to "providers only append RenderPassDesc data"
    // (Step 3.3b): calls AddAtmosphereSharedLutPasses() DIRECTLY against
    // `frame.builder` (the real RenderGraphBuilder& this frame's graph is
    // being built against), declaring its real passes IMMEDIATELY rather
    // than deferring them - AddAtmosphereSharedLutPasses() itself calls
    // builder.AddRenderPass() twice, with a genuine data dependency between
    // the two calls (Multi-Scattering needs Transmittance's own just-
    // returned handle), which cannot be expressed as a single
    // RenderPassDesc.setup/.execute pair. Publishes the resulting handles +
    // this frame's own AtmosphereParametersGpu onto the blackboard for every
    // per-view Atmosphere-sequence provider below to fetch, and appends both
    // LUT handles to `frame.finalTextureOutputs` (PHASE1's own mechanism -
    // REPLACES today's manual `outputs.push_back(...)` calls).
    m_offscreenRenderPipeline.Register("AtmosphereSharedLut", rg::ProviderScope::Once,
        [this](const rg::RenderPassFrameContext& frame, std::vector<rg::RenderPassDesc>&) {
            AtmosphereSharedLutBlackboardEntry entry;
            entry.parameters = MakeDefaultEarthAtmosphereParameters();
            entry.parameters.groundAlbedo = entry.parameters.groundAlbedo * m_atmosphereSettings.groundAlbedoTint;
            entry.handles =
                AddAtmosphereSharedLutPasses(frame.builder, m_renderer, m_atmosphereLutRenderer, entry.parameters);

            frame.finalTextureOutputs.push_back(entry.handles.transmittanceLutHandle);
            frame.finalTextureOutputs.push_back(entry.handles.multiScatteringLutHandle);

            frame.blackboard.Publish<AtmosphereSharedLutBlackboardEntry>(kAtmosphereSharedLutKey, entry);
        });

    // "GpuSkinning" - ProviderScope::Once (invoked exactly once per
    // m_offscreenRenderPipeline.DeclareInto() call, regardless of how many
    // views are active this frame - GPU Skinning's compute dispatch is
    // view-independent). A direct, literal translation of
    // RenderPasses.cpp's own AddGpuSkinningPasses() loop body, EXCEPT: this
    // provider has no rg::RenderGraphBuilder& of its own to call
    // ImportBuffer() (a RenderPassProvider's signature deliberately has none
    // - see PHASE1_CORE_VOCABULARY_AND_BLACKBOARD.md) - m_gpuSkinningRequestsThisFrame/
    // m_gpuSkinningHandlesThisFrame are populated by Run() itself,
    // immediately BEFORE calling m_offscreenRenderPipeline.DeclareInto(),
    // using the SAME RenderGraphBuilder& this frame's graph is being built
    // against (see Run()'s own offscreen build lambda) - this is the
    // "resolve outside any provider, by the caller" resolution
    // PHASE2_GPU_SKINNING_OPAQUE_BLACKBOARD_PROOF.md's own Step 3.1
    // explicitly preferred over growing RenderGraphBuilder.h's own public
    // surface. render-pass-3 campaign, PHASE3 - now called UNCONDITIONALLY
    // every frame (DeclareInto() itself runs unconditionally now - see
    // Run()) - retires PHASE2's own "Deviation 2" fallback branch
    // (`if (gameTarget == nullptr) { AddGpuSkinningPasses(...) }`), which no
    // longer exists.
    m_offscreenRenderPipeline.Register("GpuSkinning", rg::ProviderScope::Once,
        [this](const rg::RenderPassFrameContext& frame, std::vector<rg::RenderPassDesc>& out) {
            GpuSkinningPipelines& pipelines = m_game.GetGpuSkinningPipelines();

            for (std::size_t i = 0; i < m_gpuSkinningRequestsThisFrame.size(); ++i) {
                const AnimationSystem::GpuSkinningDispatchRequest& request = m_gpuSkinningRequestsThisFrame[i];
                const rg::BufferHandle handle = m_gpuSkinningHandlesThisFrame[i];

                rg::RenderPassDesc desc;
                desc.debugName = request.name;
                desc.kind = rg::PassKind::Compute;
                desc.order = rg::RenderPassEvent::PreOpaques;
                desc.view = rg::RenderViewId::Shared();
                desc.legacyCategory = rg::RenderPassCategory::GpuSkinning;
                desc.setup = [handle](rg::RenderGraphBuilder::PassBuilder& pass) {
                    pass.WriteBuffer(handle, rg::ResourceAccess::ComputeShaderWrite);
                };
                desc.execute = [this, &pipelines, request](rg::PassContext& ctx) {
                    const ComputePipeline& pipeline =
                        request.textured ? pipelines.PositionNormalUvPipeline() : pipelines.PositionNormalPipeline();
                    const std::uint32_t vertexCount = request.vertexCount;

                    m_renderer.BeginGraphPassRecording(ctx.cmd, ctx.recordDraw);
                    m_renderer.Dispatch(pipeline, request.descriptorSet, &vertexCount, sizeof(vertexCount),
                        ComputeGroupCount(vertexCount, kSkinningLocalSizeX), 1, 1);
                    m_renderer.EndGraphPassRecording();
                };
                out.push_back(std::move(desc));
            }

            // Published EVERY time this provider runs, even when
            // m_gpuSkinningHandlesThisFrame is empty (no model needs GPU
            // skinning this frame, or CPU skinning mode is active) - see
            // this phase's own Definition of Done: "RenderOpaque" below
            // ALWAYS Fetch()es this same key, every frame, unconditionally,
            // so an empty publish is never "unused" (it IS read, it just
            // reads back an empty vector) - ReportUnusedPublishesIfAny()
            // never fires for this key in normal operation.
            frame.blackboard.Publish<std::vector<rg::BufferHandle>>(
                kGpuSkinningOutputsKey, m_gpuSkinningHandlesThisFrame);
        });

    // "AtmosphereViewLut" - ProviderScope::PerActiveView, PreOpaques. The
    // SECOND Atmosphere-wrapping EXCEPTION (Step 3.3b) - calls
    // AddAtmosphereViewLutPasses() (and, Game View only, the debug-slice
    // pass) directly against `frame.builder`, fetching the shared LUT
    // handles/parameters this SAME frame's "AtmosphereSharedLut" provider
    // already published (registration order above guarantees it already
    // ran). Publishes its own per-view AtmosphereViewLutHandles for the
    // Sky Background/Composite providers below to fetch for THIS SAME view.
    m_offscreenRenderPipeline.Register("AtmosphereViewLut", rg::ProviderScope::PerActiveView,
        [this](const rg::RenderPassFrameContext& frame, std::vector<rg::RenderPassDesc>&) {
            const RenderPassViewData* viewData = FindViewData(frame.currentView);
            if (viewData == nullptr) {
                return; // Defensive - every entry in frame.activeViews always has a matching RenderPassViewData.
            }

            const std::optional<AtmosphereSharedLutBlackboardEntry> sharedLuts =
                frame.blackboard.Fetch<AtmosphereSharedLutBlackboardEntry>(kAtmosphereSharedLutKey);
            if (!sharedLuts.has_value()) {
                return; // Defensive - "AtmosphereSharedLut" (registered above) always runs first and always publishes.
            }

            const bool isGameView = (frame.currentView == rg::RenderViewId::Named("Game"));
            const char* skyViewLutName = isGameView ? "AtmosphereSkyViewLut_GameView" : "AtmosphereSkyViewLut_SceneView";
            const char* aerialVolumeName =
                isGameView ? "AtmosphereAerialPerspectiveVolume_GameView" : "AtmosphereAerialPerspectiveVolume_SceneView";
            const rg::ViewScope legacyViewScope = isGameView ? rg::ViewScope::GameView : rg::ViewScope::SceneView;

            AtmosphereViewLutHandles viewLuts = AddAtmosphereViewLutPasses(frame.builder, m_renderer,
                m_atmosphereLutRenderer, m_game.GetRegistry(), sharedLuts->parameters, m_atmosphereSettings,
                sharedLuts->handles, viewData->eyeWorldPosition, viewData->viewProjection, skyViewLutName,
                aerialVolumeName, legacyViewScope);

            frame.finalTextureOutputs.push_back(viewLuts.skyViewLutHandle);
            // A VolumeTextureHandle can never go into finalTextureOutputs
            // (TextureHandle-only) - see
            // RenderGraphBuilder::KeepVolumeTextureOutput()'s own doc
            // comment for why this is a separate call.
            frame.builder.KeepVolumeTextureOutput(viewLuts.aerialPerspectiveVolumeHandle);

            // Phase 9 debug-visibility slice - Game View only (per its own
            // doc comment), unchanged from before this phase.
            if (isGameView) {
                const rg::TextureHandle debugSlice = m_atmosphereLutRenderer.AddAerialPerspectiveVolumeDebugSlicePass(
                    frame.builder, m_renderer, viewLuts.aerialPerspectiveVolumeHandle, aerialVolumeName,
                    static_cast<std::uint32_t>(m_atmosphereSettings.aerialPerspectiveDebugSliceIndex),
                    "AtmosphereAerialPerspectiveVolumeDebugSlice", rg::ViewScope::GameView);
                frame.finalTextureOutputs.push_back(debugSlice);
            }

            const rg::RenderPassId viewLutKey = isGameView ? kAtmosphereViewLutGameKey : kAtmosphereViewLutSceneKey;
            frame.blackboard.Publish<AtmosphereViewLutHandles>(viewLutKey, viewLuts);
        });

    // "RenderOpaque" - ProviderScope::PerActiveView. render-pass-3 campaign,
    // PHASE3 - GENERALIZED from PHASE2's own Game-View-only body to read
    // frame.currentView's own RenderPassViewData via FindViewData() instead
    // of a single captured gameViewTarget/aspect - this is the one place
    // Scene View's own opaque draw becomes a REAL, SEPARATE "RenderOpaque"-
    // shaped pass for the first time (see Step 3.4). gpuSkinningOutputBuffers
    // is still Fetch()'d from the blackboard, with ZERO direct knowledge of
    // the "GpuSkinning" provider above (unchanged from PHASE2).
    m_offscreenRenderPipeline.Register("RenderOpaque", rg::ProviderScope::PerActiveView,
        [this](const rg::RenderPassFrameContext& frame, std::vector<rg::RenderPassDesc>& out) {
            const std::vector<rg::BufferHandle> gpuSkinningBuffers =
                frame.blackboard.Fetch<std::vector<rg::BufferHandle>>(kGpuSkinningOutputsKey)
                    .value_or(std::vector<rg::BufferHandle>{});

            const RenderPassViewData* viewData = FindViewData(frame.currentView);
            if (viewData == nullptr) {
                return;
            }

            const rg::TextureHandle viewTarget = viewData->colorTarget;
            const float aspectWidthOverHeight = viewData->aspectWidthOverHeight;
            const bool isGameView = (frame.currentView == rg::RenderViewId::Named("Game"));
            // Game View drives through the ECS's own active Camera
            // (viewProjectionOverride == nullptr, exactly like
            // AddRenderOpaquePass() always did); Scene View bypasses ECS
            // camera resolution via the Editor's own independently-
            // orbitable camera (viewData->viewProjection), exactly like
            // AddSceneViewPass() always did. A real, non-null
            // frameDebuggerCapture is NEVER handed to Scene View (Locked
            // Design Decision/RenderPasses.h's own doc comment).
            const Mat4 viewProjectionOverride = viewData->viewProjection;
            FrameDebuggerCaptureContext* frameDebuggerCapture =
                isGameView ? m_currentFrameDebuggerCaptureForOffscreenPipeline : nullptr;

            rg::RenderPassDesc desc;
            desc.debugName = "RenderOpaque";
            desc.kind = rg::PassKind::Graphics;
            desc.order = rg::RenderPassEvent::Opaques;
            desc.view = frame.currentView;
            // Frame Debugger Pass-Ownership behavior must be BIT-FOR-BIT
            // IDENTICAL to the old direct AddRenderOpaquePass()/
            // AddSceneViewPass() calls - see this phase's own "What We Will
            // NOT Do". legacyCategory/drawKind default to General/DrawMesh
            // respectively (PHASE1's own RenderPassDesc defaults).
            desc.legacyCategory = rg::RenderPassCategory::General;
            desc.setup = [viewTarget, gpuSkinningBuffers](rg::RenderGraphBuilder::PassBuilder& pass) {
                pass.WriteColorAttachment(viewTarget, kGameClearColor);
                pass.WriteDepthStencilAttachment(viewTarget, kGameClearDepth);
                DeclareGpuSkinningReads(pass, gpuSkinningBuffers);
            };
            desc.execute = [this, aspectWidthOverHeight, isGameView, viewProjectionOverride, frameDebuggerCapture](
                                rg::PassContext& ctx) {
                m_renderer.BeginGraphPassRecording(ctx.cmd, ctx.recordDraw);
                if (isGameView) {
                    m_game.Render(m_renderer, aspectWidthOverHeight, nullptr, frameDebuggerCapture);
                } else {
                    m_game.Render(m_renderer, aspectWidthOverHeight, &viewProjectionOverride);
                }
                m_renderer.EndGraphPassRecording();
            };
            out.push_back(std::move(desc));
        });

    // "DrawSkyBackground" - ProviderScope::PerActiveView, AfterOpaques. Wraps
    // AddDrawSkyBackgroundPass()'s existing body (unchanged clear/no-clear
    // rules); fetches this view's own Sky-View LUT + frame uniforms from the
    // blackboard (published by "AtmosphereViewLut" above, for THIS SAME
    // view) to build its own recordSkyBackground callback via
    // MakeRecordSkyBackgroundCallback() - replacing today's manual capture.
    // Step 3.7 - ALSO republishes that exact callback for the Game View
    // ONLY, so Run() can hand it to AddFrameDebuggerReplayPasses() (still
    // unmigrated) after DeclareInto() returns.
    m_offscreenRenderPipeline.Register("DrawSkyBackground", rg::ProviderScope::PerActiveView,
        [this](const rg::RenderPassFrameContext& frame, std::vector<rg::RenderPassDesc>& out) {
            const RenderPassViewData* viewData = FindViewData(frame.currentView);
            if (viewData == nullptr) {
                return;
            }

            const bool isGameView = (frame.currentView == rg::RenderViewId::Named("Game"));
            const rg::RenderPassId viewLutKey = isGameView ? kAtmosphereViewLutGameKey : kAtmosphereViewLutSceneKey;
            const std::optional<AtmosphereViewLutHandles> viewLuts =
                frame.blackboard.Fetch<AtmosphereViewLutHandles>(viewLutKey);
            const std::optional<AtmosphereSharedLutBlackboardEntry> sharedLuts =
                frame.blackboard.Fetch<AtmosphereSharedLutBlackboardEntry>(kAtmosphereSharedLutKey);
            if (!viewLuts.has_value() || !sharedLuts.has_value()) {
                return; // Defensive - both are always published earlier this same DeclareInto() call.
            }

            const char* skyViewLutName = isGameView ? "AtmosphereSkyViewLut_GameView" : "AtmosphereSkyViewLut_SceneView";
            const std::function<void(VkCommandBuffer)> recordSkyBackground =
                MakeRecordSkyBackgroundCallback(m_atmosphereLutRenderer, m_renderer, viewData->viewProjection,
                    sharedLuts->parameters, viewLuts->frameUniforms, skyViewLutName, m_atmosphereSettings.skyExposure);

            if (isGameView) {
                frame.blackboard.Publish<std::function<void(VkCommandBuffer)>>(
                    kGameSkyBackgroundCallbackKey, recordSkyBackground);
            }

            // Mirrors AddDrawSkyBackgroundPass()'s own "add nothing when
            // nothing to do" rule - unreachable in practice today
            // (MakeRecordSkyBackgroundCallback() always returns a valid
            // callable), kept for parity/documentation.
            if (!recordSkyBackground) {
                return;
            }

            const rg::TextureHandle viewTarget = viewData->colorTarget;

            rg::RenderPassDesc desc;
            desc.debugName = "DrawSkyBackground";
            desc.kind = rg::PassKind::Graphics;
            desc.order = rg::RenderPassEvent::AfterOpaques;
            desc.view = frame.currentView;
            desc.legacyCategory = rg::RenderPassCategory::General;
            // Frame Debugger Pass-Ownership campaign (render-pass-2) -
            // matches AddDrawSkyBackgroundPass()'s own DrawQuad tag exactly
            // (a real, hand-verified full-screen-triangle draw).
            desc.drawKind = rg::RenderPassDrawKind::DrawQuad;
            desc.setup = [viewTarget](rg::RenderGraphBuilder::PassBuilder& pass) {
                // Deliberately NO clear value on either attachment
                // (VK_ATTACHMENT_LOAD_OP_LOAD) - this pass must never erase
                // "RenderOpaque"'s own just-written pixels/depth.
                pass.WriteColorAttachment(viewTarget);
                pass.WriteDepthStencilAttachment(viewTarget);
            };
            desc.execute = [this, recordSkyBackground](rg::PassContext& ctx) {
                m_renderer.BeginGraphPassRecording(ctx.cmd, ctx.recordDraw);
                recordSkyBackground(ctx.cmd);
                m_renderer.EndGraphPassRecording();
            };
            out.push_back(std::move(desc));
        });

    // "RenderTransparent" - ProviderScope::PerActiveView, Transparents. Wraps
    // AddRenderTransparentPass()'s existing (always-empty-today) body
    // unchanged for the real-transparent-geometry case. render-pass-3
    // campaign, PHASE3 (Step 3.4) - ALSO folds in the Editor's own ground-
    // grid overlay for Scene View ONLY (RenderPassViewData::recordSceneOverlay),
    // replacing the old AddSceneViewPass()'s own fused
    // "sky then grid, same pass" ordering with "DrawSkyBackground" (above,
    // AfterOpaques) declared strictly BEFORE this pass (Transparents) - the
    // exact same relative order AddSceneViewPass() always guaranteed, now
    // enforced by RenderPassEvent instead of same-pass-body sequencing.
    m_offscreenRenderPipeline.Register("RenderTransparent", rg::ProviderScope::PerActiveView,
        [this](const rg::RenderPassFrameContext& frame, std::vector<rg::RenderPassDesc>& out) {
            const RenderPassViewData* viewData = FindViewData(frame.currentView);
            if (viewData == nullptr) {
                return;
            }

            const std::vector<DrawCommand> transparentCommands =
                RenderSystem::CollectTransparentRenderables(m_game.GetRegistry());
            const bool hasOverlay = static_cast<bool>(viewData->recordSceneOverlay);

            // Real transparent-geometry draws: unreachable today (see
            // RenderSystem::CollectTransparentRenderables()'s own doc
            // comment) - left deliberately unimplemented beyond this check,
            // mirroring AddRenderTransparentPass()'s own scope exactly.
            if (transparentCommands.empty() && !hasOverlay) {
                return; // A true no-op, exactly like the old direct call.
            }

            if (!hasOverlay) {
                return; // transparentCommands-driven path is unreachable today - nothing further to declare.
            }

            const rg::TextureHandle viewTarget = viewData->colorTarget;
            const Mat4 viewProjection = viewData->viewProjection;
            const std::function<void(VkCommandBuffer, const Mat4&)> recordSceneOverlay = viewData->recordSceneOverlay;

            rg::RenderPassDesc desc;
            desc.debugName = "RenderTransparent";
            desc.kind = rg::PassKind::Graphics;
            desc.order = rg::RenderPassEvent::Transparents;
            desc.view = frame.currentView;
            desc.legacyCategory = rg::RenderPassCategory::General;
            desc.setup = [viewTarget](rg::RenderGraphBuilder::PassBuilder& pass) {
                pass.WriteColorAttachment(viewTarget);
                pass.WriteDepthStencilAttachment(viewTarget);
            };
            desc.execute = [this, recordSceneOverlay, viewProjection](rg::PassContext& ctx) {
                m_renderer.BeginGraphPassRecording(ctx.cmd, ctx.recordDraw);
                recordSceneOverlay(ctx.cmd, viewProjection);
                m_renderer.EndGraphPassRecording();
            };
            out.push_back(std::move(desc));
        });

    // "AtmosphereComposite" - ProviderScope::PerActiveView, AfterTransparents,
    // ProviderTiming::AfterDeferredPasses. The THIRD Atmosphere-wrapping
    // EXCEPTION (Step 3.3b) - calls AddAtmosphereCompositePass() directly
    // against `frame.builder`, fetching this view's own Aerial Perspective
    // volume handle + frame uniforms from the blackboard. Appends its own
    // output TextureHandle to `frame.finalTextureOutputs`.
    //
    // CORRECTNESS-CRITICAL, confirmed by live testing during this phase -
    // MUST be registered with ProviderTiming::AfterDeferredPasses (see that
    // enum's own doc comment, RenderPipeline.h): this pass READS the SAME
    // texture handle "RenderOpaque"/"DrawSkyBackground" WRITE, so it must be
    // declared STRICTLY AFTER them in the underlying pass list - registering
    // it with the default BeforeDeferredPasses timing (as an earlier,
    // untested draft of this phase did) causes RenderGraphCompiler::Compile()'s
    // own resource-versioning scan to see this pass's read BEFORE any writer
    // is known, silently CULLING "RenderOpaque"/"DrawSkyBackground" entirely
    // (confirmed live: Game/Scene View rendered solid white/black, and the
    // Render Graph panel showed "RenderOpaque" as "culled").
    m_offscreenRenderPipeline.Register("AtmosphereComposite", rg::ProviderScope::PerActiveView,
        [this](const rg::RenderPassFrameContext& frame, std::vector<rg::RenderPassDesc>&) {
            const RenderPassViewData* viewData = FindViewData(frame.currentView);
            if (viewData == nullptr || viewData->renderTexture == nullptr) {
                return;
            }

            const bool isGameView = (frame.currentView == rg::RenderViewId::Named("Game"));
            const rg::RenderPassId viewLutKey = isGameView ? kAtmosphereViewLutGameKey : kAtmosphereViewLutSceneKey;
            const std::optional<AtmosphereViewLutHandles> viewLuts =
                frame.blackboard.Fetch<AtmosphereViewLutHandles>(viewLutKey);
            if (!viewLuts.has_value()) {
                return;
            }

            const char* aerialVolumeName =
                isGameView ? "AtmosphereAerialPerspectiveVolume_GameView" : "AtmosphereAerialPerspectiveVolume_SceneView";
            const char* outputTextureName = isGameView ? "GameViewComposited" : "SceneViewComposited";
            const rg::ViewScope legacyViewScope = isGameView ? rg::ViewScope::GameView : rg::ViewScope::SceneView;

            const rg::TextureHandle composited = AddAtmosphereCompositePass(frame.builder, m_renderer,
                m_atmosphereLutRenderer, *viewData->renderTexture, viewData->colorTarget,
                viewLuts->aerialPerspectiveVolumeHandle, aerialVolumeName, viewLuts->frameUniforms,
                viewData->eyeWorldPosition, m_atmosphereSettings.aerialPerspectiveStrength,
                m_atmosphereSettings.aerialPerspectiveMaxDistanceKm, m_atmosphereSettings.aerialPerspectiveDepthExponent,
                viewData->renderTexture->Extent(), outputTextureName, legacyViewScope);

            frame.finalTextureOutputs.push_back(composited);
        },
        rg::ProviderTiming::AfterDeferredPasses);
}

// render-pass-3 campaign, PHASE3 (Step 3.5) - the swapchain regime's own
// RenderPipeline, with exactly one provider, "Present". A deliberate
// EXCEPTION to "providers only append RenderPassDesc data" (Step 3.3b, same
// mechanism as the Atmosphere-wrapping providers above) - calls
// AddGpuSkinningPasses()/AddPresentPass() directly against `frame.builder`,
// mirroring exactly what Application::Run()'s swapchain-regime `build` lambda
// used to do inline. NEVER reuses/fetches a rg::BufferHandle the OFFSCREEN
// regime's own "GpuSkinning" provider published to ITS OWN, separate
// blackboard this same frame (PHASE0_MASTER_STRATEGY.md's Locked Design
// Decision 6 and this phase doc's own Step 3.5 correctness note) - a fresh,
// direct-render-only AddGpuSkinningPasses() call is made here instead,
// exactly once, ONLY when this pass is also the one drawing Game directly.
void Application::RegisterPresentRenderPipelineProvider()
{
    m_presentRenderPipeline.SetLegacyViewScopeTranslator(&TranslateLegacyViewScope);

    m_presentRenderPipeline.Register("Present", rg::ProviderScope::Once,
        [this](const rg::RenderPassFrameContext& frame, std::vector<rg::RenderPassDesc>&) {
            const std::vector<rg::BufferHandle> gpuSkinningBuffers = m_needsDirectGameRenderThisFrame
                ? AddGpuSkinningPasses(frame.builder, m_game, m_renderer)
                : std::vector<rg::BufferHandle>{};

            AddPresentPass(frame.builder, m_game, m_renderer, m_swapchainImageThisFrame,
                m_directGameRenderAspectThisFrame, m_recordImGuiThisFrame, gpuSkinningBuffers);
        });
}


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

        // frame-debugger-1 campaign, PHASE4 - read the Editor's own Pause/Step
        // toolbar state (see PHASE3's IEditorLayer::IsPlaybackPaused()/
        // TryConsumeStepRequest()) and drive EngineContext::Time with it for
        // real. This reflects whatever the user last clicked as of the END of
        // last frame's BuildUI() call - the same one-frame-of-lag every other
        // Editor<->engine feedback loop in this file already accepts (see e.g.
        // GameViewTarget()'s own doc comment). Always false/false for a release
        // build (NullEditorLayer - see PHASE3), so Application behaves exactly
        // like before this whole campaign whenever GTE_ENABLE_EDITOR is OFF.
        const bool playbackPaused = m_editorLayer->IsPlaybackPaused();
        // Deliberately called EVERY frame, unconditionally (never short-circuited
        // by `playbackPaused &&`) so a stray/stale pending step request can never
        // linger un-cleared even in an edge case the toolbar's own "Step is
        // disabled while not paused" UI guard wasn't supposed to allow in the
        // first place - see IEditorLayer::TryConsumeStepRequest()'s own doc
        // comment.
        const bool stepRequestedRaw = m_editorLayer->TryConsumeStepRequest();
        const bool steppedThisFrame = playbackPaused && stepRequestedRaw;
        m_engineContext.time.Advance(deltaSeconds, playbackPaused, steppedThisFrame, kFixedStepSeconds);

        // task_manager/frame-debugger-3 campaign, PHASE3
        // (PHASE3_FRAME_HISTORY_RING_BUFFER_AND_CAPTURE_TRIGGER.md, Step
        // 3.2, call site 2) - records "a Step happened this frame" for the
        // Frame Debugger's own later use (its real capture, which needs
        // this frame's now-FINAL RenderGraphSnapshot/Game View pixels, only
        // actually runs later THIS SAME frame, from inside BuildUI() - see
        // IEditorLayer::NotifyFrameDebuggerStepConsumed()'s own doc
        // comment). Called right where TryConsumeStepRequest() above is
        // already checked - a no-op for NullEditorLayer.
        if (steppedThisFrame) {
            m_editorLayer->NotifyFrameDebuggerStepConsumed();
        }

        m_editorLayer->NewFrame();

        // network-impl-7 campaign - drains at most ONE pending
        // GET /activate_tab request per frame, IMMEDIATELY after
        // NewFrame() and BEFORE BuildUI() - this is the one window in the
        // frame where Dear ImGui's window/dock state is valid to touch
        // (NewFrame() already ran) AND where a change here is still
        // visible in THIS SAME frame's own tab rendering (BuildUI() has
        // not run yet - see PHASE3_APPLICATION_WIRING_AND_FRAME_LOOP_INTEGRATION.md's
        // own "why this exact frame position" reasoning for the full
        // justification). Mirrors EngineCommandBridge's own "drain as
        // early as possible" precedent above, just relative to a different
        // pair of per-frame calls.
        if (const std::optional<EditorUiCommandRequest> uiRequest = m_uiCommandBridge.TryPeekPendingCommandRequest()) {
            GTE_PROFILE_SCOPE("Application::ExecuteEditorUiCommand");
            EditorUiCommandResult uiResult;
            uiResult.kind = uiRequest->kind;
            // Only one EditorUiCommandKind exists today (ActivateTab) - a
            // future addition to this bridge (see EditorUiCommandBridge.h's
            // own doc comment on why it's an enum, not a single hardcoded
            // shape) would branch on uiRequest->kind here, the exact same
            // shape ExecuteEngineCommand() (EngineCommandDispatch.cpp)
            // already uses for ITS bridge's own multiple kinds.
            const TabActivationResult activation = m_editorLayer->ActivateTab(uiRequest->activateTab.tabName);
            uiResult.activateTab.tabExists = activation.tabExists;
            uiResult.activateTab.success = activation.tabExists;
            m_uiCommandBridge.FulfillCommand(uiResult);
        }

        // task_manager/frame-debugger-3 campaign, PHASE7
        // (PHASE7_NETWORK_HTTP_AUTOMATION_AND_MAIN_VIEWPORT_PINNING.md, Step
        // 3.3) - drains at most ONE pending Frame Debugger command per
        // frame, at the SAME point in the loop EditorUiCommandBridge's own
        // pump immediately above already runs (right after NewFrame() and
        // BEFORE BuildUI()) - see FrameDebuggerCommandBridge.h's own header
        // comment for why this is safe even for a command (CaptureNow)
        // whose real effect only fully "lands" later the SAME frame, inside
        // BuildUI()'s own FrameDebuggerPanel::Build() call: every
        // IEditorLayer::FrameDebugger*() method below is a cheap, direct
        // main-thread call (never itself blocking on anything), and
        // FrameDebuggerGetState() is read back AFTER dispatching whichever
        // command actually ran, so `fdResult.state` always reflects this
        // exact call's own real effect before FulfillCommand() unblocks the
        // waiting network thread.
        if (const std::optional<FrameDebuggerCommandRequest> fdRequest =
                m_frameDebuggerCommandBridge.TryPeekPendingCommandRequest()) {
            GTE_PROFILE_SCOPE("Application::ExecuteFrameDebuggerCommand");
            FrameDebuggerCommandResult fdResult;
            fdResult.kind = fdRequest->kind;
            switch (fdRequest->kind) {
            case FrameDebuggerCommandKind::OpenWindow:
                m_editorLayer->FrameDebuggerOpenWindow();
                fdResult.success = true;
                break;
            case FrameDebuggerCommandKind::SetEnabled:
                m_editorLayer->FrameDebuggerSetEnabled(fdRequest->setEnabled.enabled);
                fdResult.success = true;
                break;
            case FrameDebuggerCommandKind::CaptureNow:
                fdResult.success = m_editorLayer->FrameDebuggerCaptureNow();
                break;
            case FrameDebuggerCommandKind::SelectEvent:
                m_editorLayer->FrameDebuggerSelectEvent(fdRequest->selectEvent.index);
                fdResult.success = true;
                break;
            case FrameDebuggerCommandKind::SetChannel:
                fdResult.success = m_editorLayer->FrameDebuggerSetChannel(fdRequest->setChannel.channel);
                break;
            case FrameDebuggerCommandKind::SetLevels:
                m_editorLayer->FrameDebuggerSetLevels(fdRequest->setLevels.black, fdRequest->setLevels.white);
                fdResult.success = true;
                break;
            case FrameDebuggerCommandKind::GetState:
                fdResult.success = true;
                break;
            }

            const FrameDebuggerStateSnapshotView stateView = m_editorLayer->FrameDebuggerGetState();
            fdResult.state.enabled = stateView.enabled;
            fdResult.state.windowOpen = stateView.windowOpen;
            fdResult.state.hasCapturedFrame = stateView.hasCapturedFrame;
            fdResult.state.totalEventCount = stateView.totalEventCount;
            fdResult.state.selectedEventIndex = stateView.selectedEventIndex;
            fdResult.state.channel = stateView.channel;
            fdResult.state.levelsBlack = stateView.levelsBlack;
            fdResult.state.levelsWhite = stateView.levelsWhite;

            m_frameDebuggerCommandBridge.FulfillCommand(fdResult);
        }

        // task_manager/stl-parser-2, PHASE1 - drains at most ONE pending
        // /import_asset request per frame. Unlike EditorUiCommandBridge's own
        // pump above, this one does not need ImGui's context to be valid at all -
        // ImportExternalAssetIntoProject() only touches ProjectPanel's own
        // AssetDatabase/filesystem state, never ImGui - but it is kept at this
        // same point in the frame, appended after every other bridge's own drain
        // block, for locality with them.
        if (const std::optional<AssetImportCommandRequest> importRequest =
                m_assetImportCommandBridge.TryPeekPendingCommandRequest()) {
            GTE_PROFILE_SCOPE("Application::ExecuteAssetImportCommand");
            AssetImportCommandResult importResult;
            importResult.kind = importRequest->kind;
            // Only one AssetImportCommandKind exists today (ImportExternalFile) -
            // a future addition would branch on importRequest->kind here, the
            // same shape ExecuteEngineCommand() already uses for its own bridge.
            const ProjectAssetImportResult layerResult = m_editorLayer->ImportExternalAssetIntoProject(
                importRequest->importExternalFile.sourceAbsolutePath,
                importRequest->importExternalFile.destinationRelativeFolder);
            ImportExternalFileOutcome& outcome = importResult.importExternalFile;
            outcome.projectAvailable = layerResult.projectAvailable;
            outcome.success = layerResult.success;
            outcome.message = layerResult.message;
            outcome.finalRelativePath = layerResult.finalRelativePath;
            outcome.finalAbsolutePath = layerResult.finalAbsolutePath;
            outcome.guid = layerResult.guid;
            outcome.convertedToMeshAsset = layerResult.convertedToMeshAsset;
            outcome.meshSourceFormat = layerResult.meshSourceFormat;
            outcome.convertedToKtx2 = layerResult.convertedToKtx2;
            outcome.convertedToMotionAsset = layerResult.convertedToMotionAsset;
            outcome.meshVertexCount = layerResult.meshVertexCount;
            outcome.meshTriangleCount = layerResult.meshTriangleCount;
            m_assetImportCommandBridge.FulfillCommand(importResult);
        }


        // Clears last frame's queued Submit() draw items before Game gets a
        // chance to queue this frame's - see Renderer::BeginFrame().
        m_renderer.BeginFrame();

        // Game::Update() itself is already wrapped in
        // GTE_PROFILE_SCOPE("Game::Update") (see src/Game/Game.cpp) - not
        // wrapped again here too, since the flat, name-keyed CPU scope
        // aggregation model (see AGENTS.md, "Profiling") would otherwise
        // double-count identically-named nested scopes rather than
        // measuring the same call twice for no reason.
        m_game.Update(m_engineContext, inputState);

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

        // task_manager/frame-debugger-3 campaign, PHASE3
        // (PHASE3_FRAME_HISTORY_RING_BUFFER_AND_CAPTURE_TRIGGER.md, Step
        // 3.4) - decides whether the Frame Debugger's real capture context
        // is ARMED for THIS frame's Game-View render (nullptr the
        // overwhelmingly common case - see PHASE1's own "zero-overhead-
        // when-disarmed" requirement). Obtained here, BEFORE Game::Render()
        // ever runs this frame, and threaded straight into AddRenderOpaquePass()
        // below - see IEditorLayer::PrepareFrameDebuggerCaptureContext()'s
        // own doc comment. `frameDebuggerCapture` is a bare, forward-
        // declared pointer type (see EditorLayer.h) - this whole file never
        // dereferences it, so no #if GTE_ENABLE_EDITOR guard is needed here
        // (see task_manager/frame-debugger-3/
        // PHASE1_RENDERER_CAPTURE_INSTRUMENTATION.md's own Step 3.1b).
        FrameDebuggerCaptureContext* frameDebuggerCapture = m_editorLayer->PrepareFrameDebuggerCaptureContext();

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
                        // render-pass-3 campaign, PHASE3
                        // (PHASE3_FULL_PRODUCTION_PASS_MIGRATION_AND_VIEW_UNIFICATION.md,
                        // Step 3.6) - this whole build lambda collapses
                        // today's hand-duplicated `if (gameTarget != nullptr)
                        // { ...40+ lines... } if (sceneTarget != nullptr) {
                        // ...40+ lines... }` pair into ONE generic per-view
                        // loop, driven entirely by m_offscreenRenderPipeline's
                        // own registered providers
                        // (RegisterOffscreenRenderPipelineProviders()) - this
                        // lambda's own job shrinks to: (a) resolve GPU
                        // Skinning's dispatch requests (needs a real builder,
                        // unconditionally - see below), (b) build this
                        // frame's RenderPassViewData for whichever of
                        // Game/Scene View are actually visible, (c) call
                        // DeclareInto(), (d) service the two still-
                        // unmigrated call sites Locked Design Decision 4
                        // requires (AddFrameDebuggerReplayPasses()/Compute
                        // Blur Validation - Step 3.7).

                        // GPU Skinning's compute-dispatch requests are
                        // resolved HERE, UNCONDITIONALLY (regardless of which
                        // views are visible - an animated model must keep
                        // skinning even when only Scene View is open), using
                        // THIS SAME builder `b` (a RenderPassProvider has no
                        // RenderGraphBuilder& of its own to call
                        // ImportBuffer() - PHASE2's own "resolve outside any
                        // provider, by the caller" resolution). Retires
                        // PHASE2's own "Game-hidden/Scene-visible fallback"
                        // special case (Deviation 2 in that phase's own
                        // completion report) - "GpuSkinning" itself
                        // (ProviderScope::Once) is now invoked
                        // unconditionally by DeclareInto() below, every
                        // frame, so that old dual-code-path is no longer
                        // needed at all.
                        m_gpuSkinningRequestsThisFrame = m_game.CollectGpuSkinningDispatchRequests();
                        m_gpuSkinningHandlesThisFrame.clear();
                        m_gpuSkinningHandlesThisFrame.reserve(m_gpuSkinningRequestsThisFrame.size());
                        for (const AnimationSystem::GpuSkinningDispatchRequest& request :
                            m_gpuSkinningRequestsThisFrame) {
                            m_gpuSkinningHandlesThisFrame.push_back(
                                b.ImportBuffer(request.name, request.outputBuffer, request.outputBufferSize));
                        }

                        rg::RenderPassBlackboard blackboard;
                        blackboard.BeginFrame();
                        rg::RenderPassFrameContext frame{ {}, rg::RenderViewId::Shared(), blackboard, b, {}, {} };

                        m_currentViewDataThisFrame.clear();
                        m_currentFrameDebuggerCaptureForOffscreenPipeline = nullptr;
                        float gameAspectForReplay = 1.0f;

                        if (gameTarget != nullptr) {
                            const VkExtent2D extent = gameTarget->Extent();
                            const float aspect =
                                AspectRatioOf(static_cast<int>(extent.width), static_cast<int>(extent.height));

                            // Game View's own eye world position (active ECS
                            // Camera) and view-projection matrix.
                            const Vec3 gameEyeWorldPosition = ResolveActiveCameraWorldPosition(m_game.GetRegistry());
                            const Mat4 gameViewProjection =
                                RenderSystem::ResolveActiveCameraViewProjection(m_game.GetRegistry(), aspect);

                            const rg::TextureHandle h =
                                b.ImportTexture("GameView", gameTarget->Target(), VK_IMAGE_LAYOUT_UNDEFINED);

                            RenderPassViewData gameViewData;
                            gameViewData.id = rg::RenderViewId::Named("Game");
                            gameViewData.colorTarget = h;
                            gameViewData.renderTexture = gameTarget;
                            gameViewData.aspectWidthOverHeight = aspect;
                            gameViewData.viewProjection = gameViewProjection;
                            gameViewData.eyeWorldPosition = gameEyeWorldPosition;
                            m_currentViewDataThisFrame.push_back(gameViewData);

                            frame.activeViews.push_back(rg::RenderViewId::Named("Game"));
                            // Game-View-only - see RenderPasses.h's own
                            // AddRenderOpaquePass() doc comment on why a
                            // real, non-null capture pointer is NEVER handed
                            // to Scene View/Present.
                            m_currentFrameDebuggerCaptureForOffscreenPipeline = frameDebuggerCapture;
                            gameAspectForReplay = aspect;
                        }

                        rg::TextureHandle sceneColorHandleForBlurValidation{};
                        VkExtent2D sceneExtentForBlurValidation{};
                        bool sceneVisibleForBlurValidation = false;

                        if (sceneTarget != nullptr) {
                            const VkExtent2D extent = sceneTarget->Extent();
                            const float aspect =
                                AspectRatioOf(static_cast<int>(extent.width), static_cast<int>(extent.height));
                            // Unlike Game View above, Scene View renders
                            // through the Editor's OWN independently-
                            // orbitable camera (see src/Editor/EditorCamera.h)
                            // rather than whatever ECS entity currently has
                            // the active Camera component.
                            const Mat4 sceneViewProjection = m_editorLayer->SceneViewProjection(aspect);
                            const Vec3 sceneEyeWorldPosition = m_editorLayer->SceneViewCameraWorldPosition();

                            const rg::TextureHandle h =
                                b.ImportTexture("SceneView", sceneTarget->Target(), VK_IMAGE_LAYOUT_UNDEFINED);

                            RenderPassViewData sceneViewData;
                            sceneViewData.id = rg::RenderViewId::Named("Scene");
                            sceneViewData.colorTarget = h;
                            sceneViewData.renderTexture = sceneTarget;
                            sceneViewData.aspectWidthOverHeight = aspect;
                            sceneViewData.viewProjection = sceneViewProjection;
                            sceneViewData.eyeWorldPosition = sceneEyeWorldPosition;
                            // The Editor's infinite ground grid (see
                            // task_manager/editor-enchancements-1/PHASE0_MASTER_STRATEGY.md)
                            // - a plain std::function keeps
                            // RegisterOffscreenRenderPipelineProviders()'s
                            // own "RenderTransparent" provider body
                            // completely Editor-agnostic (see AGENTS.md,
                            // Clean Architecture); only THIS call site
                            // (which already legitimately holds
                            // m_editorLayer) knows the real callback reaches
                            // into IEditorLayer::RenderSceneGrid().
                            sceneViewData.recordSceneOverlay = [this](VkCommandBuffer cmd, const Mat4& viewProj) {
                                m_editorLayer->RenderSceneGrid(m_renderer, cmd, viewProj);
                            };
                            m_currentViewDataThisFrame.push_back(sceneViewData);

                            frame.activeViews.push_back(rg::RenderViewId::Named("Scene"));

                            // Phase 7 of the compute-shader campaign - the
                            // Compute Blur Validation debug tool's own
                            // still-unmigrated call site (Locked Design
                            // Decision 4) needs Scene's raw imported color
                            // handle + extent, resolved here, BEFORE
                            // DeclareInto() runs (this is the SAME handle
                            // `h` the now-migrated "RenderOpaque" provider
                            // will also write this frame).
                            sceneColorHandleForBlurValidation = h;
                            sceneExtentForBlurValidation = extent;
                            sceneVisibleForBlurValidation = true;
                        }

                        m_offscreenRenderPipeline.DeclareInto(b, frame);

                        // Step 3.7 - fetch this key UNCONDITIONALLY, right
                        // here (even on the overwhelming majority of frames
                        // where no Frame Debugger replay actually ends up
                        // being serviced below) - "DrawSkyBackground"
                        // (Game View only) publishes it EVERY frame Game
                        // View is active, but it is only genuinely CONSUMED
                        // on the rare frame a replay capture is serviced.
                        // Fetching it unconditionally here (regardless of
                        // whether the "if" block below ends up using it)
                        // marks the slot as fetched, so
                        // ReportUnusedPublishesIfAny() below never flags a
                        // legitimate "not every frame needs this" hand-off
                        // as a dangling one - confirmed via a live run of
                        // this exact phase's own build catching the
                        // spurious "never fetched this frame" log spam
                        // before this fix.
                        const std::optional<std::function<void(VkCommandBuffer)>> gameSkyBackgroundCallbackForReplay =
                            blackboard.Fetch<std::function<void(VkCommandBuffer)>>(kGameSkyBackgroundCallbackKey);
#ifndef NDEBUG
                        blackboard.ReportUnusedPublishesIfAny();
#endif

                        std::vector<rg::TextureHandle> outputs = std::move(frame.finalTextureOutputs);

                        // task_manager/frame-debugger-7 campaign, PHASE3
                        // (PHASE3_UNIFIED_STEP_TIMELINE_AND_PER_DRAW_REPLAY_RENDERING.md,
                        // Step 3.5) - render-pass-3 campaign, PHASE3 (Step
                        // 3.7) - AddFrameDebuggerReplayPasses()'s existing
                        // call site (Locked Design Decision 4 - kept
                        // UNMIGRATED, called directly, exactly as before)
                        // moves to HERE, AFTER DeclareInto() returns -
                        // `blackboard`/`frame` are still ordinary Run()
                        // locals, still in scope, so this Fetch()es the
                        // Game View's own GPU Skinning buffers/sky-
                        // background callback the now-migrated
                        // "GpuSkinning"/"DrawSkyBackground" providers
                        // already published onto THIS SAME frame's
                        // blackboard - this also naturally satisfies the
                        // pre-existing ordering requirement (replay passes
                        // declared AFTER "RenderOpaque"/"DrawSkyBackground"/
                        // "RenderTransparent") for free, since DeclareInto()'s
                        // own final, sorted loop has already fully returned.
                        if (gameTarget != nullptr && frameDebuggerCapture != nullptr
                            && m_editorLayer->ConsumePendingFrameDebuggerReplayRequest()) {
                            const std::vector<rg::BufferHandle> gpuSkinningBuffersForReplay =
                                blackboard.Fetch<std::vector<rg::BufferHandle>>(kGpuSkinningOutputsKey)
                                    .value_or(std::vector<rg::BufferHandle>{});
                            const std::function<void(VkCommandBuffer)> recordGameSkyBackground =
                                gameSkyBackgroundCallbackForReplay.value_or(std::function<void(VkCommandBuffer)>{});
                            const std::size_t objectCount = m_game.CountGameViewDrawCommandsThisFrame();
                            // IMPORTANT, CORRECTNESS-CRITICAL (see
                            // RenderPasses.h's own updated doc comment on
                            // AddFrameDebuggerReplayPasses()) - every
                            // returned destination TextureHandle MUST be
                            // appended to `outputs`, or
                            // RenderGraphCompiler::Compile()'s own
                            // backward-reachability culling scan silently
                            // culls every one of these N passes.
                            const std::vector<rg::TextureHandle> replayStepHandles =
                                AddFrameDebuggerReplayPasses(b, m_game, m_renderer, gameAspectForReplay, objectCount,
                                    gpuSkinningBuffersForReplay, recordGameSkyBackground, *gameTarget,
                                    *frameDebuggerCapture);
                            for (const rg::TextureHandle& replayHandle : replayStepHandles) {
                                outputs.push_back(replayHandle);
                            }
                        }

                        // Phase 7 of the compute-shader campaign
                        // (COMPUTE_PHASE7_VALIDATION_TESTING_TOOLING_STRATEGY_v2.md)
                        // - the texture-side validation workload's own
                        // still-unmigrated call site (Locked Design Decision
                        // 4 - never touched by this campaign). Declared (and
                        // this handle added to `outputs`) only when the
                        // Editor's own "Show Compute Blur (debug)" toggle is
                        // on and "Scene" is visible; std::nullopt (always the
                        // case for NullEditorLayer) means nothing was
                        // declared at all this call.
                        if (sceneVisibleForBlurValidation) {
                            if (const std::optional<rg::TextureHandle> blurHandle = m_editorLayer->AddBlurValidationPass(
                                    b, m_renderer, sceneColorHandleForBlurValidation, sceneExtentForBlurValidation)) {
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
        // calls, which aren't gated either. "RenderOpaque"/"DrawSkyBackground"/
        // "RenderTransparent" must match RenderPasses.cpp's own pass name
        // literals exactly.
        //
        // Render Pass campaign, PHASE2 - Profiling::GpuPass::GameView used to
        // read a single "GameView" pass's own LastKnownStatsFor() before this
        // campaign split it into three real, separate passes
        // ("RenderOpaque"/"DrawSkyBackground"/"RenderTransparent" - see
        // RenderPasses.h). CombinePassGpuStats() (RenderGraphSnapshot.h) sums
        // all three passes' own stats into one aggregate before handing it to
        // SetGpuPassDrawStats()/SetGpuPassTiming(), so the sky draw's own
        // real vkCmdDraw() call is now correctly counted too - previously
        // undercounted by exactly one draw call (see
        // docs/conventions/frame-debugger.md's frame-debugger-8 section).
        // "RenderTransparent" contributes nothing today (it is a genuine
        // no-op - LastKnownStatsFor() safely returns a default, all-zero
        // PassGpuStats{} for a pass name that was never declared this frame),
        // but is included here so this call site needs no further changes
        // once a future transparency campaign gives it real stats.
        //
        // render-pass-3 campaign, PHASE3 - KNOWN, DOCUMENTED LIMITATION:
        // Scene View's own opaque/sky/transparent passes are now ALSO real,
        // separate PassRecords named "RenderOpaque"/"DrawSkyBackground"/
        // "RenderTransparent" (Step 3.4 - the same literal names Game View's
        // own copies use, by this phase's own explicit design). `RenderGraph::
        // LastKnownStatsFor()`/its own private `m_lastKnownStats` table is
        // keyed PURELY by pass NAME (RenderGraph.cpp's `UpdateDrawStatsFor()`/
        // `UpdateTimingFor()`), with NO ViewScope disambiguation - a real,
        // pre-existing limitation of that mechanism that simply never
        // mattered before this phase, since no two passes ever shared an
        // identical literal name within one frame until now. The practical
        // effect: whichever of Game/Scene View's own same-named pass EXECUTES
        // LAST this frame (Scene's, given this phase's own per-view provider
        // loop order) overwrites the other's entry, so
        // `Profiling::GpuPass::GameView`'s draw-call/triangle-count/timing
        // numbers below will silently read as SCENE View's own numbers
        // whenever BOTH panels are visible simultaneously (a Profiler-panel
        // display-only cosmetic issue - never a rendering-correctness one).
        // Fixing this for real would mean teaching RenderGraph.cpp's own
        // by-name lookup to also consider ViewScope, a genuine RenderGraph.cpp
        // core change deliberately out of scope for this already-heaviest
        // migration phase - flagged explicitly here and in this phase's own
        // completion report as a known follow-up for a future phase/campaign.
        if (gameTarget != nullptr) {
            const rg::PassGpuStats gameViewStats = rg::CombinePassGpuStats({
                m_renderGraph.LastKnownStatsFor("RenderOpaque"),
                m_renderGraph.LastKnownStatsFor("DrawSkyBackground"),
                m_renderGraph.LastKnownStatsFor("RenderTransparent"),
            });
            Profiling::FrameProfiler::Instance().SetGpuPassDrawStats(Profiling::GpuPass::GameView,
                Profiling::GpuSampleStatus::Present, gameViewStats.drawStats.drawCallCount,
                gameViewStats.drawStats.triangleCount);
            Profiling::FrameProfiler::Instance().SetGpuPassTiming(Profiling::GpuPass::GameView,
                ToProfilingGpuSampleStatus(gameViewStats.timing.status), gameViewStats.timing.milliseconds);
        }
        if (sceneTarget != nullptr) {
            // render-pass-3 campaign, PHASE3 - "SceneView" (the old, single,
            // fused pass) no longer exists (Step 3.4) - Scene's own stats are
            // now aggregated the SAME way Game View's are, from its own
            // (same-named, see the KNOWN LIMITATION note above)
            // "RenderOpaque"/"DrawSkyBackground"/"RenderTransparent" passes.
            const rg::PassGpuStats sceneViewStats = rg::CombinePassGpuStats({
                m_renderGraph.LastKnownStatsFor("RenderOpaque"),
                m_renderGraph.LastKnownStatsFor("DrawSkyBackground"),
                m_renderGraph.LastKnownStatsFor("RenderTransparent"),
            });
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
                        // render-pass-3 campaign, PHASE3 (Step 3.5) -
                        // "Present" now goes through m_presentRenderPipeline
                        // instead of a direct AddGpuSkinningPasses()/
                        // AddPresentPass() call pair - populate the small set
                        // of per-frame members its ONE registered provider
                        // reads (RegisterPresentRenderPipelineProvider()),
                        // then drive its own SEPARATE
                        // RenderPassBlackboard/RenderPassFrameContext -
                        // NEVER shared with the offscreen regime's own
                        // blackboard/frame context this same frame (Locked
                        // Design Decision 6).
                        m_needsDirectGameRenderThisFrame = needsDirectGameRender;
                        m_directGameRenderAspectThisFrame = directGameRenderAspect;
                        m_swapchainImageThisFrame = swapchainImage;
                        m_recordImGuiThisFrame = [this](VkCommandBuffer cmd) { m_editorLayer->Render(cmd); };

                        rg::RenderPassBlackboard presentBlackboard;
                        presentBlackboard.BeginFrame();
                        rg::RenderPassFrameContext presentFrame{
                            {}, rg::RenderViewId::Shared(), presentBlackboard, b, {}, {}
                        };
                        m_presentRenderPipeline.DeclareInto(b, presentFrame);
#ifndef NDEBUG
                        presentBlackboard.ReportUnusedPublishesIfAny();
#endif
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

                    // atmosphere-scattering-2 campaign, Phase 4
                    // (task_manager/atmosphere-scattering-2/PHASE4_ATMOSPHERE_AWARE_VOLUME_DEBUG_PREVIEW.md)
                    // - auto-detect the Aerial Perspective volume by name so its
                    // preview uses the atmosphere-aware transmittance/in-scattering
                    // interpretation instead of the generic density/color one -
                    // zero new HTTP endpoint/query parameter (see that phase's own
                    // Locked Design Decision 6). Every other volume texture name
                    // still resolves to GenericDensityInAlpha, byte-for-byte the
                    // same behavior network-impl-6 already shipped.
                    //
                    // frame-debugger-5 campaign, PHASE4
                    // (task_manager/frame-debugger-5/PHASE4_VOLUME_TEXTURE_RAYMARCH_PREVIEW_REUSE.md)
                    // - this rule is now a shared, named, pure function
                    // (VolumeTexturePreviewRenderer.h's SelectVolumeTexturePreviewInterpretation())
                    // rather than inlined here, since FrameDebuggerCurrentCapture::CaptureFrame()
                    // now needs the exact same rule for a second real call site - byte-for-byte
                    // unchanged behavior for this call site.
                    const VolumeTexturePreviewInterpretation interpretation =
                        SelectVolumeTexturePreviewInterpretation(requestedName);

                    const VolumeTexturePreviewRenderer::CapturedRawPixels raw = m_volumeTexturePreviewRenderer.RenderPreview(
                        m_renderer, volumeSnapshot->target, volumeSnapshot->state, interpretation);

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
                entry.kind = "texture2d"; // network-impl-6 campaign, Phase 5 - explicit at every call site, see PublishedTextureListEntry's own doc comment.
                published.push_back(std::move(entry));
            }

            // network-impl-6 campaign, Phase 5
            // (task_manager/network-impl-6/PHASE5_LIST_TEXTURES_VOLUME_SURFACING.md) -
            // every registered VOLUME texture also gets its own
            // PublishedTextureListEntry, appended right after every 2D one, so
            // GET /list_textures surfaces both kinds side by side in one flat
            // array - this is what lets an LLM/AI caller discover a
            // texture_name worth calling GET /get_texture with, without
            // already knowing the Atmosphere feature's internal naming
            // convention.
            for (const rg::DebugVolumeTextureSnapshot& vol : m_renderGraph.ListDebugVolumeTextures()) {
                PublishedTextureListEntry entry;
                entry.name = vol.name;
                entry.regime = ToDebugTextureRegimeString(vol.regime);
                entry.format = DebugTextureColorFormatName(vol.target.format);
                entry.width = vol.target.extent.width;
                entry.height = vol.target.extent.height;
                entry.hasDepth = false; // A volume texture has no depth-companion concept at all - see VolumeTarget.h.
                entry.framesSinceUpdate = currentFrameCounter - vol.lastUpdatedFrameCounter;
                entry.kind = "texture3d";
                entry.depth = vol.target.extent.depth;
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
