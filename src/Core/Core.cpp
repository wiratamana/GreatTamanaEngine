#include "Core.h"

// PHASE0_MASTER_STRATEGY.md's Locked Design Decision #8 - the ONE gte_core
// .cpp file allowed to #include the real src/Editor/EditorLayer.h header
// (Core.h itself only ever forward-declares IEditorLayer).
#include "../Editor/EditorLayer.h"

// RenderPasses.h/FrameProfiler.h/ScopeTimer.h back "GpuSkinning"/"Present"/
// "ClearViewTarget" below - RenderPasses.h lives under src/Application/, but
// that whole folder already sits inside the gte_core CMake target's own
// source list.
#include "../Application/RenderPasses.h"
#include "../Profiling/FrameProfiler.h"
#include "../Profiling/ScopeTimer.h"
#include "../Renderer/GpuSkinning/GpuSkinningPipelines.h"
#include "../Renderer/GpuSkinning/GpuSkinningRenderPassTags.h"
#include "../Renderer/Culling/CullingPipelines.h"
#include "../Renderer/RenderGraph/RenderGraphBarrierPlanner.h"
#include "../Renderer/RenderGraph/RenderGraphBuilder.h"
#include "../Renderer/RenderGraph/RenderGraphDebugTextureRegistry.h"
// The generic, early toggle guard used by the "IndirectDraw" check below.
#include "../Renderer/RenderGraph/RenderPassToggleGuard.h"

// editor-core-separation-22 campaign, PHASE3
// (task_manager/editor-core-separation-22/
// PHASE3_FIX_AUDIT_FINDINGS_SIDE_CHANNEL_LEAKS.md) - the pure "which of this
// batch's own entities are safe to exclude from RenderOpaque's own normal
// per-entity draw path THIS frame" decision, used by the "GpuDrivenBatches"
// provider below (fixes PHASE2_COMPLETION_REPORT.md's own finding #19).
#include "GpuDrivenBatchEntityExclusionLogic.h"

// better-render-pass-1 campaign, PHASE9 (Decision D3) - the pure,
// Tier-1-testable counter-increment logic backing
// Core::AddScreenPostProcessPass()'s own RUNTIME auto-priority assignment
// (see Core::AddScreenPostProcessPass() below for the full reasoning).
#include "ScreenPostProcessPassPriorityAssignment.h"

// editor-core-separation-6 campaign, PHASE2
// (PHASE2_PLUGIN_CAPABILITY_ORCHESTRATOR_REGISTRY_AND_RENDER_FEATURE_MIGRATION.md)
// - the ONE generic IPluginCapabilityOrchestrator registry. better-render-pass-2
// campaign, PHASE2 (PHASE2_DISABLE_RUNTIME_CALL_SITES.md) already removed
// this file's own registration of its two ABI-only implementors
// (LegacyRenderFeatureOrchestrator/EditorPanelCapabilityOrchestrator);
// PHASE3 (PHASE3_DELETE_ABI_HOST_CODE.md) now also removes their own
// #include lines below, since both classes' .h/.cpp files are deleted
// outright this phase.
#include "Plugins/IPluginCapabilityOrchestrator.h"
// editor-core-separation-6 campaign, PHASE4
// (PHASE4_RENDER_FEATURE_COMPOSITOR_CORE_AND_ORDERING.md) - the third real
// IPluginCapabilityOrchestrator implementation, the real `_v2` render-feature
// compositing pipeline (PHASE0_MASTER_STRATEGY.md's whole reason to exist).
#include "Plugins/RenderFeatureCompositor.h"
// editor-core-separation-13 campaign (Project Assembly Hot Reload plan,
// BIG-STEP 2), PHASE3 - RecordRenderPass()'s own no-op-outside-a-bracket
// call, added to RegisterProjectRenderPassProvider() below.
#include "Plugins/ProjectAssemblyRegistrationLedger.h"
#include "ViewBackgroundReplayKey.h"

#include <cassert>
#include <cstdint>
#include <cstdio>
#include <cstring>
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

// Shared by the "PostOpaqueFeatures"/"PostTransparentFeatures" providers
// below - resolves the current view's own already-rendered color/depth into
// a ScenePassReadHandles, or std::nullopt if this view has no renderTexture
// or that renderTexture was never built with depth-sampled access enabled
// (RenderTexture::DepthSampler() returns VK_NULL_HANDLE in that case). This
// guard is what keeps both providers safe if the set of active views is
// ever widened beyond today's Game/Scene pair.
std::optional<ScenePassReadHandles> ResolveScenePassReadHandles(
    const rg::RenderGraphBuilder& builder, const RenderPassViewData* viewData)
{
    if (viewData == nullptr || viewData->renderTexture == nullptr) {
        return std::nullopt;
    }
    const rg::RenderGraphBuilder::ImportedTextureSamplers resolved =
        builder.ResolveImportedTextureSamplers(viewData->colorTarget);
    if (resolved.depthSampler == VK_NULL_HANDLE) {
        return std::nullopt; // Mirrors the old DepthSampler()-null guard - no depth-sampled view available.
    }
    ScenePassReadHandles handles;
    handles.colorHandle = viewData->colorTarget;
    handles.depthHandle = viewData->colorTarget; // Same underlying imported texture - color and depth are
                                                  // two sub-resources of the one handle in this engine today.
    handles.colorSampler = resolved.colorSampler;
    handles.depthImageView = resolved.depthImageView;
    handles.depthSampler = resolved.depthSampler;
    return handles;
}

} // namespace

Core::Core(ISurfaceProvider& surfaceProvider, IHostServices& hostServices)
    : m_renderer(surfaceProvider)
    // Block 4 (task_manager/better-render-pass-6), PHASE7 - constructed
    // immediately after m_renderer (declaration order matches Core.h - see
    // m_sceneServicesDescriptorSet's own member doc comment there).
    , m_sceneServicesDescriptorSet(m_renderer)
    // better-render-pass-3 campaign, BLOCK 2 (Arbitrary Render Views) -
    // needs a live Renderer&, constructed immediately after m_renderer
    // (declaration order matches Core.h - see m_renderViewRegistry's own
    // member doc comment there).
    , m_renderViewRegistry(m_renderer)
    // better-render-pass-2 campaign, PHASE3 (PHASE3_DELETE_ABI_HOST_CODE.md) -
    // `, m_pluginRenderOperationRegistry(m_renderer)` removed - that member
    // (and its type) are deleted outright this phase (ABI-only).
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
    // better-render-pass-2 campaign, PHASE2
    // (PHASE2_DISABLE_RUNTIME_CALL_SITES.md) - the two ABI-only orchestrator
    // registrations that used to live here, LegacyRenderFeatureOrchestrator
    // and EditorPanelCapabilityOrchestrator, are removed outright (Locked
    // Conclusion D2, PHASE0_MASTER_STRATEGY.md Section 2.4). Both classes
    // are left intact for now (PHASE3 deletes them) - this is purely "never
    // register them again". RenderFeatureCompositor below is the one
    // surviving orchestrator - left completely untouched, since Project
    // Assembly's on-screen compositing depends on its
    // ContributeRenderGraphPasses() still being called every frame via the
    // still-intact generic m_capabilityOrchestrators loop (Decision D3).
    //
    // editor-core-separation-6 campaign, PHASE4
    // (PHASE4_RENDER_FEATURE_COMPOSITOR_CORE_AND_ORDERING.md) - the real
    // `_v2` render-feature compositing pipeline - reuses the SAME m_renderer
    // member every other real pass in this file already reads (never a second,
    // duplicate Renderer instance).
    //
    // editor-core-separation-6 campaign, PHASE7
    // (PHASE7_RENDER_GRAPH_PANEL_VISIBILITY.md) - m_renderFeatureCompositorPtr
    // is captured HERE, at the exact same statement that constructs the
    // owning std::unique_ptr, BEFORE it is moved into
    // m_capabilityOrchestrators - see Core.h's own doc comment on
    // GetRenderFeatureCompositor()/m_renderFeatureCompositorPtr for why this
    // is a plain, zero-cost pointer with no dynamic_cast/RTTI involved.
    // better-render-pass-2 campaign, PHASE3 (PHASE3_DELETE_ABI_HOST_CODE.md) -
    // RenderFeatureCompositor's constructor no longer takes a trailing
    // PluginRenderOperationRegistry& parameter (that type is deleted outright
    // this phase, ABI-only).
    auto renderFeatureCompositor = std::make_unique<RenderFeatureCompositor>(*this, m_renderer);
    m_renderFeatureCompositorPtr = renderFeatureCompositor.get();
    m_capabilityOrchestrators.push_back(std::move(renderFeatureCompositor));
}

// better-render-pass-2 campaign, PHASE3 (PHASE3_DELETE_ABI_HOST_CODE.md) -
// Core::LoadPlugins() removed outright, along with its private m_pluginHost
// member (Core.h) - PluginHost.h/.cpp are deleted this phase (ABI-only; the
// one call site that ever invoked this method, EditorHost.cpp's own, was
// already removed by PHASE2, per Locked Conclusion D2,
// PHASE0_MASTER_STRATEGY.md Section 2.4).

// editor-core-separation-11 campaign (Project Assembly system), PHASE5 -
// thin pass-through into m_projectAssemblyHost, mirroring LoadPlugins()
// immediately above exactly (always compiled - only EditorHost.cpp's own
// call site is gated, behind `#if GTE_ENABLE_PROJECT_ASSEMBLIES`, its own,
// separate flag, never GTE_ENABLE_PLUGINS).
void Core::LoadProjectAssemblies(const std::filesystem::path& outputDirectory, EditorHost* editorHost)
{
    m_projectAssemblyHost.LoadProjectAssemblies(outputDirectory, *this, editorHost);
}

// editor-core-separation-11 campaign (Project Assembly system), PHASE8
// (Finding B) - thin pass-through into m_offscreenRenderPipeline.Register(),
// mirroring LoadPlugins()/LoadProjectAssemblies() immediately above exactly.
// Always compiled (no #if guard here - a Project Assembly's own
// GTE_RegisterProject entry point calls this directly, unconditionally,
// exactly as LoadProjectAssemblies() itself is called from PHASE5's own
// ProjectAssemblyHost, which only ever runs when GTE_ENABLE_PROJECT_ASSEMBLIES
// is already on). m_offscreenRenderPipeline is confirmed the correct target:
// it is the pipeline every production Game-View/Scene-View pass
// ("GpuSkinning", "RenderOpaque",
// "GpuDrivenBatches", ...) registers onto (see
// RegisterOffscreenRenderPipelineProviders() below) - m_presentRenderPipeline
// is the separate, narrower pipeline used ONLY for the one "Present"
// pass. `timing` forwards straight into the underlying 4-argument
// Register() overload, defaulting to its own BeforeDeferredPasses default -
// every pre-existing 3-argument call site keeps compiling unmodified. Safe
// to call any time after Core's own constructor has run (a Project
// Assembly's GTE_RegisterProject runs from EditorHost.cpp's constructor
// body, strictly after Core's own construction) - Register() merely appends
// to an internal std::vector read fresh, in full, every frame by
// DeclareInto(), so registering after construction but before the first
// real frame renders behaves identically to registering during
// construction.
void Core::RegisterProjectRenderPassProvider(const char* debugName, rg::ProviderScope scope,
    rg::RenderPassProvider provider, rg::ProviderTiming timing)
{
    m_offscreenRenderPipeline.Register(debugName, scope, std::move(provider), timing);
    // editor-core-separation-13 campaign, PHASE3 - safe no-op outside an
    // active ProjectAssemblyRegistrationLedger::BeginRecordingFor() bracket.
    ProjectAssemblyRegistrationLedger::Instance().RecordRenderPass(debugName);
}

// editor-core-separation-13 campaign, PHASE3 - the teardown counterpart of
// RegisterProjectRenderPassProvider() immediately above, called ONLY by
// ProjectAssemblyRegistrationLedger::UnregisterEverythingFor() (see that
// class for the full reasoning) - never by any Project Assembly's own
// authored code directly.
void Core::UnregisterProjectRenderPassProvider(const char* debugName)
{
    m_offscreenRenderPipeline.Unregister(debugName);
}

// Thin pass-throughs into m_renderViewRegistry. `depthOnly` (default false)
// translates to RenderViewDesc{ hasColor = !depthOnly, hasDepth = true } -
// a depth-only view always keeps its depth half and never allocates a
// color image. See Core.h's own doc comment on
// CreateRenderView()/FindRenderViewTarget() for the full "why
// RegisterProjectRenderPassProvider(), never AddScreenPostProcessPass()"
// reasoning the writer pass itself must follow.
rg::RenderViewId Core::CreateRenderView(const char* name, std::uint32_t width, std::uint32_t height,
    bool depthOnly, bool allowDepthSampledAccess)
{
    RenderViewDesc desc;
    desc.width = width;
    desc.height = height;
    desc.hasColor = !depthOnly;
    desc.hasDepth = true;
    desc.allowDepthSampledAccess = allowDepthSampledAccess;
    return m_renderViewRegistry.CreateOrGetView(name, desc);
}

RenderTexture* Core::FindRenderViewTarget(rg::RenderViewId view) const noexcept
{
    return m_renderViewRegistry.FindViewTarget(view);
}

// editor-core-separation-23 campaign, PHASE3
// (PHASE3_CORE_REGISTER_PROJECT_RENDER_FEATURE_API.md) - a thin pass-through
// into m_renderFeatureCompositorPtr's own RegisterProjectFeature() (PHASE2 -
// PHASE2_REGISTER_PROJECT_FEATURE_AND_SLOT_POOL.md), NOT
// m_offscreenRenderPipeline (RegisterProjectRenderPassProvider() above's own
// target) - RenderFeatureCompositor is a separate object, owned by one of
// Core's m_capabilityOrchestrators entries, resolved through the exact same
// m_renderFeatureCompositorPtr GetRenderFeatureCompositor() already returns.
bool Core::RegisterProjectRenderFeature(const char* debugName, RenderFeatureStage stage,
    RenderFeatureBlendMode blendMode, std::int32_t priority, ProjectRenderFeatureCallback callback)
{
    if (m_renderFeatureCompositorPtr == nullptr) {
        GTE_LOG_WARNING("Core", "RegisterProjectRenderFeature('" + std::string(debugName != nullptr ? debugName : "<null>")
            + "') failed - no RenderFeatureCompositor orchestrator is registered in this build.");
        return false;
    }
    if (debugName == nullptr) {
        GTE_LOG_WARNING("Core", "RegisterProjectRenderFeature() failed - debugName is null.");
        return false;
    }
    // GtePluginRenderFeatureDescriptor::name is a fixed char[64] (63 usable
    // bytes + null terminator). This is the FIRST call site in this engine
    // that builds this string from free-form, un-length-checked input -
    // silently truncating here would defeat this whole system's own
    // "collision-checked by name" promise (two long names sharing the same
    // first 63 bytes would be reported identical with no diagnostic). Reject
    // outright instead - never truncate-and-proceed.
    if (std::strlen(debugName) > 63) {
        GTE_LOG_WARNING("Core", "RegisterProjectRenderFeature('" + std::string(debugName)
            + "') failed - name exceeds the 63-byte limit for GtePluginRenderFeatureDescriptor::name; "
            "shorten it (never silently truncated).");
        return false;
    }

    const GtePluginRenderFeatureDescriptor descriptor =
        MakeRenderFeatureDescriptor(debugName, stage, priority, blendMode);
    const bool registered = m_renderFeatureCompositorPtr->RegisterProjectFeature(descriptor, std::move(callback));
    if (registered) {
        // editor-core-separation-23 campaign, PHASE4
        // (PHASE4_HOT_RELOAD_LEDGER_TEARDOWN_WIRING.md) - safe no-op outside
        // an active ProjectAssemblyRegistrationLedger::BeginRecordingFor()
        // bracket, mirroring RegisterProjectRenderPassProvider()'s own
        // RecordRenderPass() call immediately above - but, UNLIKE that call,
        // gated on success: this underlying call CAN genuinely fail
        // (duplicate name/unwired stage/slot exhaustion), so recording it
        // unconditionally would let the ledger track a name that was never
        // actually registered, breaking UnregisterEverythingFor()'s own
        // later teardown call for it.
        ProjectAssemblyRegistrationLedger::Instance().RecordRenderFeature(debugName);
    }
    return registered;
}

// editor-core-separation-23 campaign, PHASE3 - the teardown counterpart of
// RegisterProjectRenderFeature() immediately above. Null-safe; forwards
// straight to UnregisterProjectFeature() - no length check needed (an
// over-length name could never have been successfully registered in the
// first place, so FindEntryByName() simply reports "not found," the same
// harmless outcome as any other unknown name).
void Core::UnregisterProjectRenderFeature(const char* debugName)
{
    if (m_renderFeatureCompositorPtr == nullptr || debugName == nullptr) {
        return;
    }
    m_renderFeatureCompositorPtr->UnregisterProjectFeature(debugName);
}

// better-render-pass-5 effort, BLOCK 3, PHASE3 - see this method's own
// doc comment (Core.h) for the full contract.
bool Core::AddPreOpaquePass(const char* debugName, ProjectPreOpaqueCallback callback, std::int32_t priority)
{
    if (m_renderFeatureCompositorPtr == nullptr) {
        GTE_LOG_WARNING("Core", "AddPreOpaquePass('" + std::string(debugName != nullptr ? debugName : "<null>")
            + "') failed - no RenderFeatureCompositor orchestrator is registered in this build.");
        return false;
    }
    if (debugName == nullptr) {
        GTE_LOG_WARNING("Core", "AddPreOpaquePass() failed - debugName is null.");
        return false;
    }
    if (std::strlen(debugName) > 63) {
        GTE_LOG_WARNING("Core", "AddPreOpaquePass('" + std::string(debugName)
            + "') failed - name exceeds the 63-byte limit this engine's registration surfaces consistently "
            "enforce; shorten it (never silently truncated).");
        return false;
    }

    const bool registered =
        m_renderFeatureCompositorPtr->RegisterPreOpaqueFeature(debugName, priority, std::move(callback));
    if (registered) {
        // editor-core-separation-23 campaign, PHASE4's own precedent,
        // extended here - gated on success for the exact same reason
        // RecordRenderFeature() already is (RegisterPreOpaqueFeature()
        // can genuinely fail - duplicate name).
        ProjectAssemblyRegistrationLedger::Instance().RecordPreOpaqueFeature(debugName);
    }
    return registered;
}

// better-render-pass-5 effort, BLOCK 3, PHASE3 - the teardown
// counterpart of AddPreOpaquePass() immediately above. Null-safe;
// forwards straight to UnregisterPreOpaqueFeature() - no length check
// needed (an over-length name could never have been successfully
// registered in the first place).
void Core::RemovePreOpaquePass(const char* debugName)
{
    if (m_renderFeatureCompositorPtr == nullptr || debugName == nullptr) {
        return;
    }
    m_renderFeatureCompositorPtr->UnregisterPreOpaqueFeature(debugName);
}

// Same contract as AddPreOpaquePass() above, for the PostOpaque stage - see
// that method's own doc comment (Core.h) for the full shared contract.
bool Core::AddPostOpaquePass(const char* debugName, ProjectScenePassCallback callback, std::int32_t priority)
{
    if (m_renderFeatureCompositorPtr == nullptr) {
        GTE_LOG_WARNING("Core", "AddPostOpaquePass('" + std::string(debugName != nullptr ? debugName : "<null>")
            + "') failed - no RenderFeatureCompositor orchestrator is registered in this build.");
        return false;
    }
    if (debugName == nullptr) {
        GTE_LOG_WARNING("Core", "AddPostOpaquePass() failed - debugName is null.");
        return false;
    }
    if (std::strlen(debugName) > 63) {
        GTE_LOG_WARNING("Core", "AddPostOpaquePass('" + std::string(debugName)
            + "') failed - name exceeds the 63-byte limit this engine's registration surfaces consistently "
            "enforce; shorten it (never silently truncated).");
        return false;
    }

    const bool registered =
        m_renderFeatureCompositorPtr->RegisterPostOpaqueFeature(debugName, priority, std::move(callback));
    if (registered) {
        ProjectAssemblyRegistrationLedger::Instance().RecordPostOpaqueFeature(debugName);
    }
    return registered;
}

// Teardown counterpart of AddPostOpaquePass() immediately above. Null-safe;
// a safe no-op if debugName was never successfully registered.
void Core::RemovePostOpaquePass(const char* debugName)
{
    if (m_renderFeatureCompositorPtr == nullptr || debugName == nullptr) {
        return;
    }
    m_renderFeatureCompositorPtr->UnregisterPostOpaqueFeature(debugName);
}

// Same contract as AddPreOpaquePass() above, for the PostTransparent stage -
// see that method's own doc comment (Core.h) for the full shared contract.
bool Core::AddPostTransparentPass(const char* debugName, ProjectScenePassCallback callback, std::int32_t priority)
{
    if (m_renderFeatureCompositorPtr == nullptr) {
        GTE_LOG_WARNING("Core", "AddPostTransparentPass('" + std::string(debugName != nullptr ? debugName : "<null>")
            + "') failed - no RenderFeatureCompositor orchestrator is registered in this build.");
        return false;
    }
    if (debugName == nullptr) {
        GTE_LOG_WARNING("Core", "AddPostTransparentPass() failed - debugName is null.");
        return false;
    }
    if (std::strlen(debugName) > 63) {
        GTE_LOG_WARNING("Core", "AddPostTransparentPass('" + std::string(debugName)
            + "') failed - name exceeds the 63-byte limit this engine's registration surfaces consistently "
            "enforce; shorten it (never silently truncated).");
        return false;
    }

    const bool registered =
        m_renderFeatureCompositorPtr->RegisterPostTransparentFeature(debugName, priority, std::move(callback));
    if (registered) {
        ProjectAssemblyRegistrationLedger::Instance().RecordPostTransparentFeature(debugName);
    }
    return registered;
}

