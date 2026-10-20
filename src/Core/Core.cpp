#include "Core.h"

// Access to editor layer implementation.
#include "../Editor/EditorLayer.h"

#include "../Application/RenderPasses.h"
#include "../Profiling/FrameProfiler.h"
#include "../Profiling/ScopeTimer.h"
#include "../Renderer/GpuSkinning/GpuSkinningPipelines.h"
#include "../Renderer/Culling/CullingPipelines.h"
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
#include "../Renderer/GraphicsHandleRegistry.h"
#include "../Renderer/VulkanFormatInfo.h"
#include "GpuDrivenBatchEntityExclusionLogic.h"
#include "Logging.h"
#include "ScreenPostProcessPassPriorityAssignment.h"
#include "Plugins/IPluginCapabilityOrchestrator.h"
#include "Plugins/RenderFeatureCompositor.h"
#include "Plugins/ProjectAssemblyRegistrationLedger.h"
#include "ViewBackgroundReplayKey.h"

#include <cassert>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <deque>
#include <stdexcept>
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

// Maps renderer GPU timing status to profiling sample status.
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

// Cached pass and resource names for a GPU-driven batch.
struct GpuDrivenBatchNames {
    const char* resetPassName = nullptr;
    const char* cullingPassName = nullptr;
    const char* indirectDrawPassName = nullptr;
    const char* readbackPassName = nullptr;
    const char* inputBufferName = nullptr;
    const char* indirectBufferName = nullptr;
    const char* countBufferName = nullptr;
    const char* readbackBufferName = nullptr;
    const char* displayName = nullptr;
};

// String pool for stable GPU-driven batch names.
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
        const std::string base = "GpuDrivenBatch" + std::to_string(index);
        Entry entry;
        entry.key = key;
        entry.names.resetPassName = Intern(base + " ResetCount");
        entry.names.cullingPassName = Intern(base + " Culling");
        entry.names.indirectDrawPassName = Intern(base + " IndirectDraw");
        entry.names.readbackPassName = Intern(base + " CountReadback");
        entry.names.inputBufferName = Intern(base + ".Input");
        entry.names.indirectBufferName = Intern(base + ".IndirectCommands");
        entry.names.countBufferName = Intern(base + ".VisibleCount");
        entry.names.readbackBufferName = Intern(base + ".VisibleCountReadback");
        entry.names.displayName = Intern(base);
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

    std::deque<std::string> m_storage;
    std::vector<Entry> m_entries;
};

// Global pool of batch names.
GpuDrivenBatchNamePool& BatchNamePool()
{
    static GpuDrivenBatchNamePool pool;
    return pool;
}

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

Core::Core(ISurfaceProvider& surfaceProvider, IHostServices& hostServices)
    : m_renderer(surfaceProvider)
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
}

