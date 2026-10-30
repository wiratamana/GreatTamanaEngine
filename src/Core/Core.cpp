#include "Core.h"

// Access to editor layer implementation.
#include "../Editor/EditorLayer.h"

#include "../Application/RenderPasses.h"
#include "../Profiling/FrameProfiler.h"
#include "../Profiling/NameTable.h"
#include "RenderTimingAdapter.h"
#include "GpuViewMetrics.h"
#include "../Profiling/ScopeTimer.h"
#include "../Renderer/GpuSkinning/GpuSkinningPipelines.h"
#include "../Renderer/Culling/ClusterCullMath.h"
#include "../Renderer/RenderGraph/FullscreenPass.h"
#include "../Renderer/RenderGraph/IndirectDrawPass.h"
#include "../Renderer/RenderGraph/MeshPass.h"
#include "../Renderer/RenderGraph/PassKindDefaults.h"
#include "../Renderer/RenderGraph/RenderGraphBarrierPlanner.h"
#include "../Renderer/RenderGraph/RenderGraphBuilder.h"
#include "../Renderer/RenderGraph/RecordedPass.h"
#include "../Renderer/RenderGraph/TransferPass.h"
#include "../Renderer/RenderGraph/RenderGraphDebugTextureRegistry.h"
#include "../Renderer/RenderGraph/RenderFeatureScope.h"
#include "../Renderer/RenderGraph/RenderPassToggleGuard.h"
#include "../Renderer/RenderGraph/RenderPassTagValidation.h"
#include "../Renderer/RenderGraph/ViewScopeUtil.h"
#include "../Renderer/GraphicsHandleRegistry.h"
#include "../Renderer/VulkanFormatInfo.h"
#include "ClusterCandidates.h"
#include "ClusterClaim.h"
#include "CullRequestRules.h"
#include "../Renderer/Culling/CullingTypes.h"
#include "FaultInjection.h"
#include "FeatureKit/ClusterPartPlanning.h"
#include "../Jobs/JobDispatch.h"
#include "../Jobs/JobSystem.h"
#include "Logging.h"
#include "ScreenPostProcessPassPriorityAssignment.h"
#include "Plugins/IPluginCapabilityOrchestrator.h"
#include "Plugins/RenderFeatureCompositor.h"
#include "Plugins/RenderFeatureCameraData.h"
#include "Plugins/ProjectAssemblyRegistrationLedger.h"
#include "ViewBackgroundReplayKey.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <cassert>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <optional>
#include <stdexcept>
#include <thread>
#include <unordered_map>

namespace gte {

namespace {

// Computes aspect ratio (width / height); defaults to 1.0f on invalid height.
float AspectRatioOf(int width, int height) noexcept
{
    return height > 0 ? static_cast<float>(width) / static_cast<float>(height) : 1.0f;
}

// Fixed time step for frame debugging (1/60s).
constexpr double kFixedStepSeconds = 1.0 / 60.0;

// Blackboard key for GPU skinning output buffers.
using rg::operator""_passId;
constexpr rg::RenderPassId kGpuSkinningOutputsKey = "GpuSkinning.OutputBuffers"_passId;

// Resolves color and depth texture handles for post-opaque and post-transparent passes.
// Returns nullopt when the view has no sampleable depth.
std::optional<ScenePassReadHandles> ResolveScenePassReadHandles(
    const rg::RenderGraphBuilder& builder, const RenderPassViewData* viewData)
{
    if (viewData == nullptr || viewData->renderTexture == nullptr) {
        return std::nullopt;
    }
    if (!builder.HasSampleableDepth(viewData->colorTarget)) {
        return std::nullopt;
    }
    ScenePassReadHandles handles;
    handles.colorHandle = viewData->colorTarget;
    handles.depthHandle = viewData->colorTarget;
    return handles;
}

} // namespace

Core::Core(ISurfaceProvider& surfaceProvider, IHostServices& hostServices, bool forceGpuValidation,
    PresentPolicy presentPolicy)
    : m_renderer(surfaceProvider, forceGpuValidation, presentPolicy)
    , m_sceneServicesDescriptorSet(m_renderer)
    , m_renderViewRegistry(m_renderer)
    , m_renderGraph(m_renderer)
    , m_game()
    , m_engineContext()
    , m_windowWidth(surfaceProvider.Width())
    , m_windowHeight(surfaceProvider.Height())
{
    (void)hostServices;

    RegisterBuiltinCapabilityOrchestrators();
    RegisterOffscreenRenderPipelineProviders();
    RegisterPresentRenderPipelineProvider();

#if GTE_ENABLE_PROFILER
    m_renderer.SetTimingObserver(&m_timingAdapter);
    m_renderGraph.SetTimingObserver(&m_timingAdapter);
    m_offscreenRenderPipeline.SetTimingObserver(&m_timingAdapter);
    m_presentRenderPipeline.SetTimingObserver(&m_timingAdapter);
#endif
}

Core::~Core()
{
    m_renderer.SetTimingObserver(nullptr);
}

// Initializes built-in capability orchestrators.
void Core::RegisterBuiltinCapabilityOrchestrators()
{
    auto renderFeatureCompositor = std::make_unique<RenderFeatureCompositor>(*this, m_renderer);
    m_renderFeatureCompositorPtr = renderFeatureCompositor.get();
    m_capabilityOrchestrators.push_back(std::move(renderFeatureCompositor));
}

// Loads dynamic assemblies for the project.
void Core::LoadProjectAssemblies(const std::filesystem::path& outputDirectory, EditorHost* editorHost)
{
    m_projectAssemblyHost.LoadProjectAssemblies(outputDirectory, *this, editorHost);
}

// Registers a custom render pass provider from a project assembly.
void Core::RegisterProjectRenderPassProvider(const char* debugName, rg::ProviderScope scope,
    rg::RenderPassProvider provider, rg::ProviderTiming timing)
{
    m_offscreenRenderPipeline.Register(debugName, scope, std::move(provider), timing);
    ProjectAssemblyRegistrationLedger::Instance().RecordRenderPass(debugName);
}

// Unregisters a custom render pass provider.
void Core::UnregisterProjectRenderPassProvider(const char* debugName)
{
    m_offscreenRenderPipeline.Unregister(debugName);
}

rg::RenderViewId Core::CreateRenderView(const char* name, std::uint32_t width, std::uint32_t height,
    bool depthOnly, bool allowDepthSampledAccess, PixelFormat colorFormat)
{
    RenderViewDesc desc;
    desc.width = width;
    desc.height = height;
    desc.hasColor = !depthOnly;
    desc.hasDepth = true;
    desc.colorFormat = (colorFormat == PixelFormat::Undefined && desc.hasColor) ? m_renderer.ScenePixelFormat() : colorFormat;
    desc.allowDepthSampledAccess = allowDepthSampledAccess;
    return m_renderViewRegistry.CreateOrGetView(name, desc);
}

bool Core::ResizeRenderView(rg::RenderViewId view, std::uint32_t width, std::uint32_t height)
{
    return m_renderViewRegistry.ResizeView(view, width, height);
}

RenderTexture* Core::FindRenderViewTarget(rg::RenderViewId view) const noexcept
{
    return m_renderViewRegistry.FindViewTarget(view);
}

namespace {
// False (logged) when `debugName` is null or longer than 63 bytes.
bool ValidateBuiltInPassName(const char* callerName, const char* debugName)
{
    if (debugName == nullptr) {
        GTE_LOG_WARNING("Core", std::string(callerName) + "() failed - debugName is null.");
        return false;
    }
    if (std::strlen(debugName) > 63) {
        GTE_LOG_WARNING("Core", std::string(callerName) + "('" + debugName
            + "') failed - name exceeds the 63-byte limit; shorten it.");
        return false;
    }
    return true;
}

// Validates the descriptor name and constructs the descriptor.
std::optional<GtePluginRenderFeatureDescriptor> BuildValidatedRenderFeatureDescriptor(const char* callerName,
    const char* debugName, RenderFeatureStage stage, RenderFeatureBlendMode blendMode, std::int32_t priority)
{
    if (!ValidateBuiltInPassName(callerName, debugName)) {
        return std::nullopt;
    }
    return MakeRenderFeatureDescriptor(debugName, stage, priority, blendMode);
}
} // namespace

// Registers a project render feature and records it in the hot-reload ledger.
bool Core::RegisterProjectRenderFeature(const char* debugName, RenderFeatureStage stage,
    RenderFeatureBlendMode blendMode, std::int32_t priority, ProjectRenderFeatureCallback callback)
{
    if (m_renderFeatureCompositorPtr == nullptr) {
        GTE_LOG_WARNING("Core", "RegisterProjectRenderFeature('" + std::string(debugName != nullptr ? debugName : "<null>")
            + "') failed - no RenderFeatureCompositor registered.");
        return false;
    }
    const std::optional<GtePluginRenderFeatureDescriptor> descriptor =
        BuildValidatedRenderFeatureDescriptor("RegisterProjectRenderFeature", debugName, stage, blendMode, priority);
    if (!descriptor.has_value()) {
        return false;
    }

    const bool registered = m_renderFeatureCompositorPtr->RegisterProjectFeature(*descriptor, std::move(callback));
    if (registered) {
        ProjectAssemblyRegistrationLedger::Instance().RecordRenderFeature(debugName);
    }
    return registered;
}

// Unregisters a project render feature.
void Core::UnregisterProjectRenderFeature(const char* debugName)
{
    if (m_renderFeatureCompositorPtr == nullptr || debugName == nullptr) {
        return;
    }
    m_renderFeatureCompositorPtr->UnregisterProjectFeature(debugName);
}

// Registers a custom pass executed before opaque geometry.
bool Core::AddPreOpaquePass(const char* debugName, ProjectPreOpaqueCallback callback, std::int32_t priority)
{
    if (m_renderFeatureCompositorPtr == nullptr) {
        GTE_LOG_WARNING("Core", "AddPreOpaquePass('" + std::string(debugName != nullptr ? debugName : "<null>")
            + "') failed - no RenderFeatureCompositor registered.");
        return false;
    }
    if (!ValidateBuiltInPassName("AddPreOpaquePass", debugName)) {
        return false;
    }

    const bool registered = m_renderFeatureCompositorPtr->RegisterStageFeature(
        CompositorStage::PreOpaque, debugName, priority, std::move(callback), RenderFeatureOwner::Project);
    if (registered) {
        ProjectAssemblyRegistrationLedger::Instance().RecordPreOpaqueFeature(debugName);
    }
    return registered;
}

// Unregisters a pre-opaque pass.
void Core::RemovePreOpaquePass(const char* debugName)
{
    if (m_renderFeatureCompositorPtr == nullptr || debugName == nullptr) {
        return;
    }
    m_renderFeatureCompositorPtr->UnregisterStageFeature(CompositorStage::PreOpaque, debugName);
}

// Registers a custom pass executed after opaque geometry.
bool Core::AddPostOpaquePass(const char* debugName, ProjectScenePassCallback callback, std::int32_t priority)
{
    if (m_renderFeatureCompositorPtr == nullptr) {
        GTE_LOG_WARNING("Core", "AddPostOpaquePass('" + std::string(debugName != nullptr ? debugName : "<null>")
            + "') failed - no RenderFeatureCompositor registered.");
        return false;
    }
    if (!ValidateBuiltInPassName("AddPostOpaquePass", debugName)) {
        return false;
    }

    const bool registered = m_renderFeatureCompositorPtr->RegisterStageFeature(
        CompositorStage::PostOpaque, debugName, priority, std::move(callback), RenderFeatureOwner::Project);
    if (registered) {
        ProjectAssemblyRegistrationLedger::Instance().RecordPostOpaqueFeature(debugName);
    }
    return registered;
}

// Unregisters a post-opaque pass.
void Core::RemovePostOpaquePass(const char* debugName)
{
    if (m_renderFeatureCompositorPtr == nullptr || debugName == nullptr) {
        return;
    }
    m_renderFeatureCompositorPtr->UnregisterStageFeature(CompositorStage::PostOpaque, debugName);
}

// Registers a custom pass executed after transparent geometry.
bool Core::AddPostTransparentPass(const char* debugName, ProjectScenePassCallback callback, std::int32_t priority)
{
    if (m_renderFeatureCompositorPtr == nullptr) {
        GTE_LOG_WARNING("Core", "AddPostTransparentPass('" + std::string(debugName != nullptr ? debugName : "<null>")
            + "') failed - no RenderFeatureCompositor registered.");
        return false;
    }
    if (!ValidateBuiltInPassName("AddPostTransparentPass", debugName)) {
        return false;
    }

    const bool registered = m_renderFeatureCompositorPtr->RegisterStageFeature(
        CompositorStage::PostTransparent, debugName, priority, std::move(callback), RenderFeatureOwner::Project);
    if (registered) {
        ProjectAssemblyRegistrationLedger::Instance().RecordPostTransparentFeature(debugName);
    }
    return registered;
}

// Unregisters a post-transparent pass.
void Core::RemovePostTransparentPass(const char* debugName)
{
    if (m_renderFeatureCompositorPtr == nullptr || debugName == nullptr) {
        return;
    }
    m_renderFeatureCompositorPtr->UnregisterStageFeature(CompositorStage::PostTransparent, debugName);
}

// Registers a post-composite screen pass with an auto-incremented priority if none is specified.
bool Core::AddScreenPostProcessPass(const char* debugName, ProjectRenderFeatureCallback callback,
    RenderFeatureBlendMode blendMode, std::optional<std::int32_t> priority)
{
    static std::int32_t s_autoScreenPostProcessPassPriorityCounter = 0;
    const std::int32_t resolvedPriority = priority.has_value()
        ? *priority
        : NextAutoScreenPostProcessPassPriority(s_autoScreenPostProcessPassPriorityCounter);
    return RegisterProjectRenderFeature(
        debugName, RenderFeatureStage::PostComposite, blendMode, resolvedPriority, std::move(callback));
}

// Registers built-in engine passes for a specific frame event.
bool Core::RegisterRenderPass(FrameEvent event, const char* debugName, std::int32_t priority,
    UniversalRenderPassAction action, RenderFeatureBlendMode blendMode)
{
    if (m_renderFeatureCompositorPtr == nullptr) {
        GTE_LOG_WARNING("Core", "RegisterRenderPass('" + std::string(debugName != nullptr ? debugName : "<null>")
            + "') failed - no RenderFeatureCompositor registered.");
        return false;
    }

    switch (event) {
    case FrameEvent::PreOpaque:
        if (!ValidateBuiltInPassName("RegisterRenderPass", debugName)) {
            return false;
        }
        return m_renderFeatureCompositorPtr->RegisterStageFeature(CompositorStage::PreOpaque, debugName, priority,
            ProjectPreOpaqueCallback(
                [action](rg::RenderGraphBuilder& builder, rg::RenderPassBlackboard& blackboard, rg::RenderViewId view) {
                    action(builder, blackboard, view, rg::TextureHandle{}, Extent2D{}, ScenePassReadHandles{},
                        RenderFeatureCameraData{});
                }),
            RenderFeatureOwner::Engine);

    case FrameEvent::Opaque:
    case FrameEvent::PostOpaque:
    case FrameEvent::PostTransparent: {
        if (!ValidateBuiltInPassName("RegisterRenderPass", debugName)) {
            return false;
        }
        const CompositorStage stage = (event == FrameEvent::Opaque)     ? CompositorStage::Opaque
            : (event == FrameEvent::PostOpaque)                         ? CompositorStage::PostOpaque
                                                                        : CompositorStage::PostTransparent;
        return m_renderFeatureCompositorPtr->RegisterStageFeature(stage, debugName, priority,
            ProjectScenePassCallback([action](rg::RenderGraphBuilder& builder, rg::RenderPassBlackboard& blackboard,
                                          rg::RenderViewId view, const ScenePassReadHandles& currentViewHandles) {
                action(builder, blackboard, view, rg::TextureHandle{}, Extent2D{}, currentViewHandles,
                    RenderFeatureCameraData{});
            }),
            RenderFeatureOwner::Engine);
    }

    case FrameEvent::PostComposite:
    case FrameEvent::PreUI: {
        const RenderFeatureStage stage =
            (event == FrameEvent::PostComposite) ? RenderFeatureStage::PostComposite : RenderFeatureStage::PreUI;
        const std::optional<GtePluginRenderFeatureDescriptor> descriptor =
            BuildValidatedRenderFeatureDescriptor("RegisterRenderPass", debugName, stage, blendMode, priority);
        if (!descriptor.has_value()) {
            return false;
        }
        return m_renderFeatureCompositorPtr->RegisterBuiltInFeature(*descriptor, std::move(action));
    }
    }

    GTE_LOG_WARNING("Core", std::string("RegisterRenderPass('") + (debugName != nullptr ? debugName : "<null>")
        + "') failed - unknown FrameEvent value.");
    return false;
}

void Core::Update(const InputFrame& input, float deltaTime)
{
    const bool steppedThisFrame = input.playbackPaused && input.stepRequested;
    m_engineContext.time.Advance(static_cast<double>(deltaTime), input.playbackPaused, steppedThisFrame, kFixedStepSeconds);

    if (input.inputState != nullptr) {
        m_game.Update(m_engineContext, *input.inputState);
    }
}

// Finds active view data by view ID.
const RenderPassViewData* Core::FindViewData(rg::RenderViewId view) const noexcept
{
    for (const RenderPassViewData& viewData : m_currentViewDataThisFrame) {
        if (viewData.id == view) {
            return &viewData;
        }
    }
    return nullptr;
}

// Public accessor for view data.
std::optional<RenderPassViewData> Core::FindRenderPassViewData(rg::RenderViewId view) const noexcept
{
    const RenderPassViewData* viewData = FindViewData(view);
    return viewData != nullptr ? std::optional<RenderPassViewData>(*viewData) : std::nullopt;
}

void Core::ExposeViewOutput(rg::ViewScope scope, RenderTexture& texture, std::string_view name)
{
    for (ExposedViewOutput& existing : m_exposedViewOutputsThisFrame) {
        if (existing.scope == scope) {
            existing.texture = &texture;
            existing.name = name;
            return;
        }
    }
    m_exposedViewOutputsThisFrame.push_back(ExposedViewOutput{ scope, &texture, name });
}

void Core::ClaimEntities(rg::ViewScope scope, std::span<const Entity> entities)
{
    assert(scope != rg::ViewScope::Shared && "ClaimEntities needs a per-view scope.");
    if (scope == rg::ViewScope::Shared) {
        return;
    }
    m_claimedEntities[rg::ViewScopeIndex(scope)].Insert(entities);
}

bool Core::SetSceneDrawCull(rg::ViewScope scope, const Mat4& viewProjection, float paddingFraction)
{
    if (!SceneCullAllowed(ClusterClaimContext{ scope, m_captureArmedThisFrame })
        || !TryExtractFrustumPlanes(viewProjection).has_value()) {
        return false;
    }
    m_sceneCullThisFrame[rg::ViewScopeIndex(scope)] =
        SceneCullRequest{ viewProjection, SanitizeCullPadding(paddingFraction), nullptr };
    return true;
}

SceneCullCounts Core::LastSceneDrawCullCounts(rg::ViewScope scope, std::uint64_t currentFrameIndex) const noexcept
{
    if (scope == rg::ViewScope::Shared) {
        return {};
    }
    const SceneCullResult& last = m_sceneCullCountsLast[rg::ViewScopeIndex(scope)];
    return last.valid && SceneCullCountsFresh(last.frameIndex, currentFrameIndex) ? last.counts : SceneCullCounts{};
}

std::optional<fx::View> Core::PrimaryCameraView(float fallbackAspect)
{
    Registry& registry = m_game.GetRegistry();
    // Without an active camera the matrix is identity, which must never pass as a camera.
    if (!RenderSystem::HasActiveCamera(registry)) {
        return std::nullopt;
    }

    fx::View view;
    view.scope = rg::ViewScope::Primary;
    const rg::RenderViewId primaryId = rg::RenderViewId::Named(kPrimaryViewName);
    if (const RenderPassViewData* data = FindViewData(primaryId)) {
        const RenderFeatureCameraData camera = ResolveRenderFeatureCameraData(primaryId, data);
        if (data->renderTexture != nullptr) {
            view.extent = data->renderTexture->Extent();
        }
        view.eye = camera.eyeWorldPosition;
        view.viewProjection = data->viewProjection;
        view.invViewProjection = camera.invViewProjection;
        view.invViewProjectionValid = camera.invViewProjectionValid;
    } else {
        view.eye = RenderSystem::ResolveActiveCameraWorldPosition(registry);
        view.viewProjection = RenderSystem::ResolveActiveCameraViewProjection(
            registry, PrimaryAspectFallback(m_lastPrimaryAspect, fallbackAspect));
    }
    if (!IsFiniteMatrix(view.viewProjection)) {
        return std::nullopt;
    }
    return view;
}

const Mesh* Core::ResolveClusterMesh(fx::MeshToken token)
{
    return m_game.GetRenderSystem().TryGetMesh(MeshHandle{ token.index, token.generation });
}

bool Core::EnsureClusterTable(fx::MeshToken token, std::uint64_t frameIndex)
{
    Mesh* mesh = m_game.GetRenderSystem().TryGetMesh(MeshHandle{ token.index, token.generation });
    if (mesh == nullptr) {
        return false;
    }
    if (mesh->HasClusters()) {
        return true;
    }
    const auto known = m_synthesisBackoff.find(token);
    if (known != m_synthesisBackoff.end() && !SynthesisDue(known->second, frameIndex)) {
        return false;
    }

    std::string why;
    try {
        LocalBoundsView bounds;
        if (mesh->LocalBounds().has_value()) {
            const AABB& box = *mesh->LocalBounds();
            bounds = LocalBoundsView{ true, { box.min.x, box.min.y, box.min.z }, { box.max.x, box.max.y, box.max.z } };
        }
        const std::size_t indexCount = mesh->HasIndexBuffer() ? mesh->IndexCount() : 0;
        const std::optional<MeshCluster> cluster = SynthesizeSingleCluster(bounds, indexCount, &why);
        if (cluster.has_value()) {
            // The upload waits for the GPU; declaring happens outside any pass execution, so that is legal.
            GTE_PROFILE_MARKER("Periodic.ClusterTableSynthesis");
            mesh->SetClusterTable(rg::GpuBuffer::CreateWithData(m_renderer.Device(), &*cluster, sizeof(MeshCluster),
                                      "MeshClusterTable", rg::GpuBufferUse::Storage),
                1, ClusterTableBounds(std::span<const MeshCluster>(&*cluster, 1)));
            m_synthesisBackoff.erase(token);
            return true;
        }
    } catch (const std::exception& error) {
        why = error.what();
    }

    SynthesisBackoff backoff = known != m_synthesisBackoff.end() ? known->second : SynthesisBackoff{};
    backoff = NoteSynthesisFailure(backoff, frameIndex);
    m_synthesisBackoff.insert_or_assign(token, backoff);
    GTE_LOG_WARNING("Core", "Cluster table synthesis failed (" + why + "); the mesh draws on the CPU path.");
    return false;
}

fx::EntityExclusion Core::ReserveExclusion()
{
    if (m_exclusionSetsUsed >= m_exclusionSets.size()) {
        return {};
    }
    return fx::EntityExclusion{ &m_exclusionSets[m_exclusionSetsUsed++] };
}

void Core::FillExclusion(fx::EntityExclusion exclusion, std::span<const Entity> entities)
{
    for (std::size_t i = 0; i < m_exclusionSetsUsed; ++i) {
        if (exclusion.handle == &m_exclusionSets[i]) {
            m_exclusionSets[i].Insert(entities);
            return;
        }
    }
}

namespace {

// Candidates handled by one bounds job.
constexpr std::size_t kBoundsChunkSize = 4096;

AABB NeverRejectedBox() noexcept
{
    constexpr float kHuge = std::numeric_limits<float>::max();
    return AABB{ Vec3(kHuge, kHuge, kHuge), Vec3(-kHuge, -kHuge, -kHuge) };
}

// Union of the usable boxes of one chunk; `usable` is false when any box of the chunk is not.
struct BoundsChunkResult {
    AABB box = NeverRejectedBox();
    bool usable = true;
};

// Input and output of the bounds jobs. Each job writes only its own chunk's slice, so nothing is shared for writing.
struct BoundsBuild {
    std::span<const fx::ClusterInstance> candidates;
    RenderSystem* renderSystem = nullptr;
    AABB* bounds = nullptr;
    Entity* entities = nullptr;
    BoundsChunkResult* chunks = nullptr;
};

void BuildBoundsChunk(const BoundsBuild& build, std::size_t chunk) noexcept
{
    const AABB neverRejected = NeverRejectedBox();
    const std::size_t first = chunk * kBoundsChunkSize;
    const std::size_t last = std::min(build.candidates.size(), first + kBoundsChunkSize);
    BoundsChunkResult result;
    fx::MeshToken lastToken;
    bool haveLast = false;
    const AABB* local = nullptr;
    for (std::size_t i = first; i < last; ++i) {
        const fx::ClusterInstance& instance = build.candidates[i];
        build.entities[i] = instance.entity;
        if (!haveLast || instance.mesh != lastToken) {
            lastToken = instance.mesh;
            haveLast = true;
            const Mesh* mesh =
                build.renderSystem->TryGetMesh(MeshHandle{ instance.mesh.index, instance.mesh.generation });
            local = (mesh != nullptr && mesh->ClusterBounds().has_value()) ? &*mesh->ClusterBounds() : nullptr;
        }
        AABB box = neverRejected;
        bool usable = false;
        if (local != nullptr) {
            const AABB world = TransformAABB(*local, instance.world);
            usable = fx::IsUsableInstanceBox(world);
            if (usable) {
                box = world;
            }
        }
        build.bounds[i] = box;
        if (!usable) {
            result.usable = false;
            continue;
        }
        result.box.min = Vec3(std::min(result.box.min.x, box.min.x), std::min(result.box.min.y, box.min.y),
            std::min(result.box.min.z, box.min.z));
        result.box.max = Vec3(std::max(result.box.max.x, box.max.x), std::max(result.box.max.y, box.max.y),
            std::max(result.box.max.z, box.max.z));
    }
    build.chunks[chunk] = result;
}

void RunBoundsBatch(std::uint32_t beginChunk, std::uint32_t endChunk, void* payload)
{
    const BoundsBuild& build = *static_cast<const BoundsBuild*>(payload);
    for (std::uint32_t chunk = beginChunk; chunk < endChunk; ++chunk) {
        BuildBoundsChunk(build, chunk);
    }
}

} // namespace

void Core::BuildClusterBounds()
{
    if (m_clusterBoundsBuilt) {
        return;
    }
    GTE_PROFILE_SCOPE("ClusterPart.Bounds");
    const std::size_t count = m_clusterCandidates.size();
    const std::size_t chunkCount = (count + kBoundsChunkSize - 1) / kBoundsChunkSize;
    // Every slot is rewritten below, so the arrays are only resized, never cleared.
    m_clusterCandidateBounds.resize(count);
    m_clusterCandidateEntities.resize(count);
    std::vector<BoundsChunkResult> chunks(chunkCount);

    BoundsBuild build;
    build.candidates = m_clusterCandidates;
    build.renderSystem = &m_game.GetRenderSystem();
    build.bounds = m_clusterCandidateBounds.data();
    build.entities = m_clusterCandidateEntities.data();
    build.chunks = chunks.data();
    if (chunkCount == 1) {
        BuildBoundsChunk(build, 0);
    } else if (chunkCount > 1) {
        Jobs::JobHandle handle;
        try {
            Jobs::Dispatch(&RunBoundsBatch, static_cast<std::uint32_t>(chunkCount), &build, handle, 1);
        } catch (...) {
            Jobs::JobSystem::Instance().WaitForJobs(handle); // Scheduled jobs still read `build`.
            throw;
        }
        GTE_PROFILE_BLOCKED("Wait.Workers");
        Jobs::JobSystem::Instance().WaitForJobs(handle);
    }

    AABB all = NeverRejectedBox();
    bool bounded = count > 0;
    for (const BoundsChunkResult& chunk : chunks) {
        if (!chunk.usable) {
            bounded = false;
            break;
        }
        all.min = Vec3(std::min(all.min.x, chunk.box.min.x), std::min(all.min.y, chunk.box.min.y),
            std::min(all.min.z, chunk.box.min.z));
        all.max = Vec3(std::max(all.max.x, chunk.box.max.x), std::max(all.max.y, chunk.box.max.y),
            std::max(all.max.z, chunk.box.max.z));
    }
    m_clusterCandidateUnion = bounded ? fx::ClusterPartBox{ all, true } : fx::ClusterPartBox{};
    m_clusterBoundsBuilt = true;
}

std::span<const AABB> Core::ClusterCandidateBounds()
{
    BuildClusterBounds();
    return m_clusterCandidateBounds;
}

std::span<const Entity> Core::ClusterCandidateEntities()
{
    BuildClusterBounds();
    return m_clusterCandidateEntities;
}

const fx::ClusterPartBox& Core::ClusterCandidateUnion()
{
    BuildClusterBounds();
    return m_clusterCandidateUnion;
}

void Core::BuildClusterCandidates()
{
    m_clusterCandidates.clear();
    m_clusterCandidateUnion = fx::ClusterPartBox{};
    m_clusterBoundsBuilt = false;
    m_clusterRejections = fx::ClusterRejectCounts{};
    if (!ClusterPathSupported(m_renderer.IndirectDrawCaps()) || m_renderFeatureCompositorPtr == nullptr) {
        return;
    }
    // Only enabled features of the stages that can declare a cluster chain read the list.
    const auto& opaqueFeatures = m_renderFeatureCompositorPtr->StageFeaturesInPriorityOrder<CompositorStage::Opaque>();
    const auto& preOpaqueFeatures = m_renderFeatureCompositorPtr->StageFeaturesInPriorityOrder<CompositorStage::PreOpaque>();
    const bool anyReader =
        std::any_of(opaqueFeatures.begin(), opaqueFeatures.end(),
            [this](const SceneStageEntry& entry) {
                return entry.enabledOverride && !m_faultedOpaqueFeatures.contains(entry.name);
            })
        || std::any_of(preOpaqueFeatures.begin(), preOpaqueFeatures.end(),
            [](const PreOpaqueEntry& entry) { return entry.enabledOverride; });
    if (!anyReader) {
        return;
    }

    std::unordered_set<BufferObjectHandle> gpuSkinnedOutputs;
    gpuSkinnedOutputs.reserve(m_gpuSkinningRequestsThisFrame.size());
    for (const AnimationSystem::GpuSkinningDispatchRequest& request : m_gpuSkinningRequestsThisFrame) {
        gpuSkinnedOutputs.insert(request.dispatch.output->Handle());
    }

    RenderSystem& renderSystem = m_game.GetRenderSystem();
    const std::vector<DrawCommand> commands = RenderSystem::CollectRenderables(m_game.GetRegistry());
    std::vector<ClusterCandidateInput> inputs;
    inputs.reserve(commands.size());
    for (const DrawCommand& command : commands) {
        const Mesh* mesh = renderSystem.TryGetMesh(command.mesh);
        const Pipeline* pipeline = renderSystem.TryGetPipeline(command.pipeline);
        if (mesh == nullptr || pipeline == nullptr) {
            continue;
        }
        ClusterCandidateInput input;
        input.entity = command.entity;
        input.mesh = fx::MeshToken{ command.mesh.index, command.mesh.generation };
        input.material = fx::MaterialToken{ command.texture.index, command.texture.generation };
        input.world = command.model;
        input.meshHasClusters = mesh->HasClusters();
        input.meshSynthesizable = mesh->HasIndexBuffer() && mesh->IndexCount() > 0 && mesh->IndexCount() % 3 == 0
            && mesh->LocalBounds().has_value();
        input.meshHasIndexBuffer = mesh->HasIndexBuffer();
        input.layout = pipeline->VertexLayoutKind();
        input.gpuSkinned = gpuSkinnedOutputs.contains(mesh->VertexBufferHandle());
        inputs.push_back(input);
    }
    m_clusterCandidates = SelectClusterCandidates(inputs, true, &m_clusterRejections);
}

DescriptorSetHandle Core::ResolveClusterMaterialSet(fx::MaterialToken token)
{
    return m_game.GetRenderSystem().MaterialSetOf(MaterialTextureHandle{ token.index, token.generation });
}

const Core::ExposedViewOutput* Core::FindExposedViewOutput(rg::ViewScope scope) const noexcept
{
    for (const ExposedViewOutput& output : m_exposedViewOutputsThisFrame) {
        if (output.scope == scope) {
            return &output;
        }
    }
    return nullptr;
}

// Resolves the target handle and extent of the active view.
std::optional<Core::PluginRenderFeatureTargetInfo> Core::FindPluginRenderFeatureTarget(
    const rg::RenderPassFrameContext& frame)
{
    const RenderPassViewData* viewData = FindViewData(frame.currentView);
    if (viewData == nullptr) {
        return std::nullopt;
    }

    PluginRenderFeatureTargetInfo info;
    info.target = viewData->colorTarget;
    info.extent = viewData->renderTexture != nullptr ? viewData->renderTexture->Extent() : Extent2D{};
    return info;
}

// Registers all standard offscreen passes into the pipeline.
void Core::RegisterOffscreenRenderPipelineProviders()
{
    m_offscreenRenderPipeline.SetLegacyViewScopeTranslator(&TranslateLegacyViewScope);
    m_offscreenRenderPipeline.SetPassToggleRegistry(&m_renderPassToggleRegistry);
    m_renderGraph.SetRenderPassToggleRegistry(&m_renderPassToggleRegistry);

    // Compute passes for GPU skinning: one shared pass name, one descriptor set per output group.
    m_offscreenRenderPipeline.Register("GpuSkinning", rg::ProviderScope::Once,
        [this](const rg::RenderPassFrameContext& frame, std::vector<rg::RenderPassDesc>&) {
            GpuSkinningPipelines& pipelines = m_game.GetGpuSkinningPipelines();

            if (!m_renderPassToggleRegistry.IsEnabled("GpuSkinning")) {
                return;
            }

            const rg::RenderFeatureScope scope(frame.builder, "GPU Skinning");
            for (std::size_t i = 0; i < m_gpuSkinningRequestsThisFrame.size(); ++i) {
                GpuSkinningDispatch dispatch = m_gpuSkinningRequestsThisFrame[i].dispatch;
                dispatch.outputHandle = m_gpuSkinningHandlesThisFrame[i];
                pipelines.Declare(frame.builder, dispatch, rg::ViewScope::Shared, rg::RenderPassEvent::PreOpaques);
            }

            frame.blackboard.Publish<std::vector<rg::BufferHandle>>(
                kGpuSkinningOutputsKey, m_gpuSkinningHandlesThisFrame);
        });

    // Clears color and depth targets at the start of each active view.
    m_offscreenRenderPipeline.Register("ClearViewTarget", rg::ProviderScope::PerActiveView,
        [this](const rg::RenderPassFrameContext& frame, std::vector<rg::RenderPassDesc>& out) {
            const RenderPassViewData* viewData = FindViewData(frame.currentView);
            if (viewData == nullptr) {
                return;
            }

            const rg::TextureHandle viewTarget = viewData->colorTarget;

            // Adding viewTarget to outputs keeps the attachment clear alive during graph compilation.
            frame.finalTextureOutputs.push_back(viewTarget);

            rg::RenderPassDesc desc;
            desc.debugName = "ClearViewTarget";
            desc.kind = rg::PassKind::Graphics;
            desc.order = rg::RenderPassEvent::BeforeEverything;
            desc.view = frame.currentView;
            desc.legacyCategory = rg::RenderPassCategory::General;
            desc.owningFeatureName = "Scene Rendering";
            desc.setup = [viewTarget](rg::RenderGraphBuilder::PassBuilder& pass) {
                pass.WriteColorAttachment(viewTarget, kSceneClearColor);
                pass.WriteDepthStencilAttachment(viewTarget, kGameClearDepth);
            };
            // No execute body: attachment load-op performs the clear.
            out.push_back(std::move(desc));
        });

    // Custom feature passes executed before opaque geometry.
    m_offscreenRenderPipeline.Register("PreOpaqueFeatures", rg::ProviderScope::PerActiveView,
        [this](const rg::RenderPassFrameContext& frame, std::vector<rg::RenderPassDesc>&) {
            if (m_renderFeatureCompositorPtr == nullptr) {
                return;
            }

            const rg::ViewScope viewScope = TranslateLegacyViewScope(frame.currentView);
            for (const PreOpaqueEntry& entry :
                m_renderFeatureCompositorPtr->StageFeaturesInPriorityOrder<CompositorStage::PreOpaque>()) {
                if (!RunsInScope(entry.enabledOverride, entry.scopes, viewScope)) {
                    continue;
                }

                const std::size_t before = frame.builder.DeclaredPassCount();
                const rg::RenderFeatureScope scope(frame.builder, entry.name);
                GTE_PROFILE_SCOPE_ID(FeatureScopeName(entry.name));
                entry.callback(frame.builder, frame.blackboard, frame.currentView);
                const std::size_t after = frame.builder.DeclaredPassCount();

                // Validate that all declared passes use RenderPassEvent::PreOpaques.
                const std::vector<std::size_t> violations =
                    FindPassesNotTaggedPreOpaque(frame.builder, before, after);
                for (const std::size_t index : violations) {
                    const rg::RenderPassEvent actual = frame.builder.PassEventAt(index);
                    GTE_LOG_WARNING("RenderFeatureCompositor",
                        "PreOpaque feature '" + entry.name + "' declared a pass tagged RenderPassEvent::"
                        + rg::ToString(actual) + " instead of REQUIRED RenderPassEvent::PreOpaques.");
                    assert(false && "PreOpaque pass not tagged RenderPassEvent::PreOpaques.");
                }
            }
        });

    // Standard opaque geometry pass.
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
            const bool isPrimaryView = (frame.currentView == rg::RenderViewId::Named(kPrimaryViewName));
            const Mat4 viewProjectionOverride = viewData->viewProjection;
            IFrameDebuggerCaptureRecorder* frameDebuggerCapture =
                isPrimaryView ? m_currentFrameDebuggerCaptureForOffscreenPipeline : nullptr;

            std::array<std::optional<rg::TextureHandle>, kSceneServiceSlotCount> publishedServiceSlots{};
            for (std::uint32_t slotIndex = 0; slotIndex < kSceneServiceSlotCount; ++slotIndex) {
                publishedServiceSlots[slotIndex] = frame.blackboard.Fetch<rg::TextureHandle>(
                    SceneServiceBlackboardKey(slotIndex, frame.currentView));
            }
            const rg::RenderViewId currentViewForServices = frame.currentView;

            rg::RenderPassDesc desc;
            desc.debugName = "RenderOpaque";
            desc.kind = rg::PassKind::Graphics;
            desc.order = rg::RenderPassEvent::Opaques;
            desc.view = frame.currentView;
            desc.legacyCategory = rg::RenderPassCategory::General;
            desc.owningFeatureName = "Scene Rendering";
            desc.setup = [viewTarget, gpuSkinningBuffers, publishedServiceSlots](rg::RenderGraphBuilder::PassBuilder& pass) {
                pass.WriteColorAttachment(viewTarget);
                pass.WriteDepthStencilAttachment(viewTarget);
                DeclareGpuSkinningReads(pass, gpuSkinningBuffers);
                for (std::uint32_t slotIndex = 0; slotIndex < kSceneServiceSlotCount; ++slotIndex) {
                    if (publishedServiceSlots[slotIndex].has_value()) {
                        pass.ReadTexture(*publishedServiceSlots[slotIndex], rg::ResourceAccess::ShaderRead,
                            SceneServiceSlotIsDepthResource(slotIndex));
                    }
                }
            };
            const std::size_t claimScope = rg::ViewScopeIndex(TranslateLegacyViewScope(frame.currentView));
            const std::uint64_t frameIndex = frame.builder.FrameIndex();
            desc.execute = [this, aspectWidthOverHeight, isPrimaryView, viewProjectionOverride, frameDebuggerCapture,
                                publishedServiceSlots, currentViewForServices, claimScope, frameIndex](rg::PassContext& ctx) {
                std::array<SceneServicesDescriptorSet::ResolvedSlot, kSceneServiceSlotCount> resolvedServiceSlots{};
                for (std::uint32_t slotIndex = 0; slotIndex < kSceneServiceSlotCount; ++slotIndex) {
                    if (publishedServiceSlots[slotIndex].has_value()) {
                        if (SceneServiceSlotIsDepthResource(slotIndex)) {
                            const rg::PassContext::ResolvedDepthTexture resolved =
                                ctx.resolveDepthTexture(*publishedServiceSlots[slotIndex]);
                            resolvedServiceSlots[slotIndex].view = resolved.view;
                            resolvedServiceSlots[slotIndex].sampler = resolved.sampler;
                        } else {
                            const rg::PassContext::ResolvedTexture resolved =
                                ctx.resolveReadTexture(*publishedServiceSlots[slotIndex]);
                            resolvedServiceSlots[slotIndex].view = resolved.view;
                            resolvedServiceSlots[slotIndex].sampler = resolved.sampler;
                        }
                    }
                }
                const DescriptorSetHandle sceneServicesSet =
                    m_sceneServicesDescriptorSet.Rewrite(currentViewForServices, resolvedServiceSlots);
                // Read at execute time: chains declared after this pass fill the set during declaration.
                const EntityMarkSet& claimed = m_claimedEntities[claimScope];
                SceneCullCounts cullCounts;
                std::optional<SceneCullRequest> cull = m_sceneCullThisFrame[claimScope];
                if (cull.has_value()) {
                    cull->counts = &cullCounts;
                }
                if (isPrimaryView) {
                    rg::CommandBuffer cmd = ctx.Cmd();
                    m_game.Render(m_renderer, aspectWidthOverHeight, nullptr, frameDebuggerCapture, std::nullopt,
                        claimed, sceneServicesSet, &cmd, cull);
                } else {
                    const rg::SceneRecording recording(ctx);
                    m_game.Render(m_renderer, aspectWidthOverHeight, &viewProjectionOverride, nullptr, std::nullopt,
                        claimed, sceneServicesSet, nullptr, cull);
                }
                m_sceneCullCountsLast[claimScope] = SceneCullResult{ cullCounts, frameIndex, true };
            };
            out.push_back(std::move(desc));
        });

    // Feature passes that draw into the view's color and depth right after the opaque geometry.
    m_offscreenRenderPipeline.Register("OpaqueFeatures", rg::ProviderScope::PerActiveView,
        [this](const rg::RenderPassFrameContext& frame, std::vector<rg::RenderPassDesc>&) {
            if (m_renderFeatureCompositorPtr == nullptr) {
                return;
            }
            // Passes here write the target, so no sampleable depth is needed.
            const RenderPassViewData* viewData = FindViewData(frame.currentView);
            if (viewData == nullptr || viewData->renderTexture == nullptr) {
                return;
            }
            ScenePassReadHandles handles;
            handles.colorHandle = viewData->colorTarget;
            handles.depthHandle = viewData->colorTarget;

            const auto logOnce = [this](const std::string& message) {
                if (rg::BoundedOnceSet<64>::ShouldLog(m_frameFailureOnce.Note(message))) {
                    GTE_LOG_ERROR("RenderFeatureCompositor", message);
                }
            };

            const auto& opaqueEntries = m_renderFeatureCompositorPtr->StageFeaturesInPriorityOrder<CompositorStage::Opaque>();
            // A feature that was unregistered is forgiven, so a re-registered one runs again.
            std::erase_if(m_faultedOpaqueFeatures, [&](const std::string& name) {
                return std::none_of(opaqueEntries.begin(), opaqueEntries.end(),
                    [&](const SceneStageEntry& entry) { return entry.name == name; });
            });

            const rg::ViewScope viewScope = TranslateLegacyViewScope(frame.currentView);
            for (const SceneStageEntry& entry : opaqueEntries) {
                if (!RunsInScope(entry.enabledOverride, entry.scopes, viewScope)
                    || m_faultedOpaqueFeatures.contains(entry.name)) {
                    continue;
                }

                const std::size_t before = frame.builder.DeclaredPassCount();
                try {
                    const rg::RenderFeatureScope scope(frame.builder, entry.name);
                    GTE_PROFILE_SCOPE_ID(FeatureScopeName(entry.name));
                    entry.callback(frame.builder, frame.blackboard, frame.currentView, handles);
                } catch (const std::exception& e) {
                    logOnce("Opaque feature '" + entry.name + "' threw: " + e.what());
                } catch (...) {
                    logOnce("Opaque feature '" + entry.name + "' threw an unknown exception.");
                }
                const std::size_t after = frame.builder.DeclaredPassCount();

                // Passes already declared stay; the feature just stops running.
                const std::vector<std::size_t> tagViolations =
                    FindPassesNotTaggedScenePass(frame.builder, before, after, rg::RenderPassEvent::Opaques);
                if (!tagViolations.empty()) {
                    const rg::RenderPassEvent actual = frame.builder.PassEventAt(tagViolations.front());
                    m_faultedOpaqueFeatures.insert(entry.name);
                    GTE_LOG_WARNING("RenderFeatureCompositor",
                        "Opaque feature '" + entry.name + "' declared a pass tagged RenderPassEvent::"
                        + rg::ToString(actual) + " instead of REQUIRED RenderPassEvent::Opaques; it is disabled.");
                }
            }
        },
        rg::ProviderTiming::AfterDeferredPasses);

    // Transparent geometry and scene overlays.
    m_offscreenRenderPipeline.Register("RenderTransparent", rg::ProviderScope::PerActiveView,
        [this](const rg::RenderPassFrameContext& frame, std::vector<rg::RenderPassDesc>& out) {
            const RenderPassViewData* viewData = FindViewData(frame.currentView);
            if (viewData == nullptr) {
                return;
            }

            if (!viewData->recordSceneOverlay) {
                return;
            }

            const rg::TextureHandle viewTarget = viewData->colorTarget;
            const Mat4 viewProjection = viewData->viewProjection;
            const std::function<void(const rg::RecordingContext&, const Mat4&)> recordSceneOverlay =
                viewData->recordSceneOverlay;

            rg::RenderPassDesc desc;
            desc.debugName = "RenderTransparent";
            desc.kind = rg::PassKind::Graphics;
            desc.order = rg::RenderPassEvent::Transparents;
            desc.view = frame.currentView;
            desc.legacyCategory = rg::RenderPassCategory::General;
            desc.owningFeatureName = "Scene Rendering";
            desc.setup = [viewTarget](rg::RenderGraphBuilder::PassBuilder& pass) {
                pass.WriteColorAttachment(viewTarget);
                pass.WriteDepthStencilAttachment(viewTarget);
            };
            desc.execute = [recordSceneOverlay, viewProjection](rg::PassContext& ctx) {
                const rg::SceneRecording recording(ctx);
                recordSceneOverlay(ctx.Recording(), viewProjection);
            };
            out.push_back(std::move(desc));
        });

    // Feature passes executed immediately after opaque geometry.
    m_offscreenRenderPipeline.Register("PostOpaqueFeatures", rg::ProviderScope::PerActiveView,
        [this](const rg::RenderPassFrameContext& frame, std::vector<rg::RenderPassDesc>&) {
            if (m_renderFeatureCompositorPtr == nullptr) {
                return;
            }
            const RenderPassViewData* viewData = FindViewData(frame.currentView);
            const std::optional<ScenePassReadHandles> handles =
                ResolveScenePassReadHandles(frame.builder, viewData);
            if (!handles.has_value()) {
                return;
            }

            const rg::ViewScope viewScope = TranslateLegacyViewScope(frame.currentView);
            for (const SceneStageEntry& entry :
                m_renderFeatureCompositorPtr->StageFeaturesInPriorityOrder<CompositorStage::PostOpaque>()) {
                if (!RunsInScope(entry.enabledOverride, entry.scopes, viewScope)) {
                    continue;
                }

                const std::size_t before = frame.builder.DeclaredPassCount();
                const rg::RenderFeatureScope scope(frame.builder, entry.name);
                GTE_PROFILE_SCOPE_ID(FeatureScopeName(entry.name));
                entry.callback(frame.builder, frame.blackboard, frame.currentView, *handles);
                const std::size_t after = frame.builder.DeclaredPassCount();

                const std::vector<std::size_t> tagViolations =
                    FindPassesNotTaggedScenePass(frame.builder, before, after, rg::RenderPassEvent::AfterOpaques);
                for (const std::size_t index : tagViolations) {
                    const rg::RenderPassEvent actual = frame.builder.PassEventAt(index);
                    GTE_LOG_WARNING("RenderFeatureCompositor",
                        "PostOpaque feature '" + entry.name + "' declared a pass tagged RenderPassEvent::"
                        + rg::ToString(actual) + " instead of REQUIRED RenderPassEvent::AfterOpaques.");
                    assert(false && "PostOpaque pass not tagged RenderPassEvent::AfterOpaques.");
                }

                // Kit features declare every sample through named bindings; only raw project callbacks can forget.
                if (entry.owner == RenderFeatureOwner::Project
                    && FindScenePassCallbackMissingReadDeclaration(
                        frame.builder, before, after, handles->colorHandle, handles->depthHandle)) {
                    GTE_LOG_WARNING("RenderFeatureCompositor",
                        "PostOpaque feature '" + entry.name + "' did not declare ReadTexture() usage on handles.");
                    assert(false && "PostOpaque pass missing ReadTexture() declaration.");
                }
            }
        },
        rg::ProviderTiming::AfterDeferredPasses);

    // Feature passes executed immediately after transparent geometry.
    m_offscreenRenderPipeline.Register("PostTransparentFeatures", rg::ProviderScope::PerActiveView,
        [this](const rg::RenderPassFrameContext& frame, std::vector<rg::RenderPassDesc>&) {
            if (m_renderFeatureCompositorPtr == nullptr) {
                return;
            }
            const RenderPassViewData* viewData = FindViewData(frame.currentView);
            const std::optional<ScenePassReadHandles> handles =
                ResolveScenePassReadHandles(frame.builder, viewData);
            if (!handles.has_value()) {
                return;
            }

            const rg::ViewScope viewScope = TranslateLegacyViewScope(frame.currentView);
            for (const SceneStageEntry& entry :
                m_renderFeatureCompositorPtr->StageFeaturesInPriorityOrder<CompositorStage::PostTransparent>()) {
                if (!RunsInScope(entry.enabledOverride, entry.scopes, viewScope)) {
                    continue;
                }

                const std::size_t before = frame.builder.DeclaredPassCount();
                const rg::RenderFeatureScope scope(frame.builder, entry.name);
                GTE_PROFILE_SCOPE_ID(FeatureScopeName(entry.name));
                entry.callback(frame.builder, frame.blackboard, frame.currentView, *handles);
                const std::size_t after = frame.builder.DeclaredPassCount();

                const std::vector<std::size_t> tagViolations = FindPassesNotTaggedScenePass(
                    frame.builder, before, after, rg::RenderPassEvent::AfterTransparents);
                for (const std::size_t index : tagViolations) {
                    const rg::RenderPassEvent actual = frame.builder.PassEventAt(index);
                    GTE_LOG_WARNING("RenderFeatureCompositor",
                        "PostTransparent feature '" + entry.name + "' declared a pass tagged RenderPassEvent::"
                        + rg::ToString(actual) + " instead of REQUIRED RenderPassEvent::AfterTransparents.");
                    assert(false && "PostTransparent pass not tagged RenderPassEvent::AfterTransparents.");
                }

                if (entry.owner == RenderFeatureOwner::Project
                    && FindScenePassCallbackMissingReadDeclaration(
                        frame.builder, before, after, handles->colorHandle, handles->depthHandle)) {
                    GTE_LOG_WARNING("RenderFeatureCompositor",
                        "PostTransparent feature '" + entry.name + "' did not declare ReadTexture() usage on handles.");
                    assert(false && "PostTransparent pass missing ReadTexture() declaration.");
                }
            }
        },
        rg::ProviderTiming::AfterDeferredPasses);

    // Dispatches custom passes from registered capability orchestrators.
    m_offscreenRenderPipeline.Register("PluginRenderFeatures", rg::ProviderScope::PerActiveView,
        [this](const rg::RenderPassFrameContext& frame, std::vector<rg::RenderPassDesc>& out) {
            for (auto& orchestrator : m_capabilityOrchestrators) {
                orchestrator->ContributeRenderGraphPasses(frame, out);
            }
        },
        rg::ProviderTiming::AfterDeferredPasses);
}

// Registers the swapchain presentation passes.
void Core::RegisterPresentRenderPipelineProvider()
{
    m_presentRenderPipeline.SetLegacyViewScopeTranslator(&TranslateLegacyViewScope);
    m_presentRenderPipeline.SetPassToggleRegistry(&m_renderPassToggleRegistry);

    // Copies the engine-owned primary target into the swapchain (the offscreen graph left it sampleable).
    m_presentRenderPipeline.Register("PresentPrimaryView", rg::ProviderScope::Once,
        [this](const rg::RenderPassFrameContext& frame, std::vector<rg::RenderPassDesc>&) {
            if (!m_directPrimaryTargetUsedThisFrame || !m_directPrimaryTarget.has_value()) {
                return;
            }
            const rg::RenderFeatureScope scope(frame.builder, "Present");
            bool copied = false;
            if (rg::ShouldDeclareBuiltInPassThisFrame(&m_renderPassToggleRegistry, "PresentPrimaryView")) {
                RenderTexture& source = *m_directPrimaryTarget;
                const rg::TextureHandle sourceHandle = frame.builder.ImportTexture("PresentSourcePrimaryView",
                    source.Target(), ImageLayout::ShaderReadOnlyOptimal, source.SamplerObjectHandle(),
                    SamplerHandle::Invalid, &source);
                copied = rg::FullscreenPass(frame.builder, m_presentPrimaryViewShader, "PresentPrimaryView",
                    rg::ViewScope::Shared, rg::RenderPassEvent::BeforeEverything)
                             .Bind("sceneColor", sourceHandle)
                             .Target(m_swapchainImageThisFrame, kGameClearColor)
                             .Draw();
            }
            // Without the copy the Present pass must clear the swapchain itself.
            m_directPrimaryTargetUsedThisFrame = copied;
        });

    m_presentRenderPipeline.Register("Present", rg::ProviderScope::Once,
        [this](const rg::RenderPassFrameContext& frame, std::vector<rg::RenderPassDesc>&) {
            const rg::RenderFeatureScope scope(frame.builder, "Present");
            AddPresentPass(frame.builder, m_swapchainImageThisFrame, !m_directPrimaryTargetUsedThisFrame,
                m_recordImGuiThisFrame);
        });
}