// Teardown counterpart of AddPostTransparentPass() immediately above.
// Null-safe; a safe no-op if debugName was never successfully registered.
void Core::RemovePostTransparentPass(const char* debugName)
{
    if (m_renderFeatureCompositorPtr == nullptr || debugName == nullptr) {
        return;
    }
    m_renderFeatureCompositorPtr->UnregisterPostTransparentFeature(debugName);
}

// better-render-pass-1 campaign, PHASE9 (Decision D3) - additive convenience
// wrapper over RegisterProjectRenderFeature() immediately above: fixes stage
// to RenderFeatureStage::PostComposite (the one, real "draw over the final
// composited screen" hook point "screen post-process pass" means today -
// only PostComposite/PreUI are actually wired into the live render graph at
// all, see RenderFeatureDescriptor.h's own doc comment), and auto-assigns a
// collision-tolerant priority at RUNTIME via a simple, monotonically-
// incrementing, function-local `static` counter (shared process-wide, never
// reset - a function-local static is the lower-risk, smaller-diff choice
// over a dedicated private Core member, and Core is a singleton-per-process
// composition root in practice) when the caller does not supply one
// explicitly. `RenderFeatureCompositor`'s own documented collision policy
// (RenderFeatureDescriptor.h's `priority` field doc comment) already
// tolerates a priority collision gracefully (a loud GTE_LOG_WARNING + a
// stable, deterministic lexical tie-break, never a crash) - a simple
// incrementing counter is therefore a SAFE choice even though it does not
// guarantee global uniqueness against a caller who ALSO separately calls the
// full RegisterProjectRenderFeature() with an explicit, colliding priority
// value; this is an accepted, already-documented degrade, not a new risk
// this phase introduces.
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

// Public, read-only pass-through onto FindViewData() immediately above, for
// RenderPipeline-registered callers outside Core.cpp that hold a
// RenderViewId but no access to Core's own private state.
std::optional<RenderPassViewData> Core::FindRenderPassViewData(rg::RenderViewId view) const noexcept
{
    const RenderPassViewData* viewData = FindViewData(view);
    return viewData != nullptr ? std::optional<RenderPassViewData>(*viewData) : std::nullopt;
}

// RegisterFinalizeForSamplingHook()'s own doc comment (Core.h) states the
// full contract: called once, at construction time, main-thread-only, no
// Unregister. A duplicate name is refused - logged unconditionally (so a
// release build still leaves a discoverable trace) and asserted in debug
// builds, mirroring DetectRenderPassEventContradictions()'s own precedent
// (docs/conventions/render-pass-toggle-honesty.md) rather than
// RegisterSceneServiceSlot()'s idempotent-re-registration one, since this
// mechanism has no legitimate non-error duplicate case.
bool Core::RegisterFinalizeForSamplingHook(const char* name, FinalizeForSamplingCallback callback)
{
    for (const FinalizeForSamplingHookEntry& entry : m_finalizeForSamplingHooks) {
        if (entry.name == name) {
            GTE_LOG_ERROR("Core", std::string("RegisterFinalizeForSamplingHook: duplicate name '") + name + "' - refused.");
            assert(false && "RegisterFinalizeForSamplingHook: duplicate name - see the GTE_LOG_ERROR immediately above.");
            return false;
        }
    }
    m_finalizeForSamplingHooks.push_back({ name, std::move(callback) });
    return true;
}

// Duplicate name refused the same way RegisterFinalizeForSamplingHook()
// refuses one - no legitimate duplicate case exists here either.
bool Core::RegisterViewContentPassName(const char* name)
{
    if (name == nullptr) {
        return false;
    }
    for (const std::string& existing : m_viewContentPassNames) {
        if (existing == name) {
            GTE_LOG_ERROR("Core", std::string("RegisterViewContentPassName: duplicate name '") + name + "' - refused.");
            assert(false && "RegisterViewContentPassName: duplicate name - see the GTE_LOG_ERROR above.");
            return false;
        }
    }
    m_viewContentPassNames.emplace_back(name);
    return true;
}

// Linear scan (realistic hook count is 1-3, mirrors FindViewData()'s own
// precedent) - returns nullptr both when `name` is not registered and when
// the registered callback itself returns nullptr.
RenderTexture* Core::DispatchFinalizeForSamplingHook(const char* name, VkCommandBuffer cmd)
{
    for (FinalizeForSamplingHookEntry& entry : m_finalizeForSamplingHooks) {
        if (entry.name == name) {
            entry.lastResult = entry.callback ? entry.callback(cmd) : nullptr;
            return entry.lastResult;
        }
    }
    return nullptr;
}

// Always the view's own real target - never a feature's composited output.
// See Core.h's own doc comment: a composited-output override mechanism was
// considered and deleted before any feature ever published through it.
std::optional<Core::PluginRenderFeatureTargetInfo> Core::FindPluginRenderFeatureTarget(
    const rg::RenderPassFrameContext& frame)
{
    const RenderPassViewData* viewData = FindViewData(frame.currentView);
    if (viewData == nullptr) {
        return std::nullopt;
    }

    PluginRenderFeatureTargetInfo info;
    info.target = viewData->colorTarget;
    info.extent = viewData->renderTexture != nullptr ? viewData->renderTexture->Extent() : VkExtent2D{};
    info.sampler = viewData->renderTexture != nullptr ? viewData->renderTexture->Sampler() : VK_NULL_HANDLE;
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
    // editor-core-separation-8 campaign, PHASE1 - see RenderPassToggleRegistry.h's
    // own header comment for the full contract.
    m_offscreenRenderPipeline.SetPassToggleRegistry(&m_renderPassToggleRegistry);

    // Independent kill-switch for RenderGraph's own per-pass write-target
    // capture (Frame Debugger event snapshots) - see RenderGraph::
    // SetRenderPassToggleRegistry()'s own doc comment.
    m_renderGraph.SetRenderPassToggleRegistry(&m_renderPassToggleRegistry);

    // "GpuSkinning" - ProviderScope::Once.
    m_offscreenRenderPipeline.Register("GpuSkinning", rg::ProviderScope::Once,
        [this](const rg::RenderPassFrameContext& frame, std::vector<rg::RenderPassDesc>& out) {
            GpuSkinningPipelines& pipelines = m_game.GetGpuSkinningPipelines();

            // editor-core-separation-8 campaign, PHASE1 (Step 3.4b) - a
            // single, whole-stage on/off switch: this provider is
            // ProviderScope::Once and pushes per-dispatch RenderPassDesc
            // entries under DYNAMIC names (request.name), never the literal
            // string "GpuSkinning", so RenderPipeline::DeclareOnePhase()'s
            // own choke point can never see/disable this whole stage by
            // that name - one of exactly 2 confirmed exceptions needing
            // their own direct consult of the toggle registry.
            if (!m_renderPassToggleRegistry.NoteDeclaredAndCheckEnabled("GpuSkinning")) {
                return;
            }

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

                    // better-render-pass-1 campaign, PHASE4
                    // (PHASE4_MIGRATE_CULLING_AND_GPU_SKINNING_COMPUTE_PASSES.md)
                    // - migrated onto rg::CommandBuffer (PHASE3) -
                    // DispatchOverSize() computes the correct groupX/Y/Z from
                    // the bound pipeline's own reflected LocalGroupSize()
                    // (== {256,1,1}, matching the old kSkinningLocalSizeX
                    // constant exactly) via ComputeDispatch.h's existing
                    // ComputeGroupCount3D(), degrading correctly to the
                    // previous 1D ComputeGroupCount() call for this flat
                    // vertexCount dispatch.
                    rg::CommandBuffer cmd = ctx.Cmd();
                    cmd.BindComputePipeline(pipeline);
                    cmd.BindDescriptorSet(request.descriptorSet);
                    cmd.SetPushConstants(vertexCount);
                    cmd.DispatchOverSize(vertexCount, 1, 1);
                };
                out.push_back(std::move(desc));
            }

            frame.blackboard.Publish<std::vector<rg::BufferHandle>>(
                kGpuSkinningOutputsKey, m_gpuSkinningHandlesThisFrame);
        });

    // "ClearViewTarget" - PerActiveView, BeforeEverything. Guarantees every
    // active view's color+depth target is cleared each frame, independent of
    // whether "RenderOpaque" itself runs - BeforeEverything forces this pass
    // first regardless of registration order. Permanently denylisted (never
    // toggleable): a view with no clear has no safe fallback content.
    m_offscreenRenderPipeline.Register("ClearViewTarget", rg::ProviderScope::PerActiveView,
        [this](const rg::RenderPassFrameContext& frame, std::vector<rg::RenderPassDesc>& out) {
            const RenderPassViewData* viewData = FindViewData(frame.currentView);
            if (viewData == nullptr) {
                return;
            }

            const rg::TextureHandle viewTarget = viewData->colorTarget;

            // CRITICAL - not decorative. This pass runs every frame for every
            // active view (deny-listed, PerActiveView), so adding viewTarget
            // to finalTextureOutputs here is what keeps its write (and every
            // other real writer of the same target - RenderOpaque, the
            // feature-registered view-content pass tagged AfterOpaques,
            // RenderTransparent) alive through RenderGraphCompiler's
            // reachability culling, even when every other writer is disabled.
            frame.finalTextureOutputs.push_back(viewTarget);

            rg::RenderPassDesc desc;
            desc.debugName = "ClearViewTarget";
            desc.kind = rg::PassKind::Graphics;
            desc.order = rg::RenderPassEvent::BeforeEverything;
            desc.view = frame.currentView;
            desc.legacyCategory = rg::RenderPassCategory::General;
            desc.setup = [viewTarget](rg::RenderGraphBuilder::PassBuilder& pass) {
                pass.WriteColorAttachment(viewTarget, kGameClearColor);
                pass.WriteDepthStencilAttachment(viewTarget, kGameClearDepth);
            };
            // Deliberately NO desc.execute - RenderGraph::ExecuteCompiledGraph()
            // already tolerates a null execute (`if (pass.execute) { ... }`,
            // RenderGraph.cpp) - this pass's entire job is the load-op CLEAR its
            // attachment writes above request; it issues no draw call of its own.
            out.push_back(std::move(desc));
        });

    // "PreOpaqueFeatures" - ProviderScope::PerActiveView,
    // BeforeDeferredPasses (the SAME phase "RenderOpaque" itself runs
    // in - the DEFAULT ProviderTiming, left unspecified here exactly
    // like "RenderOpaque"'s own Register() call below). better-render-
    // pass-5 effort, BLOCK 3, PHASE3
    // (task_manager/better-render-pass-5/PHASE3_CORE_PREOPAQUE_PROVIDER_AND_RUNTIME_GUARD.md).
    // Registered TEXTUALLY before "RenderOpaque"'s own Register(...)
    // call purely for human readability - this textual position is
    // NEVER what guarantees ordering (PHASE0_MASTER_STRATEGY.md, Step
    // 2): the real guarantee is entirely RenderPassEvent::PreOpaques
    // (1000) being strictly less than RenderPassEvent::Opaques (2000),
    // combined with every pass a PreOpaque callback declares being
    // tagged that way - enforced by the DeclaredPassCount()/
    // PassEventAt() check immediately below, which is the ONLY
    // mechanism in this engine that can catch a pass mistakenly left at
    // the implicit default RenderPassEvent::Opaques (the SAME tier
    // "RenderOpaque" itself uses) - DetectRenderPassEventContradictions()
    // only fires on a STRICT inequality and is structurally blind to an
    // EQUAL-tier mistake. Deliberately registered as its OWN, separate
    // provider rather than routed through the existing
    // "PluginRenderFeatures" AfterDeferredPasses provider
    // (RenderFeatureCompositor::ContributeRenderGraphPasses()) - that
    // provider's own phase is reached strictly AFTER "RenderOpaque" has
    // already been declared, unconditionally, regardless of any
    // RenderPassEvent tag (see RenderPipeline.h's own ProviderTiming
    // doc comment).
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

                // better-render-pass-5 effort, BLOCK 3, PHASE3, Step 4 of
                // the source spec - the real runtime safety net. Snapshot
                // the pass count immediately before and after invoking
                // this feature's own callback (which declares its own
                // real pass(es) IMMEDIATELY, directly against
                // frame.builder - exactly like a registered pass provider
                // already does), then hand the [before, after) range to
                // FindPassesNotTaggedPreOpaque() (PHASE1,
                // src/Core/Plugins/ProjectPreOpaqueCallback.h) - the PURE
                // logic half of this check, Tier-1 tested in isolation
                // (PHASE6). This function call site is the ONLY place in
                // the engine that turns a non-empty result into the real
                // GTE_LOG_WARNING + assert() side effects.
                const std::size_t before = frame.builder.DeclaredPassCount();
                entry.callback(frame.builder, frame.blackboard, frame.currentView); // Step 2.5 item 1 - 3-arg signature.
                const std::size_t after = frame.builder.DeclaredPassCount();

                const std::vector<std::size_t> violations =
                    FindPassesNotTaggedPreOpaque(frame.builder, before, after);
                for (const std::size_t index : violations) {
                    const rg::RenderPassEvent actual = frame.builder.PassEventAt(index);
                    GTE_LOG_WARNING("RenderFeatureCompositor",
                        "PreOpaque feature '" + entry.name + "' declared a pass (declaration index "
                        + std::to_string(index) + ") tagged RenderPassEvent::" + rg::ToString(actual)
                        + " instead of the REQUIRED RenderPassEvent::PreOpaques - this pass is not "
                        "provably guaranteed to run before \"RenderOpaque\" and may silently produce stale "
                        "or missing data for whatever reads it. See "
                        "docs/conventions/project-assembly-system.md's PreOpaque subsection.");
                    assert(false
                        && "A PreOpaque feature declared a pass not tagged RenderPassEvent::PreOpaques - see "
                           "the GTE_LOG_WARNING immediately above (category \"RenderFeatureCompositor\") for "
                           "which feature and which pass.");
                }
            }
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

            // Block 4 (task_manager/better-render-pass-6) - resolve THIS view's own
            // published scene-service handles NOW, while frame.currentView/
            // frame.blackboard are still correct for THIS specific invocation. Never
            // move this below desc.setup/desc.execute's own construction, and never
            // read frame.currentView/frame.blackboard from inside either lambda's body
            // - see PHASE7_CORE_WIRING_RENDEROPAQUE_PROVIDER.md's own hazard warning.
            std::array<std::optional<rg::TextureHandle>, kSceneServiceSlotCount> publishedServiceSlots{};
            for (std::uint32_t slotIndex = 0; slotIndex < kSceneServiceSlotCount; ++slotIndex) {
                publishedServiceSlots[slotIndex] = frame.blackboard.Fetch<rg::TextureHandle>(
                    SceneServiceBlackboardKey(slotIndex, frame.currentView));
            }
            const rg::RenderViewId currentViewForServices = frame.currentView; // plain by-value copy - see hazard warning above.

            rg::RenderPassDesc desc;
            desc.debugName = "RenderOpaque";
            desc.kind = rg::PassKind::Graphics;
            desc.order = rg::RenderPassEvent::Opaques;
            desc.view = frame.currentView;
            desc.legacyCategory = rg::RenderPassCategory::General;
            desc.setup = [viewTarget, gpuSkinningBuffers, publishedServiceSlots](rg::RenderGraphBuilder::PassBuilder& pass) {
                // LOAD is intentional here - "ClearViewTarget" (registered
                // above) already owns the one guaranteed clear of this
                // resource every frame, regardless of whether this pass runs.
                pass.WriteColorAttachment(viewTarget);
                pass.WriteDepthStencilAttachment(viewTarget);
                DeclareGpuSkinningReads(pass, gpuSkinningBuffers);
                for (std::uint32_t slotIndex = 0; slotIndex < kSceneServiceSlotCount; ++slotIndex) {
                    if (publishedServiceSlots[slotIndex].has_value()) {
                        pass.ReadTexture(*publishedServiceSlots[slotIndex], rg::ResourceAccess::ShaderRead);
                    }
                }
            };
            desc.execute = [this, aspectWidthOverHeight, isGameView, viewProjectionOverride, frameDebuggerCapture,
                                publishedServiceSlots, currentViewForServices](rg::PassContext& ctx) {
                std::array<SceneServicesDescriptorSet::ResolvedSlot, kSceneServiceSlotCount> resolvedServiceSlots{};
                for (std::uint32_t slotIndex = 0; slotIndex < kSceneServiceSlotCount; ++slotIndex) {
                    if (publishedServiceSlots[slotIndex].has_value()) {
                        const rg::PassContext::ResolvedTexture resolved = ctx.resolveReadTexture(*publishedServiceSlots[slotIndex]);
                        resolvedServiceSlots[slotIndex].view = resolved.view;
                        resolvedServiceSlots[slotIndex].sampler = resolved.sampler;
                    }
                    // else: leave both VK_NULL_HANDLE - SceneServicesDescriptorSet::Rewrite()
                    // substitutes its own dummy for any slot left this way (and also for a
                    // slot that resolved but came back with a null view or null sampler -
                    // see PHASE0 global rule 12).
                }
                const VkDescriptorSet sceneServicesSet =
                    m_sceneServicesDescriptorSet.Rewrite(currentViewForServices, resolvedServiceSlots);
                if (isGameView) {
                    // Per-draw-call granularity: CommandBuffer::Draw() opens/closes
                    // its own BeginGraphPassRecording()/EndGraphPassRecording()
                    // bracket per entity draw - do not wrap another one here.
                    rg::CommandBuffer cmd = ctx.Cmd();
                    m_game.Render(m_renderer, aspectWidthOverHeight, nullptr, frameDebuggerCapture, std::nullopt,
                        m_gpuDrivenBatchedEntitiesThisFrame, sceneServicesSet, &cmd);
                } else {
                    // Scene View draws straight through renderer.Submit() - owns
                    // its own single Begin/End bracket since nothing else does.
                    m_renderer.BeginGraphPassRecording(ctx.cmd, ctx.recordDraw);
                    m_game.Render(m_renderer, aspectWidthOverHeight, &viewProjectionOverride, nullptr, std::nullopt, {},
                        sceneServicesSet);
                    m_renderer.EndGraphPassRecording();
                }
            };
            out.push_back(std::move(desc));
        });

    // "GpuDrivenBatches" - PerActiveView.
    //
    // CORRECTNESS-CRITICAL PLACEMENT: this Register(...) call must stay
    // TEXTUALLY AFTER "RenderOpaque"'s own Register(...) call and TEXTUALLY
    // BEFORE the feature-registered view-content pass tagged AfterOpaques.
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

                // editor-core-separation-22 campaign, PHASE3
                // (PHASE2_COMPLETION_REPORT.md finding #19,
                // GpuDrivenBatchEntityExclusionLogic.h) - resolves THIS batch's
                // own "<batch> IndirectDraw" pass toggle state EARLY, before
                // that pass's own RenderPassDesc is even built below. Safe to
                // call here: RenderPassToggleRegistry::NoteDeclaredAndCheckEnabled()
                // is idempotent within a frame for a given name (see
                // RenderPassToggleGuard.h's own doc comment) - RenderPipeline::
                // DeclareOnePhase()'s own later, generic gate re-checks this
                // exact name again once this batch's IndirectDraw desc reaches
                // it below, and is guaranteed to agree with this answer.
                // RenderOpaque's own exclusion set must only ever contain this
                // batch's entities when this pass is actually going to draw
                // them - never merely because the batch was ELIGIBLE for
                // batching at collection time.
                const bool indirectDrawEnabledThisFrame = rg::ShouldDeclareBuiltInPassThisFrame(
                    &m_renderPassToggleRegistry, batch.indirectDrawPassName);
                AppendGpuDrivenBatchEntityExclusionsIfIndirectDrawEnabled(
                    indirectDrawEnabledThisFrame, batch.entities, m_gpuDrivenBatchedEntitiesThisFrame);

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

                        // better-render-pass-1 campaign, PHASE4
                        // (PHASE4_MIGRATE_CULLING_AND_GPU_SKINNING_COMPUTE_PASSES.md)
                        // - migrated onto rg::CommandBuffer (PHASE3) -
                        // DispatchOverSize() computes the correct groupX/Y/Z
                        // from CullingPipelines::Pipeline()'s own reflected
                        // LocalGroupSize() (== {256,1,1}, matching the old,
                        // now-deleted kCullingLocalSizeX constant exactly)
                        // via ComputeDispatch.h's existing
                        // ComputeGroupCount3D(), degrading correctly to the
                        // previous 1D ComputeGroupCount() call for this flat
                        // instanceCount dispatch.
                        rg::CommandBuffer cmd = ctx.Cmd();
                        cmd.BindComputePipeline(m_gpuDrivenBatchCache.Pipelines().Pipeline());
                        cmd.BindDescriptorSet(cullingDescriptorSet);
                        cmd.SetPushConstants(pc);
                        cmd.DispatchOverSize(static_cast<std::uint32_t>(instanceCount), 1, 1);
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

    // "PostOpaqueFeatures" - PerActiveView, AfterDeferredPasses. Registered
    // strictly between the deferred composite pass above and
    // "PluginRenderFeatures" so (a) anything published onto frame.blackboard
    // here is visible to "PluginRenderFeatures" in the same phase, and
    // (b) every PostOpaque pass gets a later declaration index than the
    // feature-registered view-content pass tagged AfterOpaques (both share
    // that tier), letting RenderGraphCompiler resolve it as the real writer
    // a PostOpaque pass's read depends on. Re-verify if either side's own
    // timing/phase is ever retimed.
    m_offscreenRenderPipeline.Register("PostOpaqueFeatures", rg::ProviderScope::PerActiveView,
        [this](const rg::RenderPassFrameContext& frame, std::vector<rg::RenderPassDesc>&) {
            if (m_renderFeatureCompositorPtr == nullptr) {
                return;
            }
            const RenderPassViewData* viewData = FindViewData(frame.currentView);
            const std::optional<ScenePassReadHandles> handles = ResolveScenePassReadHandles(frame.builder, viewData);
            if (!handles.has_value()) {
                return;
            }

            for (const RenderFeatureCompositor::PostOpaqueEntry& entry :
                m_renderFeatureCompositorPtr->PostOpaqueFeaturesInPriorityOrder()) {
                if (!entry.enabledOverride) {
                    continue;
                }

                const std::size_t before = frame.builder.DeclaredPassCount();
                entry.callback(frame.builder, frame.blackboard, frame.currentView, *handles);
                const std::size_t after = frame.builder.DeclaredPassCount();

                const std::vector<std::size_t> tagViolations =
                    FindPassesNotTaggedScenePass(frame.builder, before, after, rg::RenderPassEvent::AfterOpaques);
                for (const std::size_t index : tagViolations) {
                    const rg::RenderPassEvent actual = frame.builder.PassEventAt(index);
                    GTE_LOG_WARNING("RenderFeatureCompositor",
                        "PostOpaque feature '" + entry.name + "' declared a pass (declaration index "
                        + std::to_string(index) + ") tagged RenderPassEvent::" + rg::ToString(actual)
                        + " instead of the REQUIRED RenderPassEvent::AfterOpaques - this pass is not provably "
                        "guaranteed to run in the right position. See "
                        "docs/conventions/project-assembly-system.md's PostOpaque subsection.");
                    assert(false
                        && "A PostOpaque feature declared a pass not tagged RenderPassEvent::AfterOpaques - see "
                           "the GTE_LOG_WARNING immediately above (category \"RenderFeatureCompositor\") for "
                           "which feature and which pass.");
                }

                if (FindScenePassCallbackMissingReadDeclaration(
                        frame.builder, before, after, handles->colorHandle, handles->depthHandle)) {
                    GTE_LOG_WARNING("RenderFeatureCompositor",
                        "PostOpaque feature '" + entry.name + "' declared at least one pass but never declared a "
                        "ReadTexture() usage against the color/depth handles it was given - this is an "
                        "undeclared, unbarriered GPU read if its own execute lambda samples them anyway. See "
                        "docs/conventions/project-assembly-system.md's PostOpaque subsection.");
                    assert(false
                        && "A PostOpaque feature declared a pass without declaring a matching ReadTexture() - see "
                           "the GTE_LOG_WARNING immediately above (category \"RenderFeatureCompositor\").");
                }
            }
        },
        rg::ProviderTiming::AfterDeferredPasses);

    // "PostTransparentFeatures" - ProviderScope::PerActiveView,
    // AfterDeferredPasses. Registered after "PostOpaqueFeatures" and before
    // "PluginRenderFeatures", for the same two reasons PostOpaqueFeatures'
    // own doc comment above gives, applied against whatever AfterTransparents-
    // tier, AfterDeferredPasses-phase pass is registered earlier (today: the
    // deferred composite pass that reads the view's color/depth and writes a
    // separate output texture) - sharing its exact tier+phase is safe by
    // construction today, but does not automatically generalize to some
    // future third same-tier, same-phase provider that also writes into the
    // view's own color/depth handle.
    m_offscreenRenderPipeline.Register("PostTransparentFeatures", rg::ProviderScope::PerActiveView,
        [this](const rg::RenderPassFrameContext& frame, std::vector<rg::RenderPassDesc>&) {
            if (m_renderFeatureCompositorPtr == nullptr) {
                return;
            }
            const RenderPassViewData* viewData = FindViewData(frame.currentView);
            const std::optional<ScenePassReadHandles> handles = ResolveScenePassReadHandles(frame.builder, viewData);
            if (!handles.has_value()) {
                return;
            }

            for (const RenderFeatureCompositor::PostTransparentEntry& entry :
                m_renderFeatureCompositorPtr->PostTransparentFeaturesInPriorityOrder()) {
                if (!entry.enabledOverride) {
                    continue;
                }

                const std::size_t before = frame.builder.DeclaredPassCount();
                entry.callback(frame.builder, frame.blackboard, frame.currentView, *handles);
                const std::size_t after = frame.builder.DeclaredPassCount();

                const std::vector<std::size_t> tagViolations = FindPassesNotTaggedScenePass(
                    frame.builder, before, after, rg::RenderPassEvent::AfterTransparents);
                for (const std::size_t index : tagViolations) {
                    const rg::RenderPassEvent actual = frame.builder.PassEventAt(index);
                    GTE_LOG_WARNING("RenderFeatureCompositor",
                        "PostTransparent feature '" + entry.name + "' declared a pass (declaration index "
                        + std::to_string(index) + ") tagged RenderPassEvent::" + rg::ToString(actual)
                        + " instead of the REQUIRED RenderPassEvent::AfterTransparents - this pass is not "
                        "provably guaranteed to run in the right position. See "
                        "docs/conventions/project-assembly-system.md's PostTransparent subsection.");
                    assert(false
                        && "A PostTransparent feature declared a pass not tagged RenderPassEvent::AfterTransparents "
                           "- see the GTE_LOG_WARNING immediately above (category \"RenderFeatureCompositor\").");
                }

                if (FindScenePassCallbackMissingReadDeclaration(
                        frame.builder, before, after, handles->colorHandle, handles->depthHandle)) {
                    GTE_LOG_WARNING("RenderFeatureCompositor",
                        "PostTransparent feature '" + entry.name + "' declared at least one pass but never "
                        "declared a ReadTexture() usage against the color/depth handles it was given - this is an "
                        "undeclared, unbarriered GPU read if its own execute lambda samples them anyway. See "
                        "docs/conventions/project-assembly-system.md's PostTransparent subsection.");
                    assert(false
                        && "A PostTransparent feature declared a pass without declaring a matching ReadTexture() - "
                           "see the GTE_LOG_WARNING immediately above (category \"RenderFeatureCompositor\").");
                }
            }
        },
        rg::ProviderTiming::AfterDeferredPasses);

    // This provider's body is one generic loop over every registered
    // IPluginCapabilityOrchestrator (m_capabilityOrchestrators, populated
    // once by RegisterBuiltinCapabilityOrchestrators()). Still the LAST
    // Register() call in this function, after every deferred-phase pass
    // above.
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
    // editor-core-separation-8 campaign, PHASE1 - see RenderPassToggleRegistry.h's
    // own header comment for the full contract.
    m_presentRenderPipeline.SetPassToggleRegistry(&m_renderPassToggleRegistry);

    m_presentRenderPipeline.Register("Present", rg::ProviderScope::Once,
        [this](const rg::RenderPassFrameContext& frame, std::vector<rg::RenderPassDesc>&) {
            const std::vector<rg::BufferHandle> gpuSkinningBuffers = m_needsDirectGameRenderThisFrame
                ? AddGpuSkinningPasses(frame.builder, m_game, m_renderer, &m_renderPassToggleRegistry)
                : std::vector<rg::BufferHandle>{};

            AddPresentPass(frame.builder, m_game, m_renderer, m_swapchainImageThisFrame,
                m_directGameRenderAspectThisFrame, m_recordImGuiThisFrame, gpuSkinningBuffers);
        });
}