Core::~Core() = default;

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
// Validates descriptor name length (max 63 characters) and constructs descriptor.
std::optional<GtePluginRenderFeatureDescriptor> BuildValidatedRenderFeatureDescriptor(const char* callerName,
    const char* debugName, RenderFeatureStage stage, RenderFeatureBlendMode blendMode, std::int32_t priority)
{
    if (debugName == nullptr) {
        GTE_LOG_WARNING("Core", std::string(callerName) + "() failed - debugName is null.");
        return std::nullopt;
    }
    if (std::strlen(debugName) > 63) {
        GTE_LOG_WARNING("Core", std::string(callerName) + "('" + debugName
            + "') failed - name exceeds the 63-byte limit; shorten it.");
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
    if (debugName == nullptr) {
        GTE_LOG_WARNING("Core", "AddPreOpaquePass() failed - debugName is null.");
        return false;
    }
    if (std::strlen(debugName) > 63) {
        GTE_LOG_WARNING("Core", "AddPreOpaquePass('" + std::string(debugName)
            + "') failed - name exceeds the 63-byte limit; shorten it.");
        return false;
    }

    const bool registered =
        m_renderFeatureCompositorPtr->RegisterPreOpaqueFeature(debugName, priority, std::move(callback));
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
    m_renderFeatureCompositorPtr->UnregisterPreOpaqueFeature(debugName);
}

// Registers a custom pass executed after opaque geometry.
bool Core::AddPostOpaquePass(const char* debugName, ProjectScenePassCallback callback, std::int32_t priority)
{
    if (m_renderFeatureCompositorPtr == nullptr) {
        GTE_LOG_WARNING("Core", "AddPostOpaquePass('" + std::string(debugName != nullptr ? debugName : "<null>")
            + "') failed - no RenderFeatureCompositor registered.");
        return false;
    }
    if (debugName == nullptr) {
        GTE_LOG_WARNING("Core", "AddPostOpaquePass() failed - debugName is null.");
        return false;
    }
    if (std::strlen(debugName) > 63) {
        GTE_LOG_WARNING("Core", "AddPostOpaquePass('" + std::string(debugName)
            + "') failed - name exceeds the 63-byte limit; shorten it.");
        return false;
    }

    const bool registered =
        m_renderFeatureCompositorPtr->RegisterPostOpaqueFeature(debugName, priority, std::move(callback));
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
    m_renderFeatureCompositorPtr->UnregisterPostOpaqueFeature(debugName);
}

// Registers a custom pass executed after transparent geometry.
bool Core::AddPostTransparentPass(const char* debugName, ProjectScenePassCallback callback, std::int32_t priority)
{
    if (m_renderFeatureCompositorPtr == nullptr) {
        GTE_LOG_WARNING("Core", "AddPostTransparentPass('" + std::string(debugName != nullptr ? debugName : "<null>")
            + "') failed - no RenderFeatureCompositor registered.");
        return false;
    }
    if (debugName == nullptr) {
        GTE_LOG_WARNING("Core", "AddPostTransparentPass() failed - debugName is null.");
        return false;
    }
    if (std::strlen(debugName) > 63) {
        GTE_LOG_WARNING("Core", "AddPostTransparentPass('" + std::string(debugName)
            + "') failed - name exceeds the 63-byte limit; shorten it.");
        return false;
    }

    const bool registered =
        m_renderFeatureCompositorPtr->RegisterPostTransparentFeature(debugName, priority, std::move(callback));
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
    m_renderFeatureCompositorPtr->UnregisterPostTransparentFeature(debugName);
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
    switch (event) {
    case FrameEvent::PreOpaque: {
        if (m_renderFeatureCompositorPtr == nullptr) {
            GTE_LOG_WARNING("Core", "RegisterRenderPass('" + std::string(debugName != nullptr ? debugName : "<null>")
                + "') failed - no RenderFeatureCompositor registered.");
            return false;
        }
        if (debugName == nullptr) {
            GTE_LOG_WARNING("Core", "RegisterRenderPass() failed - debugName is null.");
            return false;
        }
        if (std::strlen(debugName) > 63) {
            GTE_LOG_WARNING("Core", "RegisterRenderPass('" + std::string(debugName)
                + "') failed - name exceeds the 63-byte limit; shorten it.");
            return false;
        }
        return m_renderFeatureCompositorPtr->RegisterBuiltInPreOpaqueFeature(debugName, priority,
            [action](rg::RenderGraphBuilder& builder, rg::RenderPassBlackboard& blackboard, rg::RenderViewId view) {
                action(builder, blackboard, view, rg::TextureHandle{}, Extent2D{}, ScenePassReadHandles{},
                    RenderFeatureCameraData{});
            });
    }

    case FrameEvent::PostOpaque: {
        if (m_renderFeatureCompositorPtr == nullptr) {
            GTE_LOG_WARNING("Core", "RegisterRenderPass('" + std::string(debugName != nullptr ? debugName : "<null>")
                + "') failed - no RenderFeatureCompositor registered.");
            return false;
        }
        if (debugName == nullptr) {
            GTE_LOG_WARNING("Core", "RegisterRenderPass() failed - debugName is null.");
            return false;
        }
        if (std::strlen(debugName) > 63) {
            GTE_LOG_WARNING("Core", "RegisterRenderPass('" + std::string(debugName)
                + "') failed - name exceeds the 63-byte limit; shorten it.");
            return false;
        }
        return m_renderFeatureCompositorPtr->RegisterBuiltInPostOpaqueFeature(debugName, priority,
            [action](rg::RenderGraphBuilder& builder, rg::RenderPassBlackboard& blackboard, rg::RenderViewId view,
                const ScenePassReadHandles& currentViewHandles) {
                action(builder, blackboard, view, rg::TextureHandle{}, Extent2D{}, currentViewHandles,
                    RenderFeatureCameraData{});
            });
    }

    case FrameEvent::PostTransparent: {
        if (m_renderFeatureCompositorPtr == nullptr) {
            GTE_LOG_WARNING("Core", "RegisterRenderPass('" + std::string(debugName != nullptr ? debugName : "<null>")
                + "') failed - no RenderFeatureCompositor registered.");
            return false;
        }
        if (debugName == nullptr) {
            GTE_LOG_WARNING("Core", "RegisterRenderPass() failed - debugName is null.");
            return false;
        }
        if (std::strlen(debugName) > 63) {
            GTE_LOG_WARNING("Core", "RegisterRenderPass('" + std::string(debugName)
                + "') failed - name exceeds the 63-byte limit; shorten it.");
            return false;
        }
        return m_renderFeatureCompositorPtr->RegisterBuiltInPostTransparentFeature(debugName, priority,
            [action](rg::RenderGraphBuilder& builder, rg::RenderPassBlackboard& blackboard, rg::RenderViewId view,
                const ScenePassReadHandles& currentViewHandles) {
                action(builder, blackboard, view, rg::TextureHandle{}, Extent2D{}, currentViewHandles,
                    RenderFeatureCameraData{});
            });
    }

    case FrameEvent::PostComposite:
    case FrameEvent::PreUI: {
        if (m_renderFeatureCompositorPtr == nullptr) {
            GTE_LOG_WARNING("Core", "RegisterRenderPass('" + std::string(debugName != nullptr ? debugName : "<null>")
                + "') failed - no RenderFeatureCompositor registered.");
            return false;
        }
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

// Registers a pass name to be included in GPU content stats.
bool Core::RegisterViewContentPassName(const char* name)
{
    if (name == nullptr) {
        return false;
    }
    for (const std::string& existing : m_viewContentPassNames) {
        if (existing == name) {
            GTE_LOG_ERROR("Core", std::string("RegisterViewContentPassName: duplicate name '") + name + "' - refused.");
            assert(false && "RegisterViewContentPassName: duplicate name.");
            return false;
        }
    }
    m_viewContentPassNames.emplace_back(name);
    return true;
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

            for (const RenderFeatureCompositor::PreOpaqueEntry& entry :
                m_renderFeatureCompositorPtr->PreOpaqueFeaturesInPriorityOrder()) {
                if (!entry.enabledOverride) {
                    continue;
                }

                const std::size_t before = frame.builder.DeclaredPassCount();
                const rg::RenderFeatureScope scope(frame.builder, entry.name);
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
            const bool isGameView = (frame.currentView == rg::RenderViewId::Named("Game"));
            const Mat4 viewProjectionOverride = viewData->viewProjection;
            IFrameDebuggerCaptureRecorder* frameDebuggerCapture =
                isGameView ? m_currentFrameDebuggerCaptureForOffscreenPipeline : nullptr;

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
            desc.execute = [this, aspectWidthOverHeight, isGameView, viewProjectionOverride, frameDebuggerCapture,
                                publishedServiceSlots, currentViewForServices](rg::PassContext& ctx) {
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
                if (isGameView) {
                    rg::CommandBuffer cmd = ctx.Cmd();
                    m_game.Render(m_renderer, aspectWidthOverHeight, nullptr, frameDebuggerCapture, std::nullopt,
                        m_gpuDrivenBatchedEntitiesThisFrame, sceneServicesSet, &cmd);
                } else {
                    const rg::SceneRecording recording(ctx);
                    m_game.Render(m_renderer, aspectWidthOverHeight, &viewProjectionOverride, nullptr, std::nullopt, {},
                        sceneServicesSet);
                }
            };
            out.push_back(std::move(desc));
        });

    // GPU-driven culling and indirect drawing passes.
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
            const rg::RenderViewId currentViewForServices = frame.currentView;

            for (const GpuDrivenBatchRenderData& batch : m_gpuDrivenBatchesThisFrame) {
                const rg::BufferHandle inputHandle = batch.inputHandle;
                const rg::BufferHandle indirectHandle = batch.indirectHandle;
                const rg::BufferHandle countHandle = batch.countHandle;
                const DescriptorSetHandle cullingDescriptorSet = batch.cullingDescriptorSet;
                const DescriptorSetHandle instanceBufferDescriptorSet = batch.instanceBufferDescriptorSet;
                const std::size_t instanceCount = batch.instanceCount;
                const MeshHandle meshHandle = batch.mesh;
                const PipelineHandle originalPipelineHandle = batch.originalPipeline;

                // Only exclude batched entities from normal drawing if the indirect pass is enabled.
                const bool indirectDrawEnabledThisFrame = rg::ShouldDeclareBuiltInPassThisFrame(
                    &m_renderPassToggleRegistry, batch.indirectDrawPassName);
                AppendGpuDrivenBatchEntityExclusionsIfIndirectDrawEnabled(
                    indirectDrawEnabledThisFrame, batch.entities, m_gpuDrivenBatchedEntitiesThisFrame);

                // --- Reset instance count buffer ---
                if (auto body = rg::MakeTransferBody(frame.builder, batch.resetPassName,
                        rg::TransferRecord::Fill(countHandle, 0, 0, sizeof(std::uint32_t)))) {
                    rg::RenderPassDesc desc;
                    desc.debugName = batch.resetPassName;
                    desc.kind = rg::DefaultPassKind(rg::DeclarativePassType::Transfer);
                    desc.order = rg::RenderPassEvent::Opaques;
                    desc.view = frame.currentView;
                    desc.owningFeatureName = "GPU Driven Batches";
                    desc.setup = std::move(body->setup);
                    desc.execute = std::move(body->execute);
                    out.push_back(std::move(desc));
                }

                // --- Frustum culling compute pass ---
                {
                    rg::RenderPassDesc desc;
                    desc.debugName = batch.cullingPassName;
                    desc.kind = rg::PassKind::Compute;
                    desc.order = rg::RenderPassEvent::Opaques;
                    desc.view = frame.currentView;
                    desc.owningFeatureName = "GPU Driven Batches";
                    desc.setup = [inputHandle, indirectHandle, countHandle](rg::RenderGraphBuilder::PassBuilder& pass) {
                        pass.ReadBuffer(inputHandle, rg::ResourceAccess::ComputeShaderRead);
                        pass.WriteBuffer(indirectHandle, rg::ResourceAccess::ComputeShaderWrite);
                        pass.WriteBuffer(countHandle, rg::ResourceAccess::ComputeShaderWrite);
                    };
                    desc.execute = [this, cullingDescriptorSet, instanceCount, frustumPlanes, useCompaction](
                                        rg::PassContext& ctx) {
                        CullingPushConstants pc{};
                        for (std::size_t i = 0; i < 6; ++i) {
                            pc.planes[i] = frustumPlanes[i];
                        }
                        pc.instanceCount = static_cast<std::uint32_t>(instanceCount);
                        pc.useCompaction = useCompaction ? 1u : 0u;

                        rg::CommandBuffer cmd = ctx.Cmd();
                        m_gpuDrivenBatchCache.RecordCulling(cmd, cullingDescriptorSet, pc);
                    };
                    out.push_back(std::move(desc));
                }

                // --- Indirect draw pass ---
                {
                    rg::IndirectDrawRecord record;
                    record.target = viewTarget;
                    record.indirect = indirectHandle;
                    record.count = countHandle;
                    record.instances = inputHandle;
                    record.maxDrawCount = static_cast<std::uint32_t>(instanceCount);
                    record.useCount = useCompaction;

                    auto resolveInputs = [this, meshHandle, originalPipelineHandle, instanceBufferDescriptorSet,
                                             viewProjection, currentViewForServices]() {
                        rg::IndirectDrawInputs inputs;
                        inputs.mesh = m_game.GetRenderSystem().TryGetMesh(meshHandle);
                        try {
                            inputs.pipeline = &m_gpuDrivenBatchCache.ResolveInstancedPipeline(
                                m_renderer, originalPipelineHandle, m_sceneServicesDescriptorSet.LayoutHandle());
                        } catch (const std::exception& e) {
                            const std::string failure =
                                std::string("GpuDrivenBatches: failed to resolve instanced pipeline: ") + e.what();
                            if (rg::BoundedOnceSet<64>::ShouldLog(m_frameFailureOnce.Note(failure))) {
                                GTE_LOG_ERROR("Core", failure);
                            }
                        }
                        inputs.instanceSet = instanceBufferDescriptorSet;
                        inputs.sceneSet = m_sceneServicesDescriptorSet.DescriptorSetFor(currentViewForServices);
                        inputs.viewProjection = viewProjection;
                        return inputs;
                    };

                    if (auto body = rg::MakeIndirectDrawBody(
                            frame.builder, batch.indirectDrawPassName, record, std::move(resolveInputs))) {
                        rg::RenderPassDesc desc;
                        desc.debugName = batch.indirectDrawPassName;
                        desc.kind = rg::DefaultPassKind(rg::DeclarativePassType::IndirectDraw);
                        desc.order = rg::RenderPassEvent::Opaques;
                        desc.view = frame.currentView;
                        desc.legacyCategory = rg::RenderPassCategory::General;
                        desc.owningFeatureName = "GPU Driven Batches";
                        desc.setup = std::move(body->setup);
                        desc.execute = std::move(body->execute);
                        out.push_back(std::move(desc));
                    }
                }

                // --- Visible count readback for the debug panel ---
                {
                    frame.builder.KeepBufferOutput(batch.readbackHandle);
                    if (auto body = rg::MakeTransferBody(frame.builder, batch.readbackPassName,
                            rg::TransferRecord::Readback(countHandle, batch.readbackHandle, sizeof(std::uint32_t)))) {
                        rg::RenderPassDesc desc;
                        desc.debugName = batch.readbackPassName;
                        desc.kind = rg::DefaultPassKind(rg::DeclarativePassType::Transfer);
                        desc.order = rg::RenderPassEvent::Opaques;
                        desc.view = frame.currentView;
                        desc.owningFeatureName = "GPU Driven Batches";
                        desc.setup = std::move(body->setup);
                        desc.execute = std::move(body->execute);
                        out.push_back(std::move(desc));
                    }
                }
            }
        });

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

            for (const RenderFeatureCompositor::PostOpaqueEntry& entry :
                m_renderFeatureCompositorPtr->PostOpaqueFeaturesInPriorityOrder()) {
                if (!entry.enabledOverride) {
                    continue;
                }

                const std::size_t before = frame.builder.DeclaredPassCount();
                const rg::RenderFeatureScope scope(frame.builder, entry.name);
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

                if (FindScenePassCallbackMissingReadDeclaration(
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

            for (const RenderFeatureCompositor::PostTransparentEntry& entry :
                m_renderFeatureCompositorPtr->PostTransparentFeaturesInPriorityOrder()) {
                if (!entry.enabledOverride) {
                    continue;
                }

                const std::size_t before = frame.builder.DeclaredPassCount();
                const rg::RenderFeatureScope scope(frame.builder, entry.name);
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

                if (FindScenePassCallbackMissingReadDeclaration(
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

    // Copies the engine-owned Game target into the swapchain (the offscreen graph left it sampleable).
    m_presentRenderPipeline.Register("PresentGameView", rg::ProviderScope::Once,
        [this](const rg::RenderPassFrameContext& frame, std::vector<rg::RenderPassDesc>&) {
            if (!m_directGameTargetUsedThisFrame || !m_directGameTarget.has_value()) {
                return;
            }
            const rg::RenderFeatureScope scope(frame.builder, "Present");
            bool copied = false;
            if (rg::ShouldDeclareBuiltInPassThisFrame(&m_renderPassToggleRegistry, "PresentGameView")) {
                RenderTexture& source = *m_directGameTarget;
                const rg::TextureHandle sourceHandle = frame.builder.ImportTexture("PresentSourceGameView",
                    source.Target(), ImageLayout::ShaderReadOnlyOptimal, source.SamplerObjectHandle(),
                    SamplerHandle::Invalid, &source);
                copied = rg::FullscreenPass(frame.builder, m_presentGameViewShader, "PresentGameView",
                    rg::ViewScope::Shared, rg::RenderPassEvent::BeforeEverything)
                             .Bind("sceneColor", sourceHandle)
                             .Target(m_swapchainImageThisFrame, kGameClearColor)
                             .Draw();
            }
            // Without the copy the Present pass must clear the swapchain itself.
            m_directGameTargetUsedThisFrame = copied;
        });

    m_presentRenderPipeline.Register("Present", rg::ProviderScope::Once,
        [this](const rg::RenderPassFrameContext& frame, std::vector<rg::RenderPassDesc>&) {
            const rg::RenderFeatureScope scope(frame.builder, "Present");
            AddPresentPass(frame.builder, m_swapchainImageThisFrame, !m_directGameTargetUsedThisFrame,
                m_recordImGuiThisFrame);
        });
}

// Returns the engine-owned Game target sized to the window, creating it on first use.
RenderTexture* Core::AcquireDirectGameTarget()
{
    if (m_windowWidth <= 0 || m_windowHeight <= 0) {
        return nullptr;
    }
    if (!m_directGameTarget.has_value()) {
        m_directGameTarget.emplace(m_renderer.CreateRenderTexture(m_windowWidth, m_windowHeight,
            m_renderer.ScenePixelFormat(), "DirectGameView", "DirectGameViewDepth",
            /*allowStorageImageAccess=*/true, /*allowDepthSampledAccess=*/true));
        return &*m_directGameTarget;
    }
    const Extent2D extent = m_directGameTarget->Extent();
    if (extent.width != static_cast<std::uint32_t>(m_windowWidth)
        || extent.height != static_cast<std::uint32_t>(m_windowHeight)) {
        m_renderer.ResizeRenderTexture(*m_directGameTarget, m_windowWidth, m_windowHeight);
    }
    return &*m_directGameTarget;
}

void Core::BuildFrame()
{
    m_renderGraph.BeginPersistentResourceFrame();

    m_exposedViewOutputsThisFrame.clear();

    // Query active editor render targets; without any, the Game view renders into an engine-owned target.
    RenderTexture* gameTarget = (m_editorLayer != nullptr) ? m_editorLayer->GameViewTarget() : nullptr;
    RenderTexture* sceneTarget = (m_editorLayer != nullptr) ? m_editorLayer->SceneViewTarget() : nullptr;
    m_directGameTargetUsedThisFrame = false;
    if (gameTarget == nullptr && sceneTarget == nullptr) {
        try {
            gameTarget = AcquireDirectGameTarget();
        } catch (const std::exception& e) {
            const std::string failure = std::string("Core: failed to acquire the direct Game target: ") + e.what();
            if (rg::BoundedOnceSet<64>::ShouldLog(m_frameFailureOnce.Note(failure))) {
                GTE_LOG_ERROR("Core", failure);
            }
            gameTarget = nullptr;
        }
        m_directGameTargetUsedThisFrame = (gameTarget != nullptr);
    }
    m_gameTargetThisFrame = gameTarget;

    IFrameDebuggerCaptureRecorder* frameDebuggerCapture =
        (m_editorLayer != nullptr) ? m_editorLayer->PrepareFrameDebuggerCaptureContext() : nullptr;

    // Execute offscreen rendering for Game and Scene views.
    if (gameTarget != nullptr || sceneTarget != nullptr) {
        try {
            GTE_PROFILE_SCOPE("RenderGraph::Execute(Offscreen)");
            const rg::RecordingContext offscreenCmd = m_renderer.BeginOffscreenRenderGraphRecording();
            m_renderGraph.Execute(offscreenCmd, rg::ExecuteTimingMode::SynchronousImmediateReadback,
                [&](rg::RenderGraphBuilder& b) {
                    m_gpuSkinningRequestsThisFrame = m_game.CollectGpuSkinningDispatchRequests();
                    m_gpuSkinningHandlesThisFrame.clear();
                    m_gpuSkinningHandlesThisFrame.reserve(m_gpuSkinningRequestsThisFrame.size());
                    for (const AnimationSystem::GpuSkinningDispatchRequest& request :
                        m_gpuSkinningRequestsThisFrame) {
                        m_gpuSkinningHandlesThisFrame.push_back(b.Import(*request.dispatch.output, request.name));
                    }

                    m_offscreenBlackboardThisFrame.BeginFrame();
                    rg::RenderPassFrameContext frame{
                        {}, rg::RenderViewId::Shared(), m_offscreenBlackboardThisFrame, b, {} };

                    m_currentViewDataThisFrame.clear();
                    m_currentFrameDebuggerCaptureForOffscreenPipeline = nullptr;
                    m_gpuDrivenBatchesThisFrame.clear();
                    m_gpuDrivenBatchedEntitiesThisFrame.clear();
                    m_gpuDrivenBatchDebugInfoLastFrame.clear();
                    float gameAspectForReplay = 1.0f;

                    // Set up Game View rendering.
                    if (gameTarget != nullptr) {
                        const Extent2D extent = gameTarget->Extent();
                        const float aspect =
                            AspectRatioOf(static_cast<int>(extent.width), static_cast<int>(extent.height));

                        const Vec3 gameEyeWorldPosition = RenderSystem::ResolveActiveCameraWorldPosition(m_game.GetRegistry());
                        const Mat4 gameViewProjection =
                            RenderSystem::ResolveActiveCameraViewProjection(m_game.GetRegistry(), aspect);

                        const rg::TextureHandle h = b.ImportTexture("GameView", gameTarget->Target(),
                            ImageLayout::Undefined, gameTarget->SamplerObjectHandle(), gameTarget->DepthSamplerObjectHandle(), gameTarget);

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
                            std::unordered_set<BufferObjectHandle> gpuSkinnedOutputBuffers;
                            gpuSkinnedOutputBuffers.reserve(m_gpuSkinningRequestsThisFrame.size());
                            for (const AnimationSystem::GpuSkinningDispatchRequest& request :
                                m_gpuSkinningRequestsThisFrame) {
                                gpuSkinnedOutputBuffers.insert(request.dispatch.output->Handle());
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
                                        m_renderer, frameEntry.pipeline, m_sceneServicesDescriptorSet.LayoutHandle());
                                } catch (const std::exception& e) {
                                    const std::string failure =
                                        std::string("GpuDrivenBatches: failed to resolve instanced pipeline for a batch: ")
                                        + e.what();
                                    if (rg::BoundedOnceSet<64>::ShouldLog(m_frameFailureOnce.Note(failure))) {
                                        GTE_LOG_ERROR("Core", failure);
                                    }
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
                                data.inputHandle = b.Import(cacheEntry->inputBuffer, names.inputBufferName);
                                data.indirectHandle =
                                    b.Import(cacheEntry->indirectCommandBuffer, names.indirectBufferName);
                                data.countHandle = b.Import(cacheEntry->countBuffer, names.countBufferName);
                                data.readbackHandle =
                                    b.Import(cacheEntry->countReadbackBuffer, names.readbackBufferName);
                                data.cullingDescriptorSet = cacheEntry->cullingDescriptorSet;
                                data.instanceBufferDescriptorSet = cacheEntry->instanceBufferDescriptorSet;
                                data.resetPassName = names.resetPassName;
                                data.cullingPassName = names.cullingPassName;
                                data.indirectDrawPassName = names.indirectDrawPassName;
                                data.readbackPassName = names.readbackPassName;
                                data.displayName = names.displayName;
                                data.entities.reserve(frameEntry.commands.size());
                                for (const DrawCommand& command : frameEntry.commands) {
                                    data.entities.push_back(command.entity);
                                }
                                m_gpuDrivenBatchesThisFrame.push_back(std::move(data));
                            }
                        }
                    }

                    // Set up Scene View rendering.
                    if (sceneTarget != nullptr) {
                        const Extent2D extent = sceneTarget->Extent();
                        const float aspect =
                            AspectRatioOf(static_cast<int>(extent.width), static_cast<int>(extent.height));
                        const Mat4 sceneViewProjection = m_editorLayer->SceneViewProjection(aspect);
                        const Vec3 sceneEyeWorldPosition = m_editorLayer->SceneViewCameraWorldPosition();

                        const rg::TextureHandle h = b.ImportTexture("SceneView", sceneTarget->Target(),
                            ImageLayout::Undefined, sceneTarget->SamplerObjectHandle(), sceneTarget->DepthSamplerObjectHandle(), sceneTarget);

                        RenderPassViewData sceneViewData;
                        sceneViewData.id = rg::RenderViewId::Named("Scene");
                        sceneViewData.colorTarget = h;
                        sceneViewData.renderTexture = sceneTarget;
                        sceneViewData.aspectWidthOverHeight = aspect;
                        sceneViewData.viewProjection = sceneViewProjection;
                        sceneViewData.eyeWorldPosition = sceneEyeWorldPosition;
                        sceneViewData.recordSceneOverlay = [this](const rg::RecordingContext& cmd, const Mat4& viewProj) {
                            if (m_editorLayer != nullptr) {
                                m_editorLayer->RenderSceneGrid(cmd, viewProj);
                            }
                        };
                        m_currentViewDataThisFrame.push_back(sceneViewData);

                        frame.activeViews.push_back(rg::RenderViewId::Named("Scene"));
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
                    if (gameTarget != nullptr && frameDebuggerCapture != nullptr && m_editorLayer != nullptr
                        && m_editorLayer->ConsumePendingFrameDebuggerReplayRequest()) {
                        const std::vector<rg::BufferHandle> gpuSkinningBuffersForReplay =
                            m_offscreenBlackboardThisFrame.Fetch<std::vector<rg::BufferHandle>>(kGpuSkinningOutputsKey)
                                .value_or(std::vector<rg::BufferHandle>{});
                        const std::size_t objectCount = m_game.CountGameViewDrawCommandsThisFrame();
                        const std::vector<rg::TextureHandle> replayStepHandles = frameDebuggerCapture->AddReplayPasses(
                            b, m_game, m_renderer, gameAspectForReplay, objectCount, gpuSkinningBuffersForReplay,
                            viewBackgroundForReplay.value_or(rg::FullscreenReplay{}), *gameTarget,
                            &m_renderPassToggleRegistry);
                        for (const rg::TextureHandle& replayHandle : replayStepHandles) {
                            outputs.push_back(replayHandle);
                        }
                    }

                    return outputs;
                });

            // Finalize textures for external sampling (e.g. editor UI viewport panels). A view output a feature
            // exposed this frame is finalized too and replaces the raw view in the editor.
            const rg::ResourceState shaderReadState = rg::RequiredStateFor(rg::ResourceAccess::ShaderRead, false);
            const auto finalizeView = [&](RenderTexture& target, const char* viewName, rg::ViewScope scope) {
                target.FinalizeForExternalSampling(offscreenCmd);
                m_renderGraph.NotifyDebugTextureStateOverride(viewName, shaderReadState);

                const ExposedViewOutput* exposed = FindExposedViewOutput(scope);
                if (exposed == nullptr) {
                    return static_cast<RenderTexture*>(nullptr);
                }
                exposed->texture->FinalizeForExternalSampling(offscreenCmd);
                m_renderGraph.NotifyDebugTextureStateOverride(exposed->name, shaderReadState);
                return exposed->texture;
            };
            if (gameTarget != nullptr) {
                RenderTexture* exposed = finalizeView(*gameTarget, "GameView", rg::ViewScope::GameView);
                if (m_editorLayer != nullptr) {
                    m_editorLayer->SetGameViewCompositedTexture(exposed);
                }
            }
            if (sceneTarget != nullptr) {
                RenderTexture* exposed = finalizeView(*sceneTarget, "SceneView", rg::ViewScope::SceneView);
                if (m_editorLayer != nullptr) {
                    m_editorLayer->SetSceneViewCompositedTexture(exposed);
                }
            }
            m_renderer.EndOffscreenRenderGraphRecording();

            // Read back culled instance counts for debugging.
            for (const GpuDrivenBatchRenderData& batch : m_gpuDrivenBatchesThisFrame) {
                const GpuDrivenBatchKey key{ batch.mesh, batch.originalPipeline };
                GpuDrivenBatchDebugInfo info;
                info.batchName = (batch.displayName != nullptr) ? batch.displayName : "(unnamed batch)";
                info.instanceCount = static_cast<std::uint32_t>(batch.instanceCount);
                info.visibleCount = m_gpuDrivenBatchCache.ReadLastKnownVisibleCount(key);
                m_gpuDrivenBatchDebugInfoLastFrame.push_back(info);
            }

            m_renderGraph.FinalizeSynchronousGpuTiming();
        } catch (const std::exception& e) {
            // A failure that repeats every frame is logged once per distinct message.
            const std::string failure = std::string("RenderGraph offscreen Execute() failed: ") + e.what();
            if (rg::BoundedOnceSet<64>::ShouldLog(m_frameFailureOnce.Note(failure))) {
                GTE_LOG_ERROR("Core", failure);
            }
            // The target's contents are undefined; present clears the swapchain instead of copying it.
            m_directGameTargetUsedThisFrame = false;
            assert(false && "RenderGraph offscreen Execute() threw - see the engine log");
        }
    }

    // Collect GPU pass stats for active views.
    std::vector<rg::PassGpuStats> viewContentStats;
    viewContentStats.reserve(m_viewContentPassNames.size());
    for (const std::string& passName : m_viewContentPassNames) {
        viewContentStats.push_back(m_renderGraph.LastKnownStatsFor(passName.c_str()));
    }

    if (gameTarget != nullptr) {
        const rg::PassGpuStats gameViewStats = rg::CombinePassGpuStats(viewContentStats);
        Profiling::FrameProfiler::Instance().SetGpuPassDrawStats(Profiling::GpuPass::GameView,
            Profiling::GpuSampleStatus::Present, gameViewStats.drawStats.drawCallCount,
            gameViewStats.drawStats.triangleCount);
        Profiling::FrameProfiler::Instance().SetGpuPassTiming(Profiling::GpuPass::GameView,
            ToProfilingGpuSampleStatus(gameViewStats.timing.status), gameViewStats.timing.milliseconds);
    }
    if (sceneTarget != nullptr) {
        const rg::PassGpuStats sceneViewStats = rg::CombinePassGpuStats(viewContentStats);
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

    std::optional<DrawStats> presentStats;
    try {
        presentStats = m_renderer.PresentViaRenderGraph(m_renderGraph, /*needsSwapchainDepth=*/false,
            [&](rg::RenderGraphBuilder& b, rg::TextureHandle swapchainImage) {
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
            });
    } catch (const std::exception& e) {
        const std::string failure = std::string("RenderGraph Present Execute() failed: ") + e.what();
        if (rg::BoundedOnceSet<64>::ShouldLog(m_frameFailureOnce.Note(failure))) {
            GTE_LOG_ERROR("Core", failure);
        }
        assert(false && "RenderGraph Present Execute() threw - see the engine log");
    }

    if (presentStats.has_value()) {
        Profiling::FrameProfiler::Instance().SetGpuPassDrawStats(Profiling::GpuPass::Present,
            Profiling::GpuSampleStatus::Present, presentStats->drawCallCount, presentStats->triangleCount);
        const GpuTimingSample presentTiming = m_renderGraph.LastKnownStatsFor("Present").timing;
        Profiling::FrameProfiler::Instance().SetGpuPassTiming(Profiling::GpuPass::Present,
            ToProfilingGpuSampleStatus(presentTiming.status), presentTiming.milliseconds);
    }
}

} // namespace gte