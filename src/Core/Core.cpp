#include "Core.h"

// PHASE0_MASTER_STRATEGY.md's Locked Design Decision #8 - the ONE gte_core
// .cpp file allowed to #include the real src/Editor/EditorLayer.h header
// (Core.h itself only ever forward-declares IEditorLayer).
#include "../Editor/EditorLayer.h"

// editor-core-separation-1 campaign, PHASE13
// (PHASE13_CORE_FRAME_ORCHESTRATION_EXTRACTION.md) - every one of these was
// already an unconditional, gte_core-safe #include in Application.cpp before
// this phase (RenderPasses.h/AtmospherePassSequence.h/RenderPassViewData.h
// live under src/Application/, but that whole folder already sits inside the
// gte_core CMake target's own source list - see the design doc's Section 2.2
// inventory - so no file physically moved for this phase; only WHICH
// TRANSLATION UNIT includes them changed).
#include "../Application/AtmospherePassSequence.h"
#include "../Application/RenderPasses.h"
#include "../Profiling/FrameProfiler.h"
#include "../Profiling/ScopeTimer.h"
#include "../Renderer/Atmosphere/AtmosphereParameters.h"
#include "../Renderer/ComputeDispatch.h"
#include "../Renderer/GpuSkinning/GpuSkinningPipelines.h"
#include "../Renderer/GpuSkinning/GpuSkinningRenderPassTags.h"
#include "../Renderer/Culling/CullingPipelines.h"
#include "../Renderer/RenderGraph/RenderGraphBarrierPlanner.h"
#include "../Renderer/RenderGraph/RenderGraphBuilder.h"
#include "../Renderer/RenderGraph/RenderGraphDebugTextureRegistry.h"

// editor-core-separation-6 campaign, PHASE2
// (PHASE2_PLUGIN_CAPABILITY_ORCHESTRATOR_REGISTRY_AND_RENDER_FEATURE_MIGRATION.md)
// - the new IPluginCapabilityOrchestrator registry, plus its first real
// implementation, LegacyRenderFeatureOrchestrator (a verbatim relocation of
// the former inline IRenderFeatureModule_v1 loop/warning that used to live
// directly in this file - see Core::RegisterBuiltinCapabilityOrchestrators()/
// Core::LoadPlugins()/the "PluginRenderFeatures" provider below). Neither
// IRenderFeatureModule.h/IPluginRenderPassBuilder.h/PluginRenderPassBuilderAdapter.h/
// PluginRenderFeatureDiagnostics.h/Logging.h is needed directly by this
// translation unit anymore - that real logic (and those includes) now live
// inside LegacyRenderFeatureOrchestrator.cpp itself.
#include "Plugins/IPluginCapabilityOrchestrator.h"
#include "Plugins/LegacyRenderFeatureOrchestrator.h"
// editor-core-separation-6 campaign, PHASE3
// (PHASE3_EDITOR_PANEL_ORCHESTRATOR_MIGRATION.md) - the second real
// IPluginCapabilityOrchestrator implementation, proving the registry
// generalizes beyond render features (a verbatim relocation of
// EditorHost.cpp's own former inline IEditorPanelModule_v1 discovery loop).
#include "Plugins/EditorPanelCapabilityOrchestrator.h"
// editor-core-separation-6 campaign, PHASE4
// (PHASE4_RENDER_FEATURE_COMPOSITOR_CORE_AND_ORDERING.md) - the third real
// IPluginCapabilityOrchestrator implementation, the real `_v2` render-feature
// compositing pipeline (PHASE0_MASTER_STRATEGY.md's whole reason to exist).
#include "Plugins/RenderFeatureCompositor.h"

#include <cassert>
#include <cstdint>
#include <cstdio>
#include <deque>
#include <stdexcept>

namespace gte {

namespace {

// Relocated verbatim from Application.cpp's own former anonymous namespace
// (editor-core-separation-1 campaign, PHASE13) - see each helper's own doc
// comment, unchanged.

// Aspect ratio (width / height) of a render target - see RenderSystem::Draw()/
// Game::Render(). Falls back to a square (1.0f) for a degenerate/zero-height
// extent rather than dividing by zero.
float AspectRatioOf(int width, int height) noexcept
{
    return height > 0 ? static_cast<float>(width) / static_cast<float>(height) : 1.0f;
}

// frame-debugger-1 campaign (task_manager/frame-debugger-1/
// PHASE0_MASTER_STRATEGY.md, Locked Design Decision #4) - the fixed,
// deterministic amount of simulated time a single Step advances by, and also
// what a resume-from-pause frame is clamped to (see Time::Advance()'s own
// doc comment, Locked Design Decision #11). A plain 1/60s, never derived
// from real elapsed time.
constexpr double kFixedStepSeconds = 1.0 / 60.0;

// render-pass-3 campaign, PHASE2 - the ONE rg::RenderPassBlackboard key GPU
// Skinning's "GpuSkinning" provider Publish()es its resulting
// std::vector<rg::BufferHandle> under, and "RenderOpaque"'s own provider
// Fetch()es it back from.
using rg::operator""_passId;
constexpr rg::RenderPassId kGpuSkinningOutputsKey = "GpuSkinning.OutputBuffers"_passId;

// render-pass-3 campaign, PHASE3 (Step 3.3) - every remaining blackboard key
// this phase's own provider set needs.
constexpr rg::RenderPassId kAtmosphereSharedLutKey = "Atmosphere.SharedLuts"_passId;
constexpr rg::RenderPassId kAtmosphereViewLutGameKey = "Atmosphere.ViewLut.Game"_passId;
constexpr rg::RenderPassId kAtmosphereViewLutSceneKey = "Atmosphere.ViewLut.Scene"_passId;
constexpr rg::RenderPassId kGameSkyBackgroundCallbackKey = "Atmosphere.GameSkyBackgroundCallback"_passId;

// editor-core-separation-3 campaign, PHASE3
// (PHASE3_RUNTIME_RENDER_FEATURE_CAPABILITY.md) - REAL, LIVE-TESTING-
// DISCOVERED addition (see ask_questions round-trip in this phase's own
// completion report): "PluginRenderFeatures" must draw on top of the TRUE
// final, POST-atmosphere-composite image ("GameViewComposited"/
// "SceneViewComposited" - the one GET /get_game_view/the "Game" panel
// actually display, per FrameDebuggerHistory.h's own doc comment on
// compositedPreview), never the raw pre-composite "GameView"/"SceneView"
// handle alone - a plugin pass writing only the latter is invisible in the
// actually-displayed image, since nothing re-composites after it runs this
// same frame. "AtmosphereComposite" publishes its own freshly-computed
// composited TextureHandle under one of these two keys (mirroring
// kAtmosphereViewLutGameKey/kAtmosphereViewLutSceneKey's own per-view-key
// precedent immediately above) so "PluginRenderFeatures" can fetch it back.
constexpr rg::RenderPassId kGameCompositedOutputKey = "Atmosphere.CompositedOutput.Game"_passId;
constexpr rg::RenderPassId kSceneCompositedOutputKey = "Atmosphere.CompositedOutput.Scene"_passId;

// render-pass-3 campaign, PHASE3 (Step 3.3) - "AtmosphereSharedLut"'s own
// blackboard payload.
struct AtmosphereSharedLutBlackboardEntry {
    AtmosphereSharedLutHandles handles;
    AtmosphereParametersGpu parameters;
};

// Phase 4C - the one, tiny bridge from Renderer's own (Profiling-free)
// GpuTimingSample::Status into Profiling::GpuSampleStatus.
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

// GPU-Driven Frustum Culling + Indirect Draw campaign (render-pass-5),
// PHASE5 - stable (whole-process-lifetime) per-batch pass/resource names,
// keyed by GpuDrivenBatchKey - mirrors RenderPasses.cpp's own
// ReplayStepPassNamePool() precedent exactly.
struct GpuDrivenBatchNames {
    const char* resetPassName = nullptr;
    const char* cullingPassName = nullptr;
    const char* indirectDrawPassName = nullptr;
    const char* inputBufferName = nullptr;
    const char* indirectBufferName = nullptr;
    const char* countBufferName = nullptr;
    const char* displayName = nullptr;
};

class GpuDrivenBatchNamePool {
public:
    const GpuDrivenBatchNames& NamesFor(const GpuDrivenBatchKey& key)
    {
        for (const Entry& entry : m_entries) {
            if (entry.key == key) {
                return entry.names;
            }
        }

        const std::size_t index = m_entries.size();
        Entry entry;
        entry.key = key;
        entry.names.resetPassName = Intern("GpuDrivenBatch" + std::to_string(index) + " ResetCount");
        entry.names.cullingPassName = Intern("GpuDrivenBatch" + std::to_string(index) + " Culling");
        entry.names.indirectDrawPassName = Intern("GpuDrivenBatch" + std::to_string(index) + " IndirectDraw");
        entry.names.inputBufferName = Intern("GpuDrivenBatch" + std::to_string(index) + ".Input");
        entry.names.indirectBufferName = Intern("GpuDrivenBatch" + std::to_string(index) + ".IndirectCommands");
        entry.names.countBufferName = Intern("GpuDrivenBatch" + std::to_string(index) + ".VisibleCount");
        entry.names.displayName = Intern("GpuDrivenBatch" + std::to_string(index));
        m_entries.push_back(std::move(entry));
        return m_entries.back().names;
    }

private:
    const char* Intern(std::string s)
    {
        m_storage.push_back(std::move(s));
        return m_storage.back().c_str();
    }