void Core::BuildFrame()
{
    // editor-core-separation-27 campaign, PHASE7 (BIG STEP 3 of 4) - must
    // run EXACTLY once per real engine frame, unconditionally, strictly
    // before either ExecuteTimingMode regime's Execute() call this frame -
    // see BIG_STEP_3 Section 8. Core::Present() (a SEPARATE method, called
    // AFTER this one returns by EditorHost::Run()'s own per-frame loop)
    // issues the OTHER regime's Execute() call - this call must precede
    // BOTH.
    m_renderGraph.BeginPersistentResourceFrame();

    // Reset every finalize-for-sampling hook's cached result UNCONDITIONALLY,
    // before any early-exit below - a hook's callback does not necessarily
    // run every frame (e.g. neither Game nor Scene view rendered at all this
    // frame), so a stale, several-frames-old pointer never lingers.
    for (FinalizeForSamplingHookEntry& entry : m_finalizeForSamplingHooks) {
        entry.lastResult = nullptr;
    }

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

                    // editor-core-separation-22 campaign, PHASE6
                    // (PHASE6_IRON_RULE_V2_BIDIRECTIONAL_DETECTOR.md, Step
                    // 3.3 item 2) - `blackboard` is now a real Core MEMBER
                    // (m_offscreenBlackboardThisFrame, see Core.h's own doc
                    // comment on it) rather than a plain stack local, so it
                    // survives past this callback's own return - the SAME
                    // object Application/EditorHost's own IEditorLayer::
                    // BuildUI() call later reads back through
                    // Core::GetOffscreenBlackboardForFrameDebugger() for the
                    // new Clause C "disabled side effect still visible"
                    // detector. `.BeginFrame()` is still called here, every
                    // frame, exactly as before - this promotion changes
                    // nothing about WHEN/HOW OFTEN this blackboard's own
                    // contents are cleared and rebuilt, only how long the
                    // object itself lives.
                    m_offscreenBlackboardThisFrame.BeginFrame();
                    rg::RenderPassFrameContext frame{
                        {}, rg::RenderViewId::Shared(), m_offscreenBlackboardThisFrame, b, {}, {} };

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

                        const Vec3 gameEyeWorldPosition = RenderSystem::ResolveActiveCameraWorldPosition(m_game.GetRegistry());
                        const Mat4 gameViewProjection =
                            RenderSystem::ResolveActiveCameraViewProjection(m_game.GetRegistry(), aspect);

                        const rg::TextureHandle h = b.ImportTexture("GameView", gameTarget->Target(),
                            VK_IMAGE_LAYOUT_UNDEFINED, gameTarget->Sampler(), gameTarget->DepthSampler(), gameTarget);

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
                                // editor-core-separation-22 campaign, PHASE3
                                // (PHASE2_COMPLETION_REPORT.md finding #19) -
                                // this batch's own entities are no longer
                                // inserted into m_gpuDrivenBatchedEntitiesThisFrame
                                // HERE, unconditionally, before this batch's
                                // own "<batch> IndirectDraw" pass toggle state
                                // is even known - carried on `data` instead,
                                // and only folded into the exclusion set by
                                // the "GpuDrivenBatches" provider below, once
                                // that SPECIFIC pass is confirmed to have
                                // survived its own toggle check this frame.
                                data.entities.reserve(frameEntry.commands.size());
                                for (const DrawCommand& command : frameEntry.commands) {
                                    data.entities.push_back(command.entity);
                                }
                                m_gpuDrivenBatchesThisFrame.push_back(std::move(data));
                            }
                        }
                    }

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

                        const rg::TextureHandle h = b.ImportTexture("SceneView", sceneTarget->Target(),
                            VK_IMAGE_LAYOUT_UNDEFINED, sceneTarget->Sampler(), sceneTarget->DepthSampler(), sceneTarget);

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
                    }

                    m_offscreenRenderPipeline.DeclareInto(b, frame);

                    const std::optional<std::function<void(VkCommandBuffer)>> viewBackgroundCallbackForReplay =
                        m_offscreenBlackboardThisFrame.Fetch<std::function<void(VkCommandBuffer)>>(
                            kViewBackgroundReplayCallbackKey);