// Returns the engine-owned primary target sized to the window, creating it on first use.
RenderTexture* Core::AcquireDirectPrimaryTarget()
{
    if (m_windowWidth <= 0 || m_windowHeight <= 0) {
        return nullptr;
    }
    if (!m_directPrimaryTarget.has_value()) {
        m_directPrimaryTarget.emplace(m_renderer.CreateRenderTexture(m_windowWidth, m_windowHeight,
            m_renderer.ScenePixelFormat(), "DirectPrimaryView", "DirectPrimaryViewDepth",
            /*allowStorageImageAccess=*/true, /*allowDepthSampledAccess=*/true));
        return &*m_directPrimaryTarget;
    }
    const Extent2D extent = m_directPrimaryTarget->Extent();
    if (extent.width != static_cast<std::uint32_t>(m_windowWidth)
        || extent.height != static_cast<std::uint32_t>(m_windowHeight)) {
        m_renderer.ResizeRenderTexture(*m_directPrimaryTarget, m_windowWidth, m_windowHeight);
    }
    return &*m_directPrimaryTarget;
}

const char* Core::ViewResourceName(rg::ViewScope scope) const noexcept
{
    if (m_editorLayer != nullptr) {
        return m_editorLayer->ViewResourceName(scope);
    }
    return scope == rg::ViewScope::Secondary ? kSecondaryViewName : kPrimaryViewName;
}

void Core::BuildFrame()
{
    // Per-frame claims, candidates and exclusions are cleared first so a frame that ends early leaves none behind.
    for (EntityMarkSet& claimed : m_claimedEntities) {
        claimed.Clear();
    }
    for (std::optional<SceneCullRequest>& request : m_sceneCullThisFrame) {
        request.reset();
    }
    m_clusterCandidates.clear();
    m_clusterRejections = fx::ClusterRejectCounts{};
    for (std::size_t i = 0; i < m_exclusionSetsUsed; ++i) {
        m_exclusionSets[i].Clear();
    }
    m_exclusionSetsUsed = 0;
    ++m_exclusionEpoch;
    m_captureArmedThisFrame = false;

    if (FaultFires(FaultKind::MainThreadStall)) {
        std::this_thread::sleep_for(kInjectedStallDuration);
    }
    if (FaultFires(FaultKind::DeviceLost)) {
        m_renderer.InjectDeviceLost();
    }
    if (StopIfDeviceLost()) {
        return;
    }

    RenderTexture* primaryTarget = nullptr;
    RenderTexture* secondaryTarget = nullptr;
    IFrameDebuggerCaptureRecorder* frameDebuggerCapture = nullptr;
    {
        GTE_PROFILE_SCOPE("Render.Prepare");
        const std::vector<MeshHandle> releasedMeshes = m_game.ReleaseUnusedGpuResources(m_renderer);
        if (!releasedMeshes.empty()) {
            GTE_PROFILE_MARKER("Periodic.MeshGpuRelease");
        }
        for (const MeshHandle released : releasedMeshes) {
            ForgetClusterSynthesis(released);
        }

        m_renderGraph.BeginPersistentResourceFrame();

        m_exposedViewOutputsThisFrame.clear();

        // Query active editor render targets; without any, the primary view renders into an engine-owned target.
        primaryTarget = (m_editorLayer != nullptr) ? m_editorLayer->PrimaryViewTarget() : nullptr;
        secondaryTarget = (m_editorLayer != nullptr) ? m_editorLayer->SecondaryViewTarget() : nullptr;
        m_directPrimaryTargetUsedThisFrame = false;
        if (primaryTarget == nullptr && secondaryTarget == nullptr) {
            try {
                primaryTarget = AcquireDirectPrimaryTarget();
            } catch (const std::exception& e) {
                const std::string failure = std::string("Core: failed to acquire the direct primary target: ") + e.what();
                if (rg::BoundedOnceSet<64>::ShouldLog(m_frameFailureOnce.Note(failure))) {
                    GTE_LOG_ERROR("Core", failure);
                }
                primaryTarget = nullptr;
            }
            m_directPrimaryTargetUsedThisFrame = (primaryTarget != nullptr);
        }
        m_primaryTargetThisFrame = primaryTarget;

        frameDebuggerCapture =
            (m_editorLayer != nullptr) ? m_editorLayer->PrepareFrameDebuggerCaptureContext() : nullptr;
        m_captureArmedThisFrame = (frameDebuggerCapture != nullptr);
    }

    // Execute offscreen rendering for the primary and secondary views.
    if (primaryTarget != nullptr || secondaryTarget != nullptr) {
        try {
            GTE_PROFILE_SCOPE("Offscreen");
            m_timingAdapter.SetGraphLabel("Offscreen");
            OffscreenRecording recording = [this]() -> OffscreenRecording {
                GTE_PROFILE_SCOPE("Offscreen.Begin");
                return m_renderer.BeginOffscreenRecording();
            }();
            if (FaultFires(FaultKind::OffscreenThrow)) {
                throw std::runtime_error("injected offscreen fault");
            }
            const rg::RecordingContext offscreenCmd = recording.Context();
            m_renderGraph.Execute(offscreenCmd, rg::ExecuteTimingMode::SynchronousImmediateReadback,
                [&](rg::RenderGraphBuilder& b) {
                    GTE_PROFILE_SCOPE("Offscreen.Declare");
                    {
                        GTE_PROFILE_SCOPE("Offscreen.Collect");
                        m_gpuSkinningRequestsThisFrame = m_game.CollectGpuSkinningDispatchRequests();
                        m_gpuSkinningHandlesThisFrame.clear();
                        m_gpuSkinningHandlesThisFrame.reserve(m_gpuSkinningRequestsThisFrame.size());
                        for (const AnimationSystem::GpuSkinningDispatchRequest& request :
                            m_gpuSkinningRequestsThisFrame) {
                            m_gpuSkinningHandlesThisFrame.push_back(b.Import(*request.dispatch.output, request.name));
                        }

                        // One candidate list for every view of this frame. A failure leaves the list empty (CPU path).
                        try {
                            BuildClusterCandidates();
                        } catch (const std::exception& e) {
                            m_clusterCandidates.clear();
                            m_clusterRejections = fx::ClusterRejectCounts{};
                            const std::string failure = std::string("Core: cluster candidate list failed: ") + e.what();
                            if (rg::BoundedOnceSet<64>::ShouldLog(m_frameFailureOnce.Note(failure))) {
                                GTE_LOG_ERROR("Core", failure);
                            }
                        }
                    }
                    m_offscreenBlackboardThisFrame.BeginFrame();
                    rg::RenderPassFrameContext frame{
                        {}, rg::RenderViewId::Shared(), m_offscreenBlackboardThisFrame, b, {} };

                    m_currentViewDataThisFrame.clear();
                    m_currentFrameDebuggerCaptureForOffscreenPipeline = nullptr;

                    float primaryAspectForReplay = 1.0f;

                    // Set up the primary view.
                    if (primaryTarget != nullptr) {
                        const Extent2D extent = primaryTarget->Extent();
                        const float aspect =
                            AspectRatioOf(static_cast<int>(extent.width), static_cast<int>(extent.height));

                        const Vec3 primaryEyeWorldPosition = RenderSystem::ResolveActiveCameraWorldPosition(m_game.GetRegistry());
                        const Mat4 primaryViewProjection =
                            RenderSystem::ResolveActiveCameraViewProjection(m_game.GetRegistry(), aspect);

                        const rg::TextureHandle h = b.ImportTexture(ViewResourceName(rg::ViewScope::Primary),
                            primaryTarget->Target(), ImageLayout::Undefined, primaryTarget->SamplerObjectHandle(),
                            primaryTarget->DepthSamplerObjectHandle(), primaryTarget);

                        RenderPassViewData primaryViewData;
                        primaryViewData.id = rg::RenderViewId::Named(kPrimaryViewName);
                        primaryViewData.colorTarget = h;
                        primaryViewData.renderTexture = primaryTarget;
                        primaryViewData.aspectWidthOverHeight = aspect;
                        primaryViewData.viewProjection = primaryViewProjection;
                        primaryViewData.eyeWorldPosition = primaryEyeWorldPosition;
                        m_currentViewDataThisFrame.push_back(primaryViewData);

                        frame.activeViews.push_back(rg::RenderViewId::Named(kPrimaryViewName));
                        m_currentFrameDebuggerCaptureForOffscreenPipeline = frameDebuggerCapture;
                        primaryAspectForReplay = aspect;
                        m_lastPrimaryAspect = aspect;
                    }

                    // Set up the secondary view.
                    if (secondaryTarget != nullptr) {
                        const Extent2D extent = secondaryTarget->Extent();
                        const float aspect =
                            AspectRatioOf(static_cast<int>(extent.width), static_cast<int>(extent.height));
                        const Mat4 secondaryViewProjection = m_editorLayer->SecondaryViewProjection(aspect);
                        const Vec3 secondaryEyeWorldPosition = m_editorLayer->SecondaryViewCameraWorldPosition();

                        const rg::TextureHandle h = b.ImportTexture(ViewResourceName(rg::ViewScope::Secondary),
                            secondaryTarget->Target(), ImageLayout::Undefined, secondaryTarget->SamplerObjectHandle(),
                            secondaryTarget->DepthSamplerObjectHandle(), secondaryTarget);

                        RenderPassViewData secondaryViewData;
                        secondaryViewData.id = rg::RenderViewId::Named(kSecondaryViewName);
                        secondaryViewData.colorTarget = h;
                        secondaryViewData.renderTexture = secondaryTarget;
                        secondaryViewData.aspectWidthOverHeight = aspect;
                        secondaryViewData.viewProjection = secondaryViewProjection;
                        secondaryViewData.eyeWorldPosition = secondaryEyeWorldPosition;
                        secondaryViewData.recordSceneOverlay = [this](const rg::RecordingContext& cmd, const Mat4& viewProj) {
                            if (m_editorLayer != nullptr) {
                                m_editorLayer->RenderSceneGrid(cmd, viewProj);
                            }
                        };
                        m_currentViewDataThisFrame.push_back(secondaryViewData);

                        frame.activeViews.push_back(rg::RenderViewId::Named(kSecondaryViewName));
                    }

                    m_offscreenRenderPipeline.DeclareInto(b, frame);

                    const std::optional<rg::FullscreenReplay> viewBackgroundForReplay =
                        m_offscreenBlackboardThisFrame.Fetch<rg::FullscreenReplay>(kViewBackgroundReplayCallbackKey);
#ifndef NDEBUG
                    m_offscreenBlackboardThisFrame.ReportUnusedPublishesIfAny();
#endif

                    // Merge outputs explicitly requested by pass builders.
                    for (const rg::TextureHandle& handle : b.FinalTextureOutputs()) {
                        frame.finalTextureOutputs.push_back(handle);
                    }

                    std::vector<rg::TextureHandle> outputs = std::move(frame.finalTextureOutputs);

                    // Add frame replay passes for debugging if requested.
                    if (primaryTarget != nullptr && frameDebuggerCapture != nullptr && m_editorLayer != nullptr
                        && m_editorLayer->ConsumePendingFrameDebuggerReplayRequest()) {
                        const std::vector<rg::BufferHandle> gpuSkinningBuffersForReplay =
                            m_offscreenBlackboardThisFrame.Fetch<std::vector<rg::BufferHandle>>(kGpuSkinningOutputsKey)
                                .value_or(std::vector<rg::BufferHandle>{});
                        const std::size_t objectCount = m_game.CountRenderablesThisFrame();
                        const std::vector<rg::TextureHandle> replayStepHandles = frameDebuggerCapture->AddReplayPasses(
                            b, m_game, m_renderer, primaryAspectForReplay, objectCount, gpuSkinningBuffersForReplay,
                            viewBackgroundForReplay.value_or(rg::FullscreenReplay{}), *primaryTarget,
                            &m_renderPassToggleRegistry);
                        for (const rg::TextureHandle& replayHandle : replayStepHandles) {
                            outputs.push_back(replayHandle);
                        }
                    }

                    return outputs;
                });

            {
                GTE_PROFILE_SCOPE("Offscreen.FinalizeViews");
                // Finalize textures for external sampling (e.g. editor UI viewport panels). A view output a feature
                // exposed this frame is finalized too and replaces the raw view in the editor.
                const rg::ResourceState shaderReadState = rg::RequiredStateFor(rg::ResourceAccess::ShaderRead, false);
                const auto finalizeView = [&](RenderTexture& target, rg::ViewScope scope) {
                    target.FinalizeForExternalSampling(offscreenCmd);
                    m_renderGraph.NotifyDebugTextureStateOverride(ViewResourceName(scope), shaderReadState);

                    const ExposedViewOutput* exposed = FindExposedViewOutput(scope);
                    if (exposed == nullptr) {
                        return static_cast<RenderTexture*>(nullptr);
                    }
                    exposed->texture->FinalizeForExternalSampling(offscreenCmd);
                    m_renderGraph.NotifyDebugTextureStateOverride(exposed->name, shaderReadState);
                    return exposed->texture;
                };
                if (primaryTarget != nullptr) {
                    RenderTexture* exposed = finalizeView(*primaryTarget, rg::ViewScope::Primary);
                    if (m_editorLayer != nullptr) {
                        m_editorLayer->SetPrimaryViewCompositedTexture(exposed);
                    }
                }
                if (secondaryTarget != nullptr) {
                    RenderTexture* exposed = finalizeView(*secondaryTarget, rg::ViewScope::Secondary);
                    if (m_editorLayer != nullptr) {
                        m_editorLayer->SetSecondaryViewCompositedTexture(exposed);
                    }
                }
            }
            {
                GTE_PROFILE_SCOPE("Offscreen.Submit");
                recording.Submit();
            }
            {
                GTE_PROFILE_SCOPE("Offscreen.Finalize");
                m_renderGraph.FinalizeSynchronousGpuTiming();
                PublishGpuMetrics(primaryTarget != nullptr, secondaryTarget != nullptr);
            }
        } catch (const std::exception& e) {
            // A failure that repeats every frame is logged once per distinct message.
            const std::string failure = std::string("RenderGraph offscreen Execute() failed: ") + e.what();
            if (rg::BoundedOnceSet<64>::ShouldLog(m_frameFailureOnce.Note(failure))) {
                GTE_LOG_ERROR("Core", failure);
            }
            // The target's contents are undefined; present clears the swapchain instead of copying it.
            m_directPrimaryTargetUsedThisFrame = false;
        } catch (...) {
            const std::string failure = "RenderGraph offscreen Execute() failed: unknown exception";
            if (rg::BoundedOnceSet<64>::ShouldLog(m_frameFailureOnce.Note(failure))) {
                GTE_LOG_ERROR("Core", failure);
            }
            m_directPrimaryTargetUsedThisFrame = false;
        }
    } else {
        PublishGpuMetrics(false, false);
    }
}