    struct Entry {
        GpuDrivenBatchKey key;
        GpuDrivenBatchNames names;
    };

    std::deque<std::string> m_storage; // Never reallocates an already-handed-out c_str() pointer.
    std::vector<Entry> m_entries;
};

// A function-local static, mirroring RenderPasses.cpp's own
// ReplayStepPassNamePool() precedent - whole-process-lifetime, never reset.
GpuDrivenBatchNamePool& BatchNamePool()
{
    static GpuDrivenBatchNamePool pool;
    return pool;
}

} // namespace

Core::Core(ISurfaceProvider& surfaceProvider, IHostServices& hostServices)
    : m_renderer(surfaceProvider)
    , m_renderGraph(m_renderer)
    , m_game()
    , m_engineContext()
    // editor-core-separation-1 campaign, PHASE13 - seeded from
    // surfaceProvider's own CONSTRUCTION-time size (correct at this exact
    // moment - no resize has happened yet) - see NotifyWindowResized()'s own
    // doc comment (Core.h).
    , m_windowWidth(surfaceProvider.Width())
    , m_windowHeight(surfaceProvider.Height())
{
    // editor-core-separation-1 campaign, PHASE12 - hostServices is accepted
    // (satisfying Core's own frozen public contract, design doc Section
    // 5.2) but not yet stored/used anywhere - no phase before PHASE16 needs
    // Core to actually report anything through it.
    (void)hostServices;

    // editor-core-separation-6 campaign, PHASE2
    // (PHASE2_PLUGIN_CAPABILITY_ORCHESTRATOR_REGISTRY_AND_RENDER_FEATURE_MIGRATION.md,
    // Step 3.4) - populates m_capabilityOrchestrators. Runs BEFORE
    // RegisterOffscreenRenderPipelineProviders() below (a clearer, more
    // readable convention - not itself load-bearing, see
    // RegisterBuiltinCapabilityOrchestrators()'s own doc comment in Core.h).
    RegisterBuiltinCapabilityOrchestrators();

    // render-pass-3 campaign, PHASE2/PHASE3 - registers both
    // m_offscreenRenderPipeline (every remaining production pass) and
    // m_presentRenderPipeline ("Present" alone) once, here, at construction
    // time (editor-core-separation-1 campaign, PHASE13 - relocated from
    // Application::Application()'s own constructor body, which used to call
    // these two methods on itself).
    RegisterOffscreenRenderPipelineProviders();
    RegisterPresentRenderPipelineProvider();
}

// editor-core-separation-6 campaign, PHASE2 - defined here (out-of-line),
// NOT inline in Core.h, because m_capabilityOrchestrators holds
// std::unique_ptr<IPluginCapabilityOrchestrator> and that type is only
// forward-declared in Core.h - see ~Core()'s own doc comment in Core.h for
// the full "why".
Core::~Core() = default;

// editor-core-separation-6 campaign, PHASE2
// (PHASE2_PLUGIN_CAPABILITY_ORCHESTRATOR_REGISTRY_AND_RENDER_FEATURE_MIGRATION.md,
// Step 3.4) - adding a brand-new capability kind in the future means writing
// ONE new class implementing IPluginCapabilityOrchestrator and adding ONE
// line here - never touching Core::LoadPlugins()'s own body, never touching
// Core::RegisterOffscreenRenderPipelineProviders()'s own body, ever again.
void Core::RegisterBuiltinCapabilityOrchestrators()
{
    m_capabilityOrchestrators.push_back(std::make_unique<LegacyRenderFeatureOrchestrator>(*this));
    // editor-core-separation-6 campaign, PHASE3
    // (PHASE3_EDITOR_PANEL_ORCHESTRATOR_MIGRATION.md) - the second real
    // IPluginCapabilityOrchestrator implementation, proving the registry
    // generalizes beyond render features. See EditorHost.cpp's own
    // constructor for the matching reordering fix this migration required.
    m_capabilityOrchestrators.push_back(std::make_unique<EditorPanelCapabilityOrchestrator>());
    // editor-core-separation-6 campaign, PHASE4
    // (PHASE4_RENDER_FEATURE_COMPOSITOR_CORE_AND_ORDERING.md) - the third real
    // IPluginCapabilityOrchestrator implementation, the real `_v2`
    // render-feature compositing pipeline - reuses the SAME m_renderer
    // member AddAtmosphereCompositePass()/every other real pass in this file
    // already reads (never a second, duplicate Renderer instance).
    //
    // editor-core-separation-6 campaign, PHASE7
    // (PHASE7_RENDER_GRAPH_PANEL_VISIBILITY.md) - m_renderFeatureCompositorPtr
    // is captured HERE, at the exact same statement that constructs the
    // owning std::unique_ptr, BEFORE it is moved into
    // m_capabilityOrchestrators - see Core.h's own doc comment on
    // GetRenderFeatureCompositor()/m_renderFeatureCompositorPtr for why this
    // is a plain, zero-cost pointer with no dynamic_cast/RTTI involved.
    auto renderFeatureCompositor = std::make_unique<RenderFeatureCompositor>(*this, m_renderer);
    m_renderFeatureCompositorPtr = renderFeatureCompositor.get();
    m_capabilityOrchestrators.push_back(std::move(renderFeatureCompositor));
}

// editor-core-separation-3 campaign, PHASE2
// (PHASE2_PLUGIN_HOST_AND_HELLO_WORLD_HANDSHAKE_PROBE.md) - a thin
// pass-through into m_pluginHost, always compiled (see Core.h's own doc
// comment on this method - only EditorHost.cpp's own call site is gated
// behind `#if GTE_ENABLE_PLUGINS`, mirroring GTE_ENABLE_NETWORK's own
// existing "gate the call site, not the class" precedent).
void Core::LoadPlugins(const std::filesystem::path& pluginsDirectory)
{
    m_pluginHost.LoadPlugins(pluginsDirectory);

    // editor-core-separation-6 campaign, PHASE2 - each orchestrator
    // discovers/validates its own capability kind against the freshly-loaded
    // module list. LegacyRenderFeatureOrchestrator::OnPluginsLoaded() is a
    // verbatim relocation of the multi-plugin warning that used to live
    // directly in this method's own body (editor-core-separation-4,
    // PHASE5) - same trigger condition, same exact warning text.
    for (auto& orchestrator : m_capabilityOrchestrators) {
        orchestrator->OnPluginsLoaded(m_pluginHost.AllLoadedModules());
    }
}

void Core::Update(const InputFrame& input, float deltaTime)
{
    const bool steppedThisFrame = input.playbackPaused && input.stepRequested;
    m_engineContext.time.Advance(static_cast<double>(deltaTime), input.playbackPaused, steppedThisFrame, kFixedStepSeconds);

    // Game::Update() itself is already wrapped in
    // GTE_PROFILE_SCOPE("Game::Update") (see src/Game/Game.cpp) - not
    // wrapped again here too (see AGENTS.md, "Profiling").
    if (input.inputState != nullptr) {
        m_game.Update(m_engineContext, *input.inputState);
    }
}

// render-pass-3 campaign, PHASE3 (Step 3.1) - a small, linear scan (the
// realistic view count is 0-2, so this is never worth a hashed lookup).
const RenderPassViewData* Core::FindViewData(rg::RenderViewId view) const noexcept
{
    for (const RenderPassViewData& viewData : m_currentViewDataThisFrame) {
        if (viewData.id == view) {
            return &viewData;
        }
    }
    return nullptr;
}

// editor-core-separation-6 campaign, PHASE2
// (PHASE2_PLUGIN_CAPABILITY_ORCHESTRATOR_REGISTRY_AND_RENDER_FEATURE_MIGRATION.md,
// Step 3.3) - the ENTIRE body is exactly the 3 lines that already computed
// isGameView/compositedKey/pluginTarget inline in the old "PluginRenderFeatures"
// provider body, plus the view's extent (needed by RenderFeatureCompositor,
// PHASE4 - LegacyRenderFeatureOrchestrator itself simply never reads
// `.extent`). Keeps kGameCompositedOutputKey/kSceneCompositedOutputKey
// exactly where they already are (below) - no risk of a duplicate/
// out-of-sync copy of those two rg::RenderPassId constants.
//
// editor-core-separation-6 campaign, PHASE4
// (PHASE4_RENDER_FEATURE_COMPOSITOR_CORE_AND_ORDERING.md) - gained a real
// `.sampler` resolution step (see PluginRenderFeatureTargetInfo::sampler's
// own doc comment, Core.h, for why this method is no longer `const`).
// `compositedTexture` resolves the SAME persistent RenderTexture
// "AtmosphereComposite" (above) just registered `composited` under this
// exact frame - `AtmosphereLutRenderer::CompositedOutput()` looks it up by
// the SAME literal name ("GameViewComposited"/"SceneViewComposited")
// "AtmosphereComposite" passed to `AddAtmosphereCompositePass()`'s own
// `outputTextureName` parameter. Falls back to `viewData->renderTexture`'s
// own sampler (the raw, pre-composite view target) in the same defensive
// case `.target` itself already falls back to `viewData->colorTarget` -
// see this method's own pre-existing caveat about that fallback case above.
std::optional<Core::PluginRenderFeatureTargetInfo> Core::FindPluginRenderFeatureTarget(
    const rg::RenderPassFrameContext& frame)
{
    const RenderPassViewData* viewData = FindViewData(frame.currentView);
    if (viewData == nullptr) {
        return std::nullopt;
    }
    const bool isGameView = (frame.currentView == rg::RenderViewId::Named("Game"));
    const rg::RenderPassId compositedKey = isGameView ? kGameCompositedOutputKey : kSceneCompositedOutputKey;
    const std::optional<rg::TextureHandle> composited = frame.blackboard.Fetch<rg::TextureHandle>(compositedKey);

    PluginRenderFeatureTargetInfo info;
    info.target = composited.value_or(viewData->colorTarget);
    info.extent = viewData->renderTexture != nullptr ? viewData->renderTexture->Extent() : VkExtent2D{};

    RenderTexture* compositedTexture = composited.has_value()
        ? m_atmosphereLutRenderer.CompositedOutput(isGameView ? "GameViewComposited" : "SceneViewComposited")
        : nullptr;
    info.sampler = compositedTexture != nullptr
        ? compositedTexture->Sampler()
        : (viewData->renderTexture != nullptr ? viewData->renderTexture->Sampler() : VK_NULL_HANDLE);

    return info;
}


// render-pass-3 campaign, PHASE2/PHASE3 - registers every remaining
// production pass onto m_offscreenRenderPipeline. Relocated verbatim from
// Application::RegisterOffscreenRenderPipelineProviders() (editor-core-
// separation-1 campaign, PHASE13) - every m_editorLayer-> call site below is
// now reached through Core's OWN null-checked hook (Locked Design Decision
// #8's first bucket: GameViewTarget()/SceneViewTarget()/RenderSceneGrid()
// physically live inside this per-frame orchestration body, so they moved
// here together with it).
void Core::RegisterOffscreenRenderPipelineProviders()
{
    m_offscreenRenderPipeline.SetLegacyViewScopeTranslator(&TranslateLegacyViewScope);

    // "AtmosphereSharedLut" - ProviderScope::Once, BeforeEverything.
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

    // "GpuSkinning" - ProviderScope::Once.
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
                desc.tags = kGpuSkinningDispatchPassTag.bit;
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

            frame.blackboard.Publish<std::vector<rg::BufferHandle>>(
                kGpuSkinningOutputsKey, m_gpuSkinningHandlesThisFrame);
        });

    // "AtmosphereViewLut" - ProviderScope::PerActiveView, PreOpaques.
    m_offscreenRenderPipeline.Register("AtmosphereViewLut", rg::ProviderScope::PerActiveView,
        [this](const rg::RenderPassFrameContext& frame, std::vector<rg::RenderPassDesc>&) {
            const RenderPassViewData* viewData = FindViewData(frame.currentView);
            if (viewData == nullptr) {
                return;
            }

            const std::optional<AtmosphereSharedLutBlackboardEntry> sharedLuts =
                frame.blackboard.Fetch<AtmosphereSharedLutBlackboardEntry>(kAtmosphereSharedLutKey);
            if (!sharedLuts.has_value()) {
                return;
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
            frame.builder.KeepVolumeTextureOutput(viewLuts.aerialPerspectiveVolumeHandle);

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

    // "RenderOpaque" - ProviderScope::PerActiveView.
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
            const Mat4 viewProjectionOverride = viewData->viewProjection;
            IFrameDebuggerCaptureRecorder* frameDebuggerCapture =
                isGameView ? m_currentFrameDebuggerCaptureForOffscreenPipeline : nullptr;

            rg::RenderPassDesc desc;
            desc.debugName = "RenderOpaque";
            desc.kind = rg::PassKind::Graphics;
            desc.order = rg::RenderPassEvent::Opaques;
            desc.view = frame.currentView;
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
                    m_game.Render(m_renderer, aspectWidthOverHeight, nullptr, frameDebuggerCapture, std::nullopt,
                        m_gpuDrivenBatchedEntitiesThisFrame);
                } else {
                    m_game.Render(m_renderer, aspectWidthOverHeight, &viewProjectionOverride);
                }
                m_renderer.EndGraphPassRecording();
            };
            out.push_back(std::move(desc));
        });

    // GPU-Driven Frustum Culling + Indirect Draw campaign (render-pass-5),
    // PHASE5 - "GpuDrivenBatches" - ProviderScope::PerActiveView.
    //
    // CORRECTNESS-CRITICAL PLACEMENT REQUIREMENT (unchanged from before this
    // phase's own relocation - see the original Application.cpp's own
    // identical warning, now here verbatim): this Register(...) call MUST
    // stay TEXTUALLY AFTER "RenderOpaque"'s own Register(...) call and
    // TEXTUALLY BEFORE "DrawSkyBackground"'s own Register(...) call.
    m_offscreenRenderPipeline.Register("GpuDrivenBatches", rg::ProviderScope::PerActiveView,
        [this](const rg::RenderPassFrameContext& frame, std::vector<rg::RenderPassDesc>& out) {
            if (frame.currentView != rg::RenderViewId::Named("Game")) {
                return;
            }

            const RenderPassViewData* viewData = FindViewData(frame.currentView);
            if (viewData == nullptr) {
                return;
            }
            const rg::TextureHandle viewTarget = viewData->colorTarget;

            const std::array<Plane, 6> frustumPlanes = ExtractFrustumPlanes(m_gpuDrivenGameViewProjectionThisFrame);
            const bool useCompaction = m_renderer.SupportsDrawIndirectCount();
            const Mat4 viewProjection = m_gpuDrivenGameViewProjectionThisFrame;

            for (const GpuDrivenBatchRenderData& batch : m_gpuDrivenBatchesThisFrame) {
                const rg::BufferHandle inputHandle = batch.inputHandle;
                const rg::BufferHandle indirectHandle = batch.indirectHandle;
                const rg::BufferHandle countHandle = batch.countHandle;
                const VkBuffer indirectBufferNative = batch.indirectBufferNative;
                const VkBuffer countBufferNative = batch.countBufferNative;
                const VkDescriptorSet cullingDescriptorSet = batch.cullingDescriptorSet;
                const VkDescriptorSet instanceBufferDescriptorSet = batch.instanceBufferDescriptorSet;
                const std::size_t instanceCount = batch.instanceCount;
                const MeshHandle meshHandle = batch.mesh;
                const PipelineHandle originalPipelineHandle = batch.originalPipeline;

                // --- "<batch> ResetCount" (Compute) ----------------------
                {
                    rg::RenderPassDesc desc;
                    desc.debugName = batch.resetPassName;
                    desc.kind = rg::PassKind::Compute;
                    desc.order = rg::RenderPassEvent::Opaques;
                    desc.view = frame.currentView;
                    desc.setup = [countHandle](rg::RenderGraphBuilder::PassBuilder& pass) {
                        pass.WriteBuffer(countHandle, rg::ResourceAccess::TransferDst);
                    };
                    desc.execute = [countBufferNative](rg::PassContext& ctx) {
                        vkCmdFillBuffer(ctx.cmd, countBufferNative, 0, sizeof(std::uint32_t), 0);
                    };
                    out.push_back(std::move(desc));
                }

                // --- "<batch> Culling" (Compute) --------------------------
                {
                    rg::RenderPassDesc desc;
                    desc.debugName = batch.cullingPassName;
                    desc.kind = rg::PassKind::Compute;
                    desc.order = rg::RenderPassEvent::Opaques;
                    desc.view = frame.currentView;
                    desc.setup = [inputHandle, indirectHandle, countHandle](rg::RenderGraphBuilder::PassBuilder& pass) {
                        pass.ReadBuffer(inputHandle, rg::ResourceAccess::ComputeShaderRead);
                        pass.WriteBuffer(indirectHandle, rg::ResourceAccess::ComputeShaderWrite);
                        pass.WriteBuffer(countHandle, rg::ResourceAccess::ComputeShaderWrite);
                    };
                    desc.execute = [this, cullingDescriptorSet, instanceCount, frustumPlanes, useCompaction](
                                        rg::PassContext& ctx) {
                        struct CullingPushConstants {
                            Plane planes[6];
                            std::uint32_t instanceCount;
                            std::uint32_t useCompaction;
                        };
                        static_assert(sizeof(CullingPushConstants) == kCullingPushConstantSize,
                            "CullingPushConstants must match Shaders/FrustumCull.comp's own PushConstants block "
                            "exactly");

                        CullingPushConstants pc{};
                        for (std::size_t i = 0; i < 6; ++i) {
                            pc.planes[i] = frustumPlanes[i];
                        }
                        pc.instanceCount = static_cast<std::uint32_t>(instanceCount);
                        pc.useCompaction = useCompaction ? 1u : 0u;

                        m_renderer.BeginGraphPassRecording(ctx.cmd, ctx.recordDraw);
                        m_renderer.Dispatch(m_gpuDrivenBatchCache.Pipelines().Pipeline(), cullingDescriptorSet, &pc,
                            sizeof(pc),
                            ComputeGroupCount(static_cast<std::uint32_t>(instanceCount), kCullingLocalSizeX), 1, 1);
                        m_renderer.EndGraphPassRecording();
                    };
                    out.push_back(std::move(desc));
                }

                // --- "<batch> IndirectDraw" (Graphics) --------------------
                {
                    rg::RenderPassDesc desc;
                    desc.debugName = batch.indirectDrawPassName;
                    desc.kind = rg::PassKind::Graphics;
                    desc.order = rg::RenderPassEvent::Opaques;
                    desc.view = frame.currentView;
                    desc.legacyCategory = rg::RenderPassCategory::General;
                    desc.setup = [viewTarget, indirectHandle, countHandle, inputHandle](
                                     rg::RenderGraphBuilder::PassBuilder& pass) {
                        pass.WriteColorAttachment(viewTarget);
                        pass.WriteDepthStencilAttachment(viewTarget);
                        pass.ReadBuffer(indirectHandle, rg::ResourceAccess::IndirectCommandRead);
                        pass.ReadBuffer(countHandle, rg::ResourceAccess::IndirectCommandRead);
                        pass.ReadBuffer(inputHandle, rg::ResourceAccess::VertexShaderStorageRead);
                    };
                    desc.execute = [this, meshHandle, originalPipelineHandle, indirectBufferNative, countBufferNative,
                                        instanceBufferDescriptorSet, instanceCount, useCompaction, viewProjection](
                                        rg::PassContext& ctx) {
                        const Mesh* mesh = m_game.GetRenderSystem().TryGetMesh(meshHandle);
                        const Pipeline* instancedPipeline = nullptr;
                        try {
                            instancedPipeline =
                                &m_gpuDrivenBatchCache.ResolveInstancedPipeline(m_renderer, originalPipelineHandle);
                        } catch (const std::exception& e) {
                            std::fprintf(
                                stderr, "GpuDrivenBatches: failed to resolve instanced pipeline: %s\n", e.what());
                        }
                        if (mesh == nullptr || instancedPipeline == nullptr) {
                            return;
                        }

                        m_renderer.BeginGraphPassRecording(ctx.cmd, ctx.recordDraw);
                        const VkBuffer countBufferForCall = useCompaction ? countBufferNative : VK_NULL_HANDLE;
                        m_renderer.SubmitIndirect(*instancedPipeline, *mesh, indirectBufferNative,
                            /*indirectOffset=*/0, static_cast<std::uint32_t>(instanceCount), countBufferForCall,
                            /*countBufferOffset=*/0, instanceBufferDescriptorSet, viewProjection);
                        if (ctx.recordIndirectDraw) {
                            ctx.recordIndirectDraw();
                        }
                        m_renderer.EndGraphPassRecording();
                    };
                    out.push_back(std::move(desc));
                }
            }
        });

    // "DrawSkyBackground" - ProviderScope::PerActiveView, AfterOpaques.
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
                return;
            }

            const char* skyViewLutName = isGameView ? "AtmosphereSkyViewLut_GameView" : "AtmosphereSkyViewLut_SceneView";
            const std::function<void(VkCommandBuffer)> recordSkyBackground =
                MakeRecordSkyBackgroundCallback(m_atmosphereLutRenderer, m_renderer, viewData->viewProjection,
                    sharedLuts->parameters, viewLuts->frameUniforms, skyViewLutName, m_atmosphereSettings.skyExposure);

            if (isGameView) {
                frame.blackboard.Publish<std::function<void(VkCommandBuffer)>>(
                    kGameSkyBackgroundCallbackKey, recordSkyBackground);
            }

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
            desc.drawKind = rg::RenderPassDrawKind::DrawQuad;
            desc.setup = [viewTarget](rg::RenderGraphBuilder::PassBuilder& pass) {
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

    // "RenderTransparent" - ProviderScope::PerActiveView, Transparents.
    m_offscreenRenderPipeline.Register("RenderTransparent", rg::ProviderScope::PerActiveView,
        [this](const rg::RenderPassFrameContext& frame, std::vector<rg::RenderPassDesc>& out) {
            const RenderPassViewData* viewData = FindViewData(frame.currentView);
            if (viewData == nullptr) {
                return;
            }

            const std::vector<DrawCommand> transparentCommands =
                RenderSystem::CollectTransparentRenderables(m_game.GetRegistry());
            const bool hasOverlay = static_cast<bool>(viewData->recordSceneOverlay);

            if (transparentCommands.empty() && !hasOverlay) {
                return;
            }

            if (!hasOverlay) {
                return;
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
    // ProviderTiming::AfterDeferredPasses.
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

            // editor-core-separation-3 campaign, PHASE3 - publish this
            // view's freshly-computed composited handle so
            // "PluginRenderFeatures" (registered immediately after this
            // provider, below) can draw on top of the TRUE final image -
            // see kGameCompositedOutputKey/kSceneCompositedOutputKey's own
            // doc comment above for the full "why".
            frame.blackboard.Publish<rg::TextureHandle>(
                isGameView ? kGameCompositedOutputKey : kSceneCompositedOutputKey, composited);
        },
        rg::ProviderTiming::AfterDeferredPasses);

    // editor-core-separation-6 campaign, PHASE2
    // (PHASE2_PLUGIN_CAPABILITY_ORCHESTRATOR_REGISTRY_AND_RENDER_FEATURE_MIGRATION.md,
    // Step 3.4) - this provider's body collapses to the ONE generic loop over
    // every registered IPluginCapabilityOrchestrator (m_capabilityOrchestrators,
    // populated once by RegisterBuiltinCapabilityOrchestrators() - see the
    // constructor above). Core itself no longer hand-codes a bespoke,
    // capability-specific `for` loop here - LegacyRenderFeatureOrchestrator
    // (src/Core/Plugins/LegacyRenderFeatureOrchestrator.h/.cpp) is a VERBATIM
    // relocation of the exact loop body that used to live directly in this
    // lambda (editor-core-separation-3, PHASE3), so this is a pure,
    // zero-observable-behavior-change refactor: same warning text, same pass
    // names, same "AfterDeferredPasses" timing, same LAST-Register(...)-call
    // position in this function (still immediately after "AtmosphereComposite"'s
    // own call, above).
    m_offscreenRenderPipeline.Register("PluginRenderFeatures", rg::ProviderScope::PerActiveView,
        [this](const rg::RenderPassFrameContext& frame, std::vector<rg::RenderPassDesc>& out) {
            for (auto& orchestrator : m_capabilityOrchestrators) {
                orchestrator->ContributeRenderGraphPasses(frame, out);
            }
        },
        rg::ProviderTiming::AfterDeferredPasses);
}

// render-pass-3 campaign, PHASE3 (Step 3.5) - the swapchain regime's own
// RenderPipeline, with exactly one provider, "Present". Relocated verbatim
// from Application::RegisterPresentRenderPipelineProvider(), PHASE13.
void Core::RegisterPresentRenderPipelineProvider()
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

void Core::BuildFrame()
{
    // editor-core-separation-1 campaign, PHASE13 (Locked Design Decision #8's
    // first bucket) - GameViewTarget()/SceneViewTarget()/
    // PrepareFrameDebuggerCaptureContext() are called ONLY here, through
    // Core's own null-checked m_editorLayer hook. A nullptr hook (a Player
    // host) makes gameTarget/sceneTarget/frameDebuggerCapture all stay
    // nullptr, which - exactly like NullEditorLayer's own all-no-op methods
    // already did for Application - makes every render-graph-building branch
    // below a safe, silent no-op.
    RenderTexture* gameTarget = (m_editorLayer != nullptr) ? m_editorLayer->GameViewTarget() : nullptr;
    RenderTexture* sceneTarget = (m_editorLayer != nullptr) ? m_editorLayer->SceneViewTarget() : nullptr;
    m_gameTargetThisFrame = gameTarget;
    m_sceneTargetThisFrame = sceneTarget;

    IFrameDebuggerCaptureRecorder* frameDebuggerCapture =
        (m_editorLayer != nullptr) ? m_editorLayer->PrepareFrameDebuggerCaptureContext() : nullptr;

    // Call 1 of 2: the SYNCHRONOUS offscreen regime - Game view + Scene view
    // together.
    if (gameTarget != nullptr || sceneTarget != nullptr) {
        try {
            GTE_PROFILE_SCOPE("RenderGraph::Execute(Offscreen)");
            const VkCommandBuffer offscreenCmd = m_renderer.BeginOffscreenRenderGraphRecording();
            m_renderGraph.Execute(offscreenCmd, rg::ExecuteTimingMode::SynchronousImmediateReadback,
                [&](rg::RenderGraphBuilder& b) {
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
                    m_gpuDrivenBatchesThisFrame.clear();
                    m_gpuDrivenBatchedEntitiesThisFrame.clear();
                    m_gpuDrivenBatchDebugInfoLastFrame.clear();
                    float gameAspectForReplay = 1.0f;

                    if (gameTarget != nullptr) {
                        const VkExtent2D extent = gameTarget->Extent();
                        const float aspect =
                            AspectRatioOf(static_cast<int>(extent.width), static_cast<int>(extent.height));

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
                        m_currentFrameDebuggerCaptureForOffscreenPipeline = frameDebuggerCapture;
                        gameAspectForReplay = aspect;

                        m_gpuDrivenGameViewProjectionThisFrame = gameViewProjection;
                        {
                            std::unordered_set<VkBuffer> gpuSkinnedOutputBuffers;
                            gpuSkinnedOutputBuffers.reserve(m_gpuSkinningRequestsThisFrame.size());
                            for (const AnimationSystem::GpuSkinningDispatchRequest& request :
                                m_gpuSkinningRequestsThisFrame) {
                                gpuSkinnedOutputBuffers.insert(request.outputBuffer);
                            }

                            RenderSystem& renderSystem = m_game.GetRenderSystem();
                            const std::vector<GpuDrivenBatchFrameEntry> entries =
                                renderSystem.CollectGpuDrivenBatches(m_game.GetRegistry(), m_renderer,
                                    m_gpuDrivenBatchCache, gpuSkinnedOutputBuffers);

                            for (const GpuDrivenBatchFrameEntry& frameEntry : entries) {
                                const GpuDrivenBatchKey key{ frameEntry.mesh, frameEntry.pipeline };
                                const GpuDrivenBatchCache::Entry* cacheEntry = m_gpuDrivenBatchCache.TryGet(key);
                                const Mesh* meshPtr = renderSystem.TryGetMesh(frameEntry.mesh);
                                if (cacheEntry == nullptr || meshPtr == nullptr) {
                                    continue;
                                }

                                bool instancedPipelineResolved = true;
                                try {
                                    m_gpuDrivenBatchCache.ResolveInstancedPipeline(
                                        m_renderer, frameEntry.pipeline);
                                } catch (const std::exception& e) {
                                    std::fprintf(stderr,
                                        "GpuDrivenBatches: failed to resolve instanced pipeline for a batch: "
                                        "%s\n",
                                        e.what());
                                    instancedPipelineResolved = false;
                                }
                                if (!instancedPipelineResolved) {
                                    continue;
                                }

                                const GpuDrivenBatchNames& names = BatchNamePool().NamesFor(key);

                                GpuDrivenBatchRenderData data;
                                data.mesh = frameEntry.mesh;
                                data.originalPipeline = frameEntry.pipeline;
                                data.instanceCount = frameEntry.instanceCount;
                                data.inputHandle = b.ImportBuffer(names.inputBufferName,
                                    cacheEntry->inputBuffer.Native(), cacheEntry->inputBuffer.Size());
                                data.indirectHandle = b.ImportBuffer(names.indirectBufferName,
                                    cacheEntry->indirectCommandBuffer.Native(),
                                    cacheEntry->indirectCommandBuffer.Size());
                                data.countHandle = b.ImportBuffer(names.countBufferName,
                                    cacheEntry->countBuffer.Native(), cacheEntry->countBuffer.Size());
                                data.indirectBufferNative = cacheEntry->indirectCommandBuffer.Native();
                                data.countBufferNative = cacheEntry->countBuffer.Native();
                                data.cullingDescriptorSet = cacheEntry->cullingDescriptorSet;
                                data.instanceBufferDescriptorSet = cacheEntry->instanceBufferDescriptorSet;
                                data.resetPassName = names.resetPassName;
                                data.cullingPassName = names.cullingPassName;
                                data.indirectDrawPassName = names.indirectDrawPassName;
                                data.displayName = names.displayName;
                                m_gpuDrivenBatchesThisFrame.push_back(data);

                                for (const DrawCommand& command : frameEntry.commands) {
                                    m_gpuDrivenBatchedEntitiesThisFrame.insert(command.entity);
                                }
                            }
                        }
                    }

                    rg::TextureHandle sceneColorHandleForBlurValidation{};
                    VkExtent2D sceneExtentForBlurValidation{};
                    bool sceneVisibleForBlurValidation = false;

                    if (sceneTarget != nullptr) {
                        const VkExtent2D extent = sceneTarget->Extent();
                        const float aspect =
                            AspectRatioOf(static_cast<int>(extent.width), static_cast<int>(extent.height));
                        // editor-core-separation-1 campaign, PHASE13 - reached
                        // only when sceneTarget != nullptr, which itself can
                        // only be true when m_editorLayer != nullptr
                        // (SceneViewTarget() above only ever returns non-null
                        // through a real hook) - safe without a second,
                        // redundant null-check here.
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
                        sceneViewData.recordSceneOverlay = [this](VkCommandBuffer cmd, const Mat4& viewProj) {
                            if (m_editorLayer != nullptr) {
                                m_editorLayer->RenderSceneGrid(m_renderer, cmd, viewProj);
                            }
                        };
                        m_currentViewDataThisFrame.push_back(sceneViewData);

                        frame.activeViews.push_back(rg::RenderViewId::Named("Scene"));

                        sceneColorHandleForBlurValidation = h;
                        sceneExtentForBlurValidation = extent;
                        sceneVisibleForBlurValidation = true;
                    }

                    m_offscreenRenderPipeline.DeclareInto(b, frame);

                    const std::optional<std::function<void(VkCommandBuffer)>> gameSkyBackgroundCallbackForReplay =
                        blackboard.Fetch<std::function<void(VkCommandBuffer)>>(kGameSkyBackgroundCallbackKey);
#ifndef NDEBUG
                    blackboard.ReportUnusedPublishesIfAny();
#endif

                    std::vector<rg::TextureHandle> outputs = std::move(frame.finalTextureOutputs);

                    if (gameTarget != nullptr && frameDebuggerCapture != nullptr && m_editorLayer != nullptr
                        && m_editorLayer->ConsumePendingFrameDebuggerReplayRequest()) {
                        const std::vector<rg::BufferHandle> gpuSkinningBuffersForReplay =
                            blackboard.Fetch<std::vector<rg::BufferHandle>>(kGpuSkinningOutputsKey)
                                .value_or(std::vector<rg::BufferHandle>{});
                        const std::function<void(VkCommandBuffer)> recordGameSkyBackground =
                            gameSkyBackgroundCallbackForReplay.value_or(std::function<void(VkCommandBuffer)>{});
                        const std::size_t objectCount = m_game.CountGameViewDrawCommandsThisFrame();
                        const std::vector<rg::TextureHandle> replayStepHandles = frameDebuggerCapture->AddReplayPasses(
                            b, m_game, m_renderer, gameAspectForReplay, objectCount, gpuSkinningBuffersForReplay,
                            recordGameSkyBackground, *gameTarget);
                        for (const rg::TextureHandle& replayHandle : replayStepHandles) {
                            outputs.push_back(replayHandle);
                        }
                    }

                    if (sceneVisibleForBlurValidation && m_editorLayer != nullptr) {
                        if (const std::optional<rg::TextureHandle> blurHandle = m_editorLayer->AddBlurValidationPass(
                                b, m_renderer, sceneColorHandleForBlurValidation, sceneExtentForBlurValidation)) {
                            outputs.push_back(*blurHandle);
                        }
                    }

                    if (sceneVisibleForBlurValidation && m_editorLayer != nullptr) {
                        if (const std::optional<GBufferValidationHandles> gbufferHandles =
                                m_editorLayer->AddGBufferValidationPass(
                                    b, m_renderer, sceneExtentForBlurValidation)) {
                            outputs.push_back(gbufferHandles->albedo);
                            outputs.push_back(gbufferHandles->normal);
                            outputs.push_back(gbufferHandles->visualized);
                        }
                    }

                    return outputs;
                });

            // GPU-Driven Frustum Culling + Indirect Draw campaign
            // (render-pass-5), PHASE6 - copies each eligible batch's own
            // atomic visible-count buffer into its persistent, host-visible
            // countReadbackBuffer.
            for (const GpuDrivenBatchRenderData& batch : m_gpuDrivenBatchesThisFrame) {
                const GpuDrivenBatchKey copyKey{ batch.mesh, batch.originalPipeline };
                const GpuDrivenBatchCache::Entry* entry = m_gpuDrivenBatchCache.TryGet(copyKey);
                if (entry == nullptr) {
                    continue;
                }

                VkBufferMemoryBarrier2 preCopyBarrier{};
                preCopyBarrier.sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER_2;
                preCopyBarrier.srcStageMask = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT;
                preCopyBarrier.srcAccessMask = VK_ACCESS_2_SHADER_WRITE_BIT;
                preCopyBarrier.dstStageMask = VK_PIPELINE_STAGE_2_TRANSFER_BIT;
                preCopyBarrier.dstAccessMask = VK_ACCESS_2_TRANSFER_READ_BIT;
                preCopyBarrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
                preCopyBarrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
                preCopyBarrier.buffer = batch.countBufferNative;
                preCopyBarrier.offset = 0;
                preCopyBarrier.size = sizeof(std::uint32_t);

                VkDependencyInfo preCopyDependency{};
                preCopyDependency.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO;
                preCopyDependency.bufferMemoryBarrierCount = 1;
                preCopyDependency.pBufferMemoryBarriers = &preCopyBarrier;
                vkCmdPipelineBarrier2(offscreenCmd, &preCopyDependency);

                VkBufferCopy region{};
                region.srcOffset = 0;
                region.dstOffset = 0;
                region.size = sizeof(std::uint32_t);
                vkCmdCopyBuffer(
                    offscreenCmd, batch.countBufferNative, entry->countReadbackBuffer.Native(), 1, &region);
            }

            // Manual finalize.
            if (gameTarget != nullptr) {
                FinalizeRenderTextureForExternalSampling(offscreenCmd, *gameTarget);
                m_renderGraph.NotifyDebugTextureStateOverride(
                    "GameView", rg::RequiredStateFor(rg::ResourceAccess::ShaderRead, false));

                m_atmosphereLutRenderer.FinalizeAerialPerspectiveCompositeForSampling(
                    offscreenCmd, "GameViewComposited");
                m_renderGraph.NotifyDebugTextureStateOverride(
                    "GameViewComposited", rg::RequiredStateFor(rg::ResourceAccess::ShaderRead, false));
                if (m_editorLayer != nullptr) {
                    m_editorLayer->SetGameViewCompositedTexture(
                        m_atmosphereLutRenderer.CompositedOutput("GameViewComposited"));
                }
            }
            if (sceneTarget != nullptr) {
                FinalizeRenderTextureForExternalSampling(offscreenCmd, *sceneTarget);
                m_renderGraph.NotifyDebugTextureStateOverride(
                    "SceneView", rg::RequiredStateFor(rg::ResourceAccess::ShaderRead, false));

                m_atmosphereLutRenderer.FinalizeAerialPerspectiveCompositeForSampling(
                    offscreenCmd, "SceneViewComposited");
                m_renderGraph.NotifyDebugTextureStateOverride(
                    "SceneViewComposited", rg::RequiredStateFor(rg::ResourceAccess::ShaderRead, false));
                if (m_editorLayer != nullptr) {
                    m_editorLayer->SetSceneViewCompositedTexture(
                        m_atmosphereLutRenderer.CompositedOutput("SceneViewComposited"));
                }
            }
            if (m_editorLayer != nullptr) {
                m_editorLayer->FinalizeBlurValidationForSampling(offscreenCmd);
            }
            m_renderGraph.NotifyDebugTextureStateOverride(
                "BlurredSceneOutput", rg::RequiredStateFor(rg::ResourceAccess::ShaderRead, false));

            if (m_editorLayer != nullptr) {
                m_editorLayer->FinalizeGBufferValidationForSampling(offscreenCmd);
            }
            m_renderGraph.NotifyDebugTextureStateOverride(
                "GBufferAlbedo", rg::RequiredStateFor(rg::ResourceAccess::ShaderRead, false));
            m_renderGraph.NotifyDebugTextureStateOverride(
                "GBufferNormal", rg::RequiredStateFor(rg::ResourceAccess::ShaderRead, false));
            m_renderGraph.NotifyDebugTextureStateOverride(
                "GBufferVisualized", rg::RequiredStateFor(rg::ResourceAccess::ShaderRead, false));

            m_renderer.EndOffscreenRenderGraphRecording();

            // GPU-Driven Frustum Culling + Indirect Draw campaign
            // (render-pass-5), PHASE6 - reads back this frame's real
            // "instances culled" count for every eligible batch.
            for (const GpuDrivenBatchRenderData& batch : m_gpuDrivenBatchesThisFrame) {
                const GpuDrivenBatchKey key{ batch.mesh, batch.originalPipeline };
                GpuDrivenBatchDebugInfo info;
                info.batchName = (batch.displayName != nullptr) ? batch.displayName : "(unnamed batch)";
                info.instanceCount = static_cast<std::uint32_t>(batch.instanceCount);
                info.visibleCount = m_gpuDrivenBatchCache.ReadLastKnownVisibleCount(key);
                m_gpuDrivenBatchDebugInfoLastFrame.push_back(info);
            }

            // editor-core-separation-1 campaign, PHASE13 - the Game-View
            // FrameCaptureBridge success-path capture is a HOST-LEVEL
            // AUTOMATION concern (design doc Section 6.1) - it was
            // RELOCATED OUT of this try block, into Application::Run()
            // itself, now running right after BuildFrame() RETURNS (safe:
            // EndOffscreenRenderGraphRecording() above already fence-waited
            // by the time this method returns) - see
            // Core::GetGameViewTargetThisFrame()'s own doc comment (Core.h)
            // and PHASE13_COMPLETION_REPORT.md for the full reasoning.

            m_renderGraph.FinalizeSynchronousGpuTiming();
        } catch (const std::exception& e) {
            std::fprintf(stderr, "RenderGraph offscreen Execute() failed: %s\n", e.what());
            assert(false && "RenderGraph offscreen Execute() threw - see stderr");
        }
    }

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
}

void Core::Present()
{
    GTE_PROFILE_SCOPE("Renderer::PresentViaRenderGraph");
    const bool needsDirectGameRender = (m_gameTargetThisFrame == nullptr && m_sceneTargetThisFrame == nullptr);
    const std::optional<float> directGameRenderAspect =
        needsDirectGameRender ? std::optional<float>(AspectRatioOf(m_windowWidth, m_windowHeight)) : std::nullopt;

    std::optional<DrawStats> presentStats;
    try {
        presentStats = m_renderer.PresentViaRenderGraph(m_renderGraph, needsDirectGameRender,
            [&](rg::RenderGraphBuilder& b, rg::TextureHandle swapchainImage) {
                m_needsDirectGameRenderThisFrame = needsDirectGameRender;
                m_directGameRenderAspectThisFrame = directGameRenderAspect;
                m_swapchainImageThisFrame = swapchainImage;
                // editor-core-separation-1 campaign, PHASE13 - IEditorLayer::
                // Render() is explicitly HOST-LEVEL (Locked Design Decision
                // #8's second bucket) - m_presentImGuiRecorder is a callback
                // Application/EditorHost itself constructed (capturing ITS
                // OWN m_editorLayer, never Core's), handed in once via
                // SetPresentImGuiRecorder() - Core merely stores/forwards it,
                // never calls IEditorLayer::Render() itself. A default-
                // constructed (empty) std::function here is a safe no-op via
                // AddPresentPass()'s own existing truthiness check.
                m_recordImGuiThisFrame = m_presentImGuiRecorder;

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

    // editor-core-separation-1 campaign, PHASE13 - the swapchain
    // FrameCaptureBridge success-path capture is a HOST-LEVEL AUTOMATION
    // concern (design doc Section 6.1) - RELOCATED OUT of this method,
    // into Application::Run() itself, now running right after Present()
    // RETURNS (safe - see this method's own final fence-wait inside
    // Renderer::PresentViaRenderGraph()). See PHASE13_COMPLETION_REPORT.md.

    if (presentStats.has_value()) {
        Profiling::FrameProfiler::Instance().SetGpuPassDrawStats(Profiling::GpuPass::Present,
            Profiling::GpuSampleStatus::Present, presentStats->drawCallCount, presentStats->triangleCount);
        const GpuTimingSample presentTiming = m_renderGraph.LastKnownStatsFor("Present").timing;
        Profiling::FrameProfiler::Instance().SetGpuPassTiming(Profiling::GpuPass::Present,
            ToProfilingGpuSampleStatus(presentTiming.status), presentTiming.milliseconds);
    }
}

} // namespace gte