#ifndef NDEBUG
                    m_offscreenBlackboardThisFrame.ReportUnusedPublishesIfAny();
#endif

                    // A PreOpaque/PostOpaque/PostTransparent callback has no access to
                    // RenderPassFrameContext::finalTextureOutputs - it marks a plain texture
                    // output as a required root through the builder instead. Merge it into
                    // this frame's own root set here.
                    for (const rg::TextureHandle& handle : b.FinalTextureOutputs()) {
                        frame.finalTextureOutputs.push_back(handle);
                    }

                    std::vector<rg::TextureHandle> outputs = std::move(frame.finalTextureOutputs);

                    if (gameTarget != nullptr && frameDebuggerCapture != nullptr && m_editorLayer != nullptr
                        && m_editorLayer->ConsumePendingFrameDebuggerReplayRequest()) {
                        const std::vector<rg::BufferHandle> gpuSkinningBuffersForReplay =
                            m_offscreenBlackboardThisFrame.Fetch<std::vector<rg::BufferHandle>>(kGpuSkinningOutputsKey)
                                .value_or(std::vector<rg::BufferHandle>{});
                        const std::function<void(VkCommandBuffer)> recordViewBackground =
                            viewBackgroundCallbackForReplay.value_or(std::function<void(VkCommandBuffer)>{});
                        const std::size_t objectCount = m_game.CountGameViewDrawCommandsThisFrame();
                        const std::vector<rg::TextureHandle> replayStepHandles = frameDebuggerCapture->AddReplayPasses(
                            b, m_game, m_renderer, gameAspectForReplay, objectCount, gpuSkinningBuffersForReplay,
                            recordViewBackground, *gameTarget, &m_renderPassToggleRegistry);
                        for (const rg::TextureHandle& replayHandle : replayStepHandles) {
                            outputs.push_back(replayHandle);
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
                gameTarget->FinalizeForExternalSampling(offscreenCmd);
                m_renderGraph.NotifyDebugTextureStateOverride(
                    "GameView", rg::RequiredStateFor(rg::ResourceAccess::ShaderRead, false));

                RenderTexture* composited = DispatchFinalizeForSamplingHook("GameViewComposited", offscreenCmd);
                if (composited != nullptr) {
                    m_renderGraph.NotifyDebugTextureStateOverride(
                        "GameViewComposited", rg::RequiredStateFor(rg::ResourceAccess::ShaderRead, false));
                }
                if (m_editorLayer != nullptr) {
                    // Pass through as-is, every frame, including null - readers
                    // already fall back to the live view on their own.
                    m_editorLayer->SetGameViewCompositedTexture(composited);
                }
            }
            if (sceneTarget != nullptr) {
                sceneTarget->FinalizeForExternalSampling(offscreenCmd);
                m_renderGraph.NotifyDebugTextureStateOverride(
                    "SceneView", rg::RequiredStateFor(rg::ResourceAccess::ShaderRead, false));

                RenderTexture* composited = DispatchFinalizeForSamplingHook("SceneViewComposited", offscreenCmd);
                if (composited != nullptr) {
                    m_renderGraph.NotifyDebugTextureStateOverride(
                        "SceneViewComposited", rg::RequiredStateFor(rg::ResourceAccess::ShaderRead, false));
                }
                if (m_editorLayer != nullptr) {
                    m_editorLayer->SetSceneViewCompositedTexture(composited);
                }
            }
            // Generic sweep: any other registered finalize-for-sampling hook
            // (today: none) runs unconditionally, once per frame.
            for (FinalizeForSamplingHookEntry& entry : m_finalizeForSamplingHooks) {
                if (entry.name == "GameViewComposited" || entry.name == "SceneViewComposited") {
                    continue;
                }
                entry.lastResult = entry.callback(offscreenCmd);
            }
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

    // GPU stats for every registered view-content pass (RenderOpaque/
    // RenderTransparent plus whatever features registered via
    // RegisterViewContentPassName()) - same name list for both views, so
    // built once rather than duplicated per view.
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