void Core::PublishGpuMetrics(bool primaryActive, bool secondaryActive)
{
    const rg::RenderGraphSnapshot& snapshot = m_renderGraph.LastSnapshot(rg::ExecuteTimingMode::SynchronousImmediateReadback);
    Profiling::FrameProfiler& profiler = Profiling::FrameProfiler::Instance();
    const bool captureEnabled = profiler.IsCaptureEnabled();
    const bool wantRows = captureEnabled && (primaryActive || secondaryActive);

    if (m_gpuPassRowScratch.capacity() < Profiling::kMaxGpuPassRowsPerFrame) {
        m_gpuPassRowScratch.reserve(Profiling::kMaxGpuPassRowsPerFrame);
    }
    m_gpuRecordScratch.clear();
    m_gpuPassRowScratch.clear();
    bool bucketAdded = false;
    std::uint32_t executed = 0;
    for (const rg::RenderGraphPassSnapshot& pass : snapshot.passesInExecutionOrder) {
        if (pass.isCulled) {
            continue;
        }
        ++executed;
        const GpuPassRecord& record = pass.stats.gpu;
        m_gpuRecordScratch.push_back(record);

        if (!wantRows || (record.flags & kGpuRecordSkipped) != 0
            || m_gpuPassRowScratch.size() >= Profiling::kMaxGpuPassRowsPerFrame) {
            continue;
        }
        // Folded passes share one bucket record; emit it once.
        const bool bucket = (record.flags & kGpuRecordOverflowBucket) != 0;
        if (bucket) {
            if (bucketAdded) {
                continue;
            }
            bucketAdded = true;
        }
        Profiling::GpuPassRow row;
        row.pass = bucket ? Profiling::kInvalidNameId : Profiling::InternName(pass.name.c_str());
        row.feature = pass.owningFeatureName.empty() ? Profiling::kInvalidNameId
                                                     : Profiling::InternName(pass.owningFeatureName.c_str());
        row.barrierMs = record.barrierMs;
        row.workMs = record.workMs;
        row.drawCalls = record.drawCalls;
        row.foldedPasses = bucket ? record.foldedPasses : 0u;
        row.view = ToViewSlot(pass.viewScope);
        row.status = ToSampleStatus(record.state);
        m_gpuPassRowScratch.push_back(row);
    }

    GpuMetricsInput input;
    input.captureEnabled = captureEnabled;
    input.viewActive[static_cast<std::size_t>(Profiling::ViewSlot::Primary)] = primaryActive;
    input.viewActive[static_cast<std::size_t>(Profiling::ViewSlot::Secondary)] = secondaryActive;
    input.declaredPasses = static_cast<std::uint32_t>(snapshot.passesInExecutionOrder.size());
    input.executedPasses = executed;
    input.disabledByToggle = m_offscreenRenderPipeline.DisabledByToggleLastFrame();
    input.droppedFrames = snapshot.droppedGpuFrames;

    profiler.SetGpuMetrics(BuildGpuFrameMetrics(m_gpuRecordScratch, snapshot.gpuReport, input));
    profiler.SetGpuPassRows(m_gpuPassRowScratch);
}

bool Core::StopIfDeviceLost()
{
    if (!m_renderer.DeviceLost()) {
        return false;
    }
    if (!m_lostLogged) {
        m_lostLogged = true;
        GTE_LOG_ERROR("Core", "GPU device lost, rendering stopped");
    }
    return true;
}

void Core::Present()
{
    if (StopIfDeviceLost()) {
        return;
    }
    GTE_PROFILE_SCOPE("Present");
    m_timingAdapter.SetGraphLabel("Present");

    try {
        static_cast<void>(m_renderer.PresentViaRenderGraph(m_renderGraph,
            [&](rg::RenderGraphBuilder& b, rg::TextureHandle swapchainImage) {
                GTE_PROFILE_SCOPE("Present.Declare");
                m_swapchainImageThisFrame = swapchainImage;
                m_recordImGuiThisFrame = m_presentImGuiRecorder;

                rg::RenderPassBlackboard presentBlackboard;
                presentBlackboard.BeginFrame();
                rg::RenderPassFrameContext presentFrame{
                    {}, rg::RenderViewId::Shared(), presentBlackboard, b, {}
                };
                m_presentRenderPipeline.DeclareInto(b, presentFrame);
#ifndef NDEBUG
                presentBlackboard.ReportUnusedPublishesIfAny();
#endif
                return std::vector<rg::TextureHandle>{ swapchainImage };
            }));
    } catch (const std::exception& e) {
        const std::string failure = std::string("RenderGraph Present Execute() failed: ") + e.what();
        if (rg::BoundedOnceSet<64>::ShouldLog(m_frameFailureOnce.Note(failure))) {
            GTE_LOG_ERROR("Core", failure);
        }
    } catch (...) {
        const std::string failure = "RenderGraph Present Execute() failed: unknown exception";
        if (rg::BoundedOnceSet<64>::ShouldLog(m_frameFailureOnce.Note(failure))) {
            GTE_LOG_ERROR("Core", failure);
        }
    }
    TrackPresentedFrame();
}

void Core::TrackPresentedFrame()
{
    Profiling::FrameProfiler& profiler = Profiling::FrameProfiler::Instance();

    // Link only while a profiler frame is open; otherwise the slot is cleared so no late span attaches to the wrong frame.
    const std::uint64_t presented = m_renderer.PresentedFrameCount();
    if (presented != m_lastPresentedFrame) {
        m_lastPresentedFrame = presented;
        const std::optional<std::uint64_t> openFrame = profiler.OpenFrameIndex();
        m_presentedFrameLinks[presented % m_presentedFrameLinks.size()] =
            openFrame.has_value() ? PresentedFrameLink{ presented, *openFrame } : PresentedFrameLink{};
    }

    // A present frame read back now belongs to an earlier profiler frame; only its GPU span is added there.
    const rg::RenderGraphSnapshot& snapshot = m_renderGraph.LastSnapshot(rg::ExecuteTimingMode::PipelinedDeferredReadback);
    const std::uint64_t resolved = snapshot.gpuFrameIndex;
    if (resolved == 0 || resolved == m_lastLateGpuFrame) {
        return;
    }
    m_lastLateGpuFrame = resolved;

    const PresentedFrameLink& link = m_presentedFrameLinks[resolved % m_presentedFrameLinks.size()];
    if (link.presentedFrame != resolved || !snapshot.gpuReport.spanValid) {
        return;
    }
    Profiling::GpuViewMetric span;
    span.status = Profiling::SampleStatus::Present;
    span.milliseconds = static_cast<float>(snapshot.gpuReport.spanMs);
    span.foldedPasses = snapshot.gpuReport.foldedPasses;
    profiler.AttachLateGpuReport(link.profilerFrame, span);
}

} // namespace gte