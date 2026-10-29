#include "EditorHost.h"

#include "Logger.h"
#include "EditorSceneIOCapability.h"
#include "EditorLogQueryCapability.h"
#include "EditorHotReloadDebugCapability.h"
#include "EditorProjectLifecycleCapability.h"
#include "ActiveProjectAssemblyState.h"
// ResolveProjectAssemblyOutputDirectory().
#include "../Core/Plugins/ProjectAssemblyBuildRunner.h"
// PerformProjectAssemblyHotReload().
#include "../Core/Plugins/ProjectAssemblyHotReload.h"
// Built-in feature modules are created generically through this registry.
#include "../Core/Plugins/BuiltinFeatureModuleRegistry.h"
#include "../Core/FeatureKit/FeatureRegistration.h"
#include "ProjectRootPath.h" // ExecutableDirectory().
#include "CaptureFailureReason.h"
#include "ExitGate.h"
#include "VolumeTexturePreviewSelection.h"
#include "../Core/EditorPanelRegistry.h"
// RenderFeatureCompositor::DebugSnapshot() needs the full type (Core.h only forward-declares it).
#include "../Core/Plugins/RenderFeatureCompositor.h"
#include "../Application/EventTranslator.h"
#include "../Application/EngineCommandDispatch.h"
#include "../Application/MemorySnapshotBuilder.h"
#include "../Assets/AtomicFile.h" // SweepStaleTempFiles().
#include "../Assets/AssetTypes.h"
#include "../Assets/GtaFile.h"
#include "ClusterToolUiState.h" // ValidateMeshAssetPath().
#include "../Encoding/Utf8Path.h"
#include "MeshClusterBuildRunner.h"
#include "EditorGpuMemoryNameOverlay.h"
#include "../Encoding/CapturedPixelConversion.h" // gte::Encoding::IsBgraFormat() - the one canonical copy.
#include "../Encoding/DepthVisualization.h"
#include "../Encoding/HdrColorVisualization.h"
#include "../Encoding/PixelConversion.h"
#include "../Encoding/PngEncoder.h"
#include "../Encoding/SingleChannelVisualization.h"
#include "../Memory/SdlMemoryTracker.h"
#include "../Profiling/FrameProfiler.h"
#include "../Profiling/ProfilerSnapshotBuilder.h"
#include "../Profiling/Overhead.h"
#include "../Profiling/ScopeTimer.h"
#include "../Renderer/GpuClockCalibration.h"
#include "../Renderer/RenderGraph/RenderGraphBuilder.h"
#include "../Renderer/RenderGraph/RenderGraphDebugTextureRegistry.h"
// BuildRenderGraphMetadata() for GET /render_graph.
#include "../Renderer/RenderGraph/RenderGraphMetadata.h"

#include <SDL3/SDL.h>

#include <algorithm>
#include <cassert>
#include <cctype>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <optional>
#include <stdexcept>
#include <string_view>
#include <utility>

namespace gte {

namespace {

// One sample per live GPU allocation, named through the editor's debug-name overlay (empty when unnamed).
std::vector<rg::GpuAllocationSample> CollectGpuAllocationSamples(const Renderer& renderer)
{
    const std::vector<GpuMemoryTracker::Entry> entries = renderer.GetMemoryResources();
    std::vector<rg::GpuAllocationSample> samples;
    samples.reserve(entries.size());
    for (const GpuMemoryTracker::Entry& entry : entries) {
        samples.push_back({ EditorGpuMemoryNameOverlay::GetDebugName(entry.handle), entry.record.sizeBytes });
    }
    return samples;
}

// GET /list_textures label for an execution regime; an unrecognized one falls back to a safe string.
const char* ToDebugTextureRegimeString(rg::ExecuteTimingMode mode)
{
    switch (mode) {
    case rg::ExecuteTimingMode::SynchronousImmediateReadback: return "synchronous";
    case rg::ExecuteTimingMode::PipelinedDeferredReadback: return "pipelined";
    }
    return "unknown"; // Unreachable - every real enumerator handled above.
}

// The one log query capability instance. A stateless namespace-scope static: its address is needed in the
// member-initializer list (for m_networkServer), before the constructor body runs.
EditorLogQueryCapability s_editorLogQueryCapability;

// The one hot reload debug capability instance; used by the network server and by the engine command call site.
EditorHotReloadDebugCapability s_editorHotReloadDebugCapability;

// The one project lifecycle capability instance; a namespace-scope static for the same reason as above.
EditorProjectLifecycleCapability s_editorProjectLifecycleCapability;

// Delivers a result to the waiting caller; logs when the request already timed out.
template <typename Bridge, typename Result>
void FulfillOrLogDrop(Bridge& bridge, std::uint64_t id, Result&& result, const char* kind)
{
    if (!bridge.FulfillCommand(id, std::forward<Result>(result))) {
        GTE_LOG_WARNING("EditorHost", std::string("Dropped a late result for a timed-out command (kind=") + kind + ")");
    }
}

BuildMeshClustersOutcome MakeClusterRefusal(BuildMeshClustersStatus status, std::string message)
{
    BuildMeshClustersOutcome outcome;
    outcome.status = status;
    outcome.message = std::move(message);
    return outcome;
}

BuildMeshClustersOutcome ToBridgeOutcome(const MeshClusterFinishedBuild& finished)
{
    const MeshClusterReport& report = finished.outcome.report;
    BuildMeshClustersOutcome outcome;
    outcome.success = finished.outcome.success;
    outcome.status = outcome.success ? BuildMeshClustersStatus::Ok : BuildMeshClustersStatus::BadRequest;
    outcome.message = finished.outcome.message;
    outcome.weld = report.weld == ClusterWeldMode::PositionOnly ? "position_only" : "exact";
    outcome.vertexCountBefore = report.vertexCountBefore;
    outcome.vertexCountAfter = report.vertexCountAfter;
    outcome.triangleCount = report.triangleCount;
    outcome.clusterCount = report.clusterCount;
    outcome.avgTrianglesPerCluster = report.avgTrianglesPerCluster;
    outcome.avgVerticesPerCluster = report.avgVerticesPerCluster;
    outcome.poorFill = report.poorFill;
    outcome.milliseconds = report.milliseconds;
    return outcome;
}

CloneMeshClustersOutcome MakeCloneRefusal(BuildMeshClustersStatus status, std::string message)
{
    CloneMeshClustersOutcome outcome;
    outcome.status = status;
    outcome.message = std::move(message);
    return outcome;
}

CloneMeshClustersOutcome ToCloneBridgeOutcome(const MeshClusterFinishedBuild& finished)
{
    const MeshClusterReport& report = finished.outcome.report;
    CloneMeshClustersOutcome outcome;
    outcome.success = finished.outcome.success;
    outcome.status = outcome.success ? BuildMeshClustersStatus::Ok : BuildMeshClustersStatus::BadRequest;
    outcome.message = finished.outcome.message;
    outcome.createdPath = PathToUtf8(finished.createdPath);
    outcome.guid = finished.guid.ToString();
    outcome.weldUsed = finished.weldUsed == ClusterWeldMode::PositionOnly ? "position_only" : "exact";
    outcome.weldFellBack = finished.weldFellBack;
    outcome.vertexCountBefore = report.vertexCountBefore;
    outcome.vertexCountAfter = report.vertexCountAfter;
    outcome.triangleCount = report.triangleCount;
    outcome.clusterCount = report.clusterCount;
    outcome.avgTrianglesPerCluster = report.avgTrianglesPerCluster;
    outcome.milliseconds = report.milliseconds;
    return outcome;
}

// Runs one viewer operation. The path of Open goes through the project-root check first.
ClusterViewerOutcome RunClusterViewerCommand(
    const ClusterViewerCommand& command, IClusterViewerControl* control, Renderer& renderer)
{
    ClusterViewerOutcome out;
    if (control == nullptr) {
        out.available = false;
        out.result = { false, ClusterViewerError::Unavailable, "the cluster viewer is not available in this build" };
        return out;
    }
    switch (command.operation) {
    case ClusterViewerOperation::Open: {
        std::filesystem::path validated;
        if (const std::optional<std::string> invalid =
                ValidateMeshAssetPath(command.path, ResolveProjectRootDirectory(), validated)) {
            out.result = { false, ClusterViewerError::BadInput, *invalid };
        } else {
            out.result = control->Open(renderer, PathToUtf8(validated));
        }
        break;
    }
    case ClusterViewerOperation::State:
        out.result.ok = true;
        break;
    case ClusterViewerOperation::Set:
        out.result = control->Set(command.pairs);
        break;
    case ClusterViewerOperation::Pick: {
        const ClusterViewerPickResult pick = control->Pick(command.x, command.y);
        out.result = { pick.ok, pick.error, pick.message };
        out.hit = pick.hit;
        break;
    }
    case ClusterViewerOperation::Verify:
        out.result = control->Verify();
        break;
    case ClusterViewerOperation::CaptureHiz:
        out.result = control->CaptureHiz();
        break;
    case ClusterViewerOperation::Image:
        out.image = control->CaptureImage(command.target, command.overlay);
        if (out.image.has_value()) {
            out.result.ok = true;
        } else {
            out.result = { false, ClusterViewerError::NotOpen,
                "no image: the viewer is not open, has no valid mesh, or the camera preview is off" };
        }
        return out;
    }
    out.report = control->Report();
    return out;
}

// Runs one Scene camera operation.
SceneCameraOutcome RunSceneCameraCommand(const SceneCameraCommand& command, ISceneCameraControl* control)
{
    SceneCameraOutcome out;
    if (control == nullptr) {
        out.available = false;
        out.result = { false, SceneCameraError::Unavailable, "the Scene cameras are not available in this build" };
        return out;
    }
    switch (command.operation) {
    case SceneCameraOperation::State:
        out.result.ok = true;
        break;
    case SceneCameraOperation::Select:
        out.result = control->Select(command.name);
        break;
    case SceneCameraOperation::Pick: {
        const SceneCameraPickResult pick = control->Pick(command.x, command.y);
        out.result = { pick.ok, pick.error, pick.message };
        out.hit = pick.hit;
        out.entityName = pick.entityName;
        break;
    }
    case SceneCameraOperation::SetPose: {
        SceneCameraPose pose;
        pose.position[0] = command.poseX;
        pose.position[1] = command.poseY;
        pose.position[2] = command.poseZ;
        pose.yawDegrees = command.yawDegrees;
        pose.pitchDegrees = command.pitchDegrees;
        out.result = control->SetPose(pose);
        break;
    }
    case SceneCameraOperation::Image:
        out.image = control->CaptureImage(command.overlay);
        if (out.image.has_value()) {
            out.result.ok = true;
        } else {
            out.result = { false, SceneCameraError::NotRendered, "the Scene view was not rendered" };
        }
        return out;
    }
    out.report = control->Report();
    return out;
}

// Runs one editor UI command on the layer; main thread only.
EditorUiCommandResult RunEditorUiCommand(const EditorUiCommandRequest& request, IEditorLayer& layer, Game& game,
    Renderer& renderer, DescriptorSetLayoutHandle sceneServicesSetLayout)
{
    EditorUiCommandResult result;
    result.kind = request.kind;
    switch (request.kind) {
    case EditorUiCommandKind::ActivateTab: {
        const TabActivationResult activation = layer.ActivateTab(request.activateTab.tabName);
        result.activateTab.tabExists = activation.tabExists;
        result.activateTab.success = activation.tabExists;
        break;
    }
    case EditorUiCommandKind::SpawnInstanceGrid: {
        const InstanceGridSpawnLayerResult spawned = layer.SpawnInstanceGrid(game, renderer,
            request.spawnInstanceGrid.instanceCount, request.spawnInstanceGrid.spacing, sceneServicesSetLayout);
        result.spawnInstanceGrid.success = spawned.success;
        result.spawnInstanceGrid.editorAvailable = spawned.editorAvailable;
        result.spawnInstanceGrid.errorMessage = spawned.errorMessage;
        result.spawnInstanceGrid.instanceCount = spawned.instanceCount;
        break;
    }
    case EditorUiCommandKind::SelectAsset: {
        const AssetSelectionResult selected = layer.SelectProjectAsset(request.selectAsset.absolutePath);
        result.selectAsset.success = selected.success;
        result.selectAsset.editorAvailable = selected.editorAvailable;
        result.selectAsset.message = selected.message;
        break;
    }
    case EditorUiCommandKind::InspectorState: {
        InspectorStateView view = layer.InspectorState();
        InspectorStateOutcome& out = result.inspectorState;
        out.editorAvailable = view.editorAvailable;
        out.selectionKind = std::move(view.selectionKind);
        out.selectedPath = std::move(view.selectedPath);
        out.shownPath = std::move(view.shownPath);
        out.clustersText = std::move(view.clustersText);
        out.clusterStatus = std::move(view.clusterStatus);
        out.buildEnabled = view.buildEnabled;
        out.buildDisabledReason = std::move(view.buildDisabledReason);
        out.buildRunning = view.buildRunning;
        out.cloneButtonDisabledReason = std::move(view.cloneButtonDisabledReason);
        out.cloneDestination = std::move(view.cloneDestination);
        out.cloneWeld = std::move(view.cloneWeld);
        out.openViewerAfterClone = view.openViewerAfterClone;
        break;
    }
    case EditorUiCommandKind::InspectorBuildClusters: {
        const InspectorBuildClustersCommand& command = request.inspectorBuildClusters;
        InspectorClusterOptions options;
        options.positionOnlyWeld = command.weld == "position_only";
        options.maxVertices = command.maxVertices;
        options.maxTriangles = command.maxTriangles;
        options.coneWeight = command.coneWeight;
        ClusterBuildStartResult started = layer.InspectorBuildClusters(options);
        InspectorBuildClustersOutcome& out = result.inspectorBuildClusters;
        switch (started.status) {
        case ClusterBuildStart::Started: out.status = InspectorBuildClustersStatus::Started; break;
        case ClusterBuildStart::Busy: out.status = InspectorBuildClustersStatus::Busy; break;
        case ClusterBuildStart::Unavailable: out.status = InspectorBuildClustersStatus::Unavailable; break;
        case ClusterBuildStart::Refused: out.status = InspectorBuildClustersStatus::Refused; break;
        case ClusterBuildStart::Ready: out.status = InspectorBuildClustersStatus::Unavailable; break; // Gate-only value.
        }
        out.message = std::move(started.message);
        break;
    }
    case EditorUiCommandKind::SplitViews: {
        const SplitViewsLayerResult split = layer.SetSceneGameSplit(request.splitViews.enabled);
        result.splitViews.success = split.success;
        result.splitViews.split = split.split;
        result.splitViews.message = split.message;
        break;
    }
    case EditorUiCommandKind::ClusterViewer:
        result.clusterViewer = RunClusterViewerCommand(request.clusterViewer, layer.ClusterViewerControl(), renderer);
        break;
    case EditorUiCommandKind::SceneCamera:
        result.sceneCamera = RunSceneCameraCommand(request.sceneCamera, layer.SceneCameraControl());
        break;
    case EditorUiCommandKind::HierarchyState:
        result.hierarchyState = layer.HierarchyState(game, request.hierarchyState.first, request.hierarchyState.count);
        break;
    case EditorUiCommandKind::Exit:
        break; // The result kind is the answer; Run() ends the loop.
    }
    return result;
}

// Runs once per finished job, whichever door started it. A build drops the cached GPU mesh and refreshes the
// asset record; a clone leaves the source alone and hands the new file to the editor layer.
void RunClusterBuildFollowUp(const MeshClusterFinishedBuild& finished, Game& game, IEditorLayer& editorLayer)
{
    const std::string pathUtf8 = PathToUtf8(finished.path);
    if (!finished.outcome.success) {
        GTE_LOG_WARNING("MeshClustering",
            std::string(finished.wasClone ? "Cluster clone failed for " : "Cluster build failed for ") + pathUtf8 + ": "
                + finished.outcome.message);
        return;
    }
    const MeshClusterReport& report = finished.outcome.report;
    if (finished.wasClone) {
        const std::string createdUtf8 = PathToUtf8(finished.createdPath);
        const char* weld = finished.weldUsed == ClusterWeldMode::PositionOnly ? "position only" : "exact";
        GTE_LOG_INFO("MeshClustering",
            "ClusterClone: " + pathUtf8 + " -> " + createdUtf8 + ", " + std::to_string(report.clusterCount)
                + " clusters, weld " + weld);
        editorLayer.OnMeshCloneFinished(createdUtf8, finished.openViewerAfterClone);
        return;
    }
    MeshInstantiationSystem& meshes = game.GetMeshInstantiationSystem();
    meshes.InvalidateCachedMeshAsset(pathUtf8);
    const std::string normalized = PathToUtf8(finished.path.lexically_normal().make_preferred());
    if (normalized != pathUtf8) {
        meshes.InvalidateCachedMeshAsset(normalized);
    }
    editorLayer.RefreshProjectAssetRecord(pathUtf8);
    GTE_LOG_INFO("MeshClustering",
        "Cluster build finished for " + pathUtf8 + ": " + std::to_string(report.clusterCount) + " clusters, vertices "
            + std::to_string(report.vertexCountBefore) + " -> " + std::to_string(report.vertexCountAfter) + ", "
            + std::to_string(static_cast<std::int64_t>(report.milliseconds + 0.5)) + " ms"
            + (report.poorFill ? ", poor fill" : ""));
}

} // namespace

EditorHost::SdlContext::SdlContext()
{
    // The memory tracker must be installed before any SDL call, including SDL_Init().
    SdlMemoryTracker::Install();

    if (!SDL_Init(SDL_INIT_VIDEO)) {
        throw std::runtime_error(std::string("SDL_Init failed: ") + SDL_GetError());
    }
}

EditorHost::SdlContext::~SdlContext()
{
    SDL_Quit();
}

EditorHost::EditorHost(const std::string& title, int width, int height, bool forceGpuValidation, PresentPolicy presentPolicy)
    : m_sdlContext()
    , m_window(title, width, height)
    // m_core is built from m_window (ISurfaceProvider) and m_hostServices (IHostServices); both exist by now.
    , m_hostServices()
    , m_core(m_window, m_hostServices, forceGpuValidation, presentPolicy)
    // References bound to m_core's own instances.
    , m_renderer(m_core.GetRenderer())
    , m_renderGraph(m_core.GetRenderGraph())
    // CreateEditorLayer() resolves to the ImGui-backed implementation.
    , m_editorLayer(CreateEditorLayer(m_window, m_renderer))
    , m_game(m_core.GetGame())
    , m_engineContext(m_core.GetEngineContext())
    // The network server gets the bridges and capabilities its routes need; they are declared (and
    // constructed) before m_networkServer. The lifecycle capability is passed twice via its two bases.
    , m_networkServer(&m_captureBridge, &m_commandBridge, &m_uiCommandBridge, &m_frameDebuggerCommandBridge,
          &m_assetImportCommandBridge, &s_editorLogQueryCapability, &m_renderGraphControlCommandBridge,
          &s_editorHotReloadDebugCapability, &s_editorProjectLifecycleCapability, &s_editorProjectLifecycleCapability,
          &m_featureDebugCommandBridge, &m_profilerBridge)
{
    m_renderer.LogValidationState();

    // Removes orphaned temp files of crashed writes before any job or import can run.
    if (const std::size_t sweptTempFiles = SweepStaleTempFiles(ResolveProjectRootDirectory()); sweptTempFiles > 0) {
        GTE_LOG_INFO("EditorHost", "Removed " + std::to_string(sweptTempFiles) + " stale temp file(s) from the project folder.");
    }

    // Installs the pass-debug metadata sink and provider here: CreateEditorLayer() has no RenderGraph.
    // Order is irrelevant: Execute() only runs later, from Core::BuildFrame().
    m_renderGraph.SetDebugMetadataSink(&m_passMetadataRecorder);
    m_renderGraph.SetDebugMetadataProvider(&m_passMetadataRecorder);

    // Installs the per-draw event sink owned by the Frame Debugger panel (null in a release build).
    if (FrameDebuggerEventSink* eventSink = m_editorLayer->FrameDebuggerGetEventSinkForInstall()) {
        m_renderGraph.SetFrameDebuggerEventSink(eventSink);
    }

    // Core::BuildFrame() needs a non-null editor layer from here on; the assert catches a missed or wrong call at startup.
    assert(m_editorLayer != nullptr && "EditorHost: m_editorLayer must be non-null before SetEditorLayerHook()");
    m_core.SetEditorLayerHook(m_editorLayer.get());

    // Gives the editor layer the project lifecycle capability (a namespace-scope static, alive before this body runs).
    m_editorLayer->SetProjectLifecycleCapability(&s_editorProjectLifecycleCapability);

    // Gives the editor layer the asset scaffolding capability: the same object as the lifecycle capability, via its second base.
    m_editorLayer->SetAssetScaffoldingCapability(&s_editorProjectLifecycleCapability);

    // Gives the editor layer the hot reload debug capability (the same object the network server uses).
    m_editorLayer->SetHotReloadDebugCapability(&s_editorHotReloadDebugCapability);

    // Hands the debug capability the project assembly host before the network server accepts requests.
    // Unconditional: Core always owns the host; only the load call below is gated.
    s_editorHotReloadDebugCapability.SetProjectAssemblyHost(m_core.GetProjectAssemblyHost());

    // Gives the active project state a live ProjectAssemblyHost so its isLoaded field is real.
    ActiveProjectAssemblyState::Instance().SetProjectAssemblyHost(m_core.GetProjectAssemblyHost());

    // Gives the hot reload capability the hot reload command bridge (already constructed by now).
    s_editorHotReloadDebugCapability.SetHotReloadCommandBridge(m_hotReloadCommandBridge);

    // Gives the hot reload capability the general command bridge (already constructed by now).
    s_editorHotReloadDebugCapability.SetEngineCommandBridge(m_commandBridge);

    // Gives the project lifecycle capability its load bridge and live Core/EditorHost references.
    s_editorProjectLifecycleCapability.SetLoadCommandBridge(m_projectLifecycleLoadCommandBridge);
    s_editorProjectLifecycleCapability.SetEngineReferences(m_core, this);

    // Core only stores this callback; the editor's own Render(cmd) is still called from here, on EditorHost.
    m_core.SetPresentImGuiRecorder([this](const rg::RecordingContext& cmd) { m_editorLayer->Render(cmd); });

    // Seeds the panel registry with every built-in panel name before any plugin panel registers,
    // so built-ins always list first. The list must match the panel catalog.
    EditorPanelRegistry::Instance().RegisterBuiltinPanelName("Hierarchy");
    EditorPanelRegistry::Instance().RegisterBuiltinPanelName("Inspector");
    EditorPanelRegistry::Instance().RegisterBuiltinPanelName("Scene");
    EditorPanelRegistry::Instance().RegisterBuiltinPanelName("Game");
    EditorPanelRegistry::Instance().RegisterBuiltinPanelName("Memory");
    EditorPanelRegistry::Instance().RegisterBuiltinPanelName("Profiler");
    EditorPanelRegistry::Instance().RegisterBuiltinPanelName("Render Graph");
    EditorPanelRegistry::Instance().RegisterBuiltinPanelName("Jobs");
    EditorPanelRegistry::Instance().RegisterBuiltinPanelName("Log");
#if GTE_ENABLE_PROJECT_PANEL
    EditorPanelRegistry::Instance().RegisterBuiltinPanelName("Project");
#endif

    // Project assemblies load once, from their own output folder (never "plugins/"); `this` is the EditorHost
    // every "*_Editor.dll" receives. Gated by GTE_ENABLE_PROJECT_ASSEMBLIES.
#if GTE_ENABLE_PROJECT_ASSEMBLIES
    m_core.LoadProjectAssemblies(ResolveProjectAssemblyOutputDirectory(gte::ExecutableDirectory()), this);
#endif

    // Created after every built-in panel name is registered, so plugin panels come after built-ins.
    // EditorHost never names a concrete feature module.
    m_builtinFeatureModules = BuiltinFeatureModuleRegistry::Instance().CreateAll(m_core);
    m_editorLayer->AttachBuiltinFeatureModules(m_builtinFeatureModules, m_renderer, m_renderGraph);

    // Scene IO for the engine command dispatch. One process-lifetime instance; the demo handles are resolved per call.
    static EditorSceneIOCapability s_editorSceneIOCapability;
    s_editorSceneIOCapability.SetHizDemoServicesProvider([this]() { return MakeHizDemoServices(); });
    m_sceneIOCapability = &s_editorSceneIOCapability;

#if GTE_ENABLE_NETWORK
    // Loopback only, port 8080. A bind failure (e.g. another instance running) is logged by the server and is non-fatal.
    m_networkServer.Start(8080);
#endif

    // Proves logging is reachable once every other subsystem is up.
    GTE_LOG_INFO("EditorHost",
        "EditorHost constructed: SdlContext -> Window -> Core -> CreateEditorLayer() -> "
        "Core::SetEditorLayerHook() all completed, with a genuinely non-null IEditorLayer*, "
        "every automation bridge attached, and NetworkServer started.");
}

void EditorHost::ServiceFeatureDebugCommand()
{
    const auto pending = m_featureDebugCommandBridge.TryPeekPendingCommand();
    if (!pending) {
        return;
    }
    const FeatureDebugCommandRequest* const request = &pending->request;
    GTE_PROFILE_SCOPE("EditorHost::ExecuteFeatureDebugCommand");

    FeatureDebugCommandResult result;
    for (const std::unique_ptr<IEngineFeatureModule>& module : m_builtinFeatureModules) {
        fx::Feature* feature = module ? fx::FindFeature(*module) : nullptr;
        fx::IFeatureDebugEndpoint* endpoint = feature ? feature->DebugEndpoint() : nullptr;
        if (endpoint == nullptr) {
            continue;
        }
        if (request->kind == FeatureDebugCommandKind::List) {
            result.featureNames.emplace_back(feature->Name());
            continue;
        }
        if (request->featureName != feature->Name()) {
            continue;
        }
        result.found = true;
        // An exception must not unwind through Run(); it becomes an error answer.
        try {
            switch (request->kind) {
            case FeatureDebugCommandKind::GetState:
                result.result = endpoint->GetState();
                break;
            case FeatureDebugCommandKind::Set:
                result.result = endpoint->Set(request->args);
                break;
            case FeatureDebugCommandKind::Invoke:
                result.result = endpoint->Invoke(request->action, request->args);
                break;
            case FeatureDebugCommandKind::List:
                break;
            }
        } catch (const std::exception& e) {
            result.result = fx::FeatureDebugResult{};
            result.result.ok = false;
            result.result.message = e.what();
        }
        break;
    }
    result.found = result.found || request->kind == FeatureDebugCommandKind::List;
    FulfillOrLogDrop(m_featureDebugCommandBridge, pending->id, std::move(result), "FeatureDebug");
}

HizDemoServices EditorHost::MakeHizDemoServices()
{
    HizDemoServices services;
    const HizDemoEditorHandles handles = m_editorLayer->HizDemoHandles();
    services.grid = handles.grid;
    services.state = handles.state;
    services.compositor = m_core.GetRenderFeatureCompositor();
    for (const std::unique_ptr<IEngineFeatureModule>& module : m_builtinFeatureModules) {
        fx::Feature* feature = module ? fx::FindFeature(*module) : nullptr;
        if (feature != nullptr && std::string_view(feature->Name()) == "GpuDrivenScene") {
            services.gpuDrivenScene = feature->DebugEndpoint();
            break;
        }
    }
    return services;
}

void EditorHost::ServiceHizDemoMenuAction()
{
    const HizDemoEditorHandles handles = m_editorLayer->HizDemoHandles();
    if (handles.pendingMenuAction == nullptr || *handles.pendingMenuAction == HizDemoMenuAction::None) {
        return;
    }
    const HizDemoMenuAction action = std::exchange(*handles.pendingMenuAction, HizDemoMenuAction::None);
    GTE_PROFILE_SCOPE("EditorHost::ExecuteHizDemoMenuAction");

    const HizDemoServices services = MakeHizDemoServices();
    std::string message;
    bool ok = true;
    switch (action) {
    case HizDemoMenuAction::Open: {
        const HizDemoReport report = OpenHizDemo(m_game, m_renderer,
            m_core.GetSceneServicesDescriptorSet().LayoutHandle(), PresetParameters{}, services);
        ok = report.success;
        message = report.message;
        break;
    }
    case HizDemoMenuAction::Close:
        ok = CloseHizDemo(m_game, services, message);
        break;
    case HizDemoMenuAction::SceneReplaced:
        if (ReleaseHizDemoSettings(services)) {
            return;
        }
        ok = false;
        message = "The Hi-Z demo settings were not fully restored (see the log).";
        break;
    case HizDemoMenuAction::None:
        return;
    }
    if (handles.menuResult != nullptr) {
        *handles.menuResult = HizDemoMenuResult{ true, !ok, std::move(message) };
    }
}

int EditorHost::Run()
{
    bool running = true;
    ExitGate exitGate;

    // One micro-benchmark on the main thread; the profiler shows it as an estimated cost per event.
    Profiling::FrameProfiler::Instance().SetEventCostNs(Profiling::MeasureScopeEventCostNs());

    // The memory summary copies every tracked allocation, so it is rebuilt every few frames only.
    constexpr std::uint32_t kGpuMemoryRefreshFrames = 30;
    rg::RenderGraphGpuMemoryMetadata gpuMemory;
    std::uint32_t gpuMemoryAgeFrames = kGpuMemoryRefreshFrames;
    // Identifies this host's window among the SDL windows ImGui multi-viewport may create; an SDL window ID never changes.
    const Uint32 mainWindowId = m_window.Id();
    Uint64 lastTicksNs = SDL_GetTicksNS();

    InputState inputState;

    while (running) {
        // Brackets the whole frame for the profiler; a no-op while profiling is disabled.
        Profiling::FrameProfiler::Instance().BeginFrame();

        // GPU timing follows the editor's "Capture" checkbox.
        m_renderGraph.SetGpuTimingCaptureEnabled(Profiling::FrameProfiler::Instance().IsCaptureEnabled());

        // Clear last frame's transient "just pressed/released" flags and
        // per-frame mouse/wheel deltas before this frame's events arrive.
        inputState.BeginFrame();

        {
            GTE_PROFILE_SCOPE("EditorHost::PollEvents");
            SDL_Event sdlEvent;
            while (SDL_PollEvent(&sdlEvent)) {
                // The editor sees every raw event first, directly (never through Core).
                m_editorLayer->ProcessEvent(sdlEvent);

                const std::optional<Event> event = EventTranslator::Translate(sdlEvent, mainWindowId);
                if (!event.has_value()) {
                    continue;
                }

                if (event->type == EventType::Quit) {
                    running = false;
                } else if (event->type == EventType::WindowResized) {
                    const auto& resized = std::get<WindowResizedEventData>(event->data);
                    m_renderer.OnResize(resized.width, resized.height);
                    // Keeps Core's direct-to-swapchain fallback aspect ratio correct across a live resize.
                    m_core.NotifyWindowResized(resized.width, resized.height);
                    // Keeps the Game-view RenderTexture tracking the window size (no-op in a release build).
                    m_editorLayer->OnWindowResized(resized.width, resized.height);
                }

                // Events the editor UI wants (cursor over a panel, focused text field) must not reach gameplay.
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

        // Drains at most one network engine command per frame, right after input and before Game::Update().
        if (const auto enginePending = m_commandBridge.TryPeekPendingCommand()) {
            GTE_PROFILE_SCOPE("EditorHost::ExecuteEngineCommand");
            const EngineCommandResult result = ExecuteEngineCommand(m_game, m_renderer, m_sceneIOCapability,
                &s_editorHotReloadDebugCapability, m_core.GetSceneServicesDescriptorSet().LayoutHandle(), enginePending->request);
            FulfillOrLogDrop(m_commandBridge, enginePending->id, result, "Engine");
        }

        ServiceHizDemoMenuAction();

        // Hot reload: at most once per frame, and it blocks this thread until done (a deliberate hard freeze).
        if (const std::optional<std::string> requestedProject = m_hotReloadCommandBridge.TryPeekPendingProjectName()) {
            GTE_PROFILE_SCOPE("EditorHost::PerformProjectAssemblyHotReload");
            // Paths are resolved here (editor tier) and passed by value: the Core-tier orchestrator must not call ExecutableDirectory().
            const std::filesystem::path outputDirectory = ResolveProjectAssemblyOutputDirectory(gte::ExecutableDirectory());
            const std::filesystem::path buildDirectory = ResolveCMakeBuildDirectory(gte::ExecutableDirectory());
            // Resolved here for the same reason: the Core-tier orchestrator must not resolve paths itself.
            const std::filesystem::path projectRootDirectory = ResolveProjectRootDirectory();
            PerformProjectAssemblyHotReload(*requestedProject, m_core, m_renderer, this, outputDirectory, buildDirectory, projectRootDirectory);
            m_hotReloadCommandBridge.FulfillPending();
        }

        const Uint64 nowTicksNs = SDL_GetTicksNS();
        const double deltaSeconds = static_cast<double>(nowTicksNs - lastTicksNs) / 1000000000.0;
        lastTicksNs = nowTicksNs;

        // Reads the toolbar Pause/Step state. The step request is consumed every frame so a stale one never lingers.
        const bool playbackPaused = m_editorLayer->IsPlaybackPaused();
        const bool stepRequestedRaw = m_editorLayer->TryConsumeStepRequest();
        const bool steppedThisFrame = playbackPaused && stepRequestedRaw;

        // Tells the Frame Debugger a Step happened this frame.
        if (steppedThisFrame) {
            m_editorLayer->NotifyFrameDebuggerStepConsumed();
        }

        // Advances time (honoring Pause/Step) and runs Game::Update().
        InputFrame inputFrame;
        inputFrame.inputState = &inputState;
        inputFrame.playbackPaused = playbackPaused;
        inputFrame.stepRequested = stepRequestedRaw;
        m_core.Update(inputFrame, static_cast<float>(deltaSeconds));

        // Stamps log entries until the next Advance() with this frame's number; lives here because Core must not include Logger.h.
        gte::Logger::SetCurrentFrame(m_engineContext.time.FrameCount());

        m_editorLayer->NewFrame();

        // Drains at most one pending editor UI command per frame, after NewFrame() and before BuildUI().
        if (const auto uiPending = m_uiCommandBridge.TryPeekPendingCommand()) {
            GTE_PROFILE_SCOPE("EditorHost::ExecuteEditorUiCommand");
            const EditorUiCommandResult uiResult = RunEditorUiCommand(uiPending->request, *m_editorLayer, m_game,
                m_renderer, m_core.GetSceneServicesDescriptorSet().LayoutHandle());
            FulfillOrLogDrop(m_uiCommandBridge, uiPending->id, uiResult, "EditorUi");
            if (uiResult.kind == EditorUiCommandKind::Exit) {
                exitGate.Arm(static_cast<std::uint64_t>(m_engineContext.time.FrameCount()), SDL_GetTicksNS());
            }
        }

        ServiceFeatureDebugCommand();

        // Drains at most one pending Frame Debugger command per frame, right after the UI command pump.
        if (const auto fdPending = m_frameDebuggerCommandBridge.TryPeekPendingCommand()) {
            const FrameDebuggerCommandRequest* const fdRequest = &fdPending->request;
            GTE_PROFILE_SCOPE("EditorHost::ExecuteFrameDebuggerCommand");
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
            case FrameDebuggerCommandKind::GetEventTexture: {
                const FrameDebuggerEventTextureResult eventTexture =
                    m_editorLayer->FrameDebuggerGetEventTexture(fdRequest->getEventTexture.index);
                fdResult.success = eventTexture.found;
                fdResult.eventTexture.found = eventTexture.found;
                fdResult.eventTexture.pixels = eventTexture.pixels;
                fdResult.eventTexture.width = eventTexture.width;
                fdResult.eventTexture.height = eventTexture.height;
                break;
            }
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
            fdResult.state.perDrawEventCount = stateView.perDrawEventCount;

            FulfillOrLogDrop(m_frameDebuggerCommandBridge, fdPending->id, fdResult, "FrameDebugger");
        }

        // Drains at most one pending render-graph-control command per frame.
        // Built-in-pass/plugin-feature mutations go straight to m_core.
        if (const auto rgcPending = m_renderGraphControlCommandBridge.TryPeekPendingCommand()) {
            const RenderGraphControlCommandRequest* const rgcRequest = &rgcPending->request;
            GTE_PROFILE_SCOPE("EditorHost::ExecuteRenderGraphControlCommand");
            RenderGraphControlCommandResult rgcResult;
            rgcResult.kind = rgcRequest->kind;
            switch (rgcRequest->kind) {
            case RenderGraphControlCommandKind::SetBuiltInPassEnabled: {
                const bool applied = m_core.SetBuiltInRenderPassEnabled(
                    rgcRequest->setPassEnabled.name, rgcRequest->setPassEnabled.enabled);
                rgcResult.success = applied;
                if (!applied) {
                    rgcResult.errorMessage = "\"" + rgcRequest->setPassEnabled.name + "\" cannot be disabled (deny-listed).";
                } else {
                    // Runs before BuildUI(), so tell the Frame Debugger directly (no-op unless it is enabled).
                    m_editorLayer->FrameDebuggerCaptureNow();
                }
                break;
            }
            case RenderGraphControlCommandKind::ListPassStates: {
                for (const rg::RenderPassToggleState& state : m_core.GetRenderPassToggleRegistryMutable().ListAll()) {
                    RenderGraphControlPassStateOutcome outcome;
                    outcome.name = state.name;
                    outcome.enabled = state.enabled;
                    outcome.everDeclaredThisSession = state.everDeclaredThisSession;
                    outcome.owningFeatureName = state.owningFeatureName;
                    rgcResult.passStates.push_back(std::move(outcome));
                }
                rgcResult.success = true;
                break;
            }
            case RenderGraphControlCommandKind::SetFeatureEnabled: {
                RenderFeatureCompositor* compositor = m_core.GetRenderFeatureCompositor();
                if (compositor == nullptr) {
                    rgcResult.success = false;
                    rgcResult.errorMessage = "no render feature compositor available this session";
                } else {
                    const RenderGraphControlSetFeatureEnabledCommand& toggle = rgcRequest->setFeatureEnabled;
                    if (toggle.view == RenderGraphControlViewFilter::Both) {
                        // Both scopes: the global override and both scope flags.
                        rgcResult.success = compositor->SetFeatureEnabled(toggle.name, toggle.enabled)
                            && compositor->SetFeatureViewEnabled(toggle.name, rg::ViewScope::Primary, toggle.enabled)
                            && compositor->SetFeatureViewEnabled(toggle.name, rg::ViewScope::Secondary, toggle.enabled);
                    } else {
                        const rg::ViewScope scope = toggle.view == RenderGraphControlViewFilter::Primary
                            ? rg::ViewScope::Primary
                            : rg::ViewScope::Secondary;
                        rgcResult.success = compositor->SetFeatureViewEnabled(toggle.name, scope, toggle.enabled);
                    }
                    if (!rgcResult.success) {
                        rgcResult.errorMessage =
                            "\"" + rgcRequest->setFeatureEnabled.name + "\" matches no loaded plugin render feature.";
                    }
                }
                break;
            }
            case RenderGraphControlCommandKind::SetFeaturePriority: {
                RenderFeatureCompositor* compositor = m_core.GetRenderFeatureCompositor();
                if (compositor == nullptr) {
                    rgcResult.success = false;
                    rgcResult.errorMessage = "no render feature compositor available this session";
                } else {
                    rgcResult.success = compositor->SetFeaturePriority(
                        rgcRequest->setFeaturePriority.name, rgcRequest->setFeaturePriority.priority);
                    if (!rgcResult.success) {
                        rgcResult.errorMessage =
                            "\"" + rgcRequest->setFeaturePriority.name + "\" matches no loaded plugin render feature.";
                    }
                }
                break;
            }
            case RenderGraphControlCommandKind::SetDisplayedRegime: {
                rgcResult.windowOpen =
                    m_editorLayer->SetRenderGraphDisplayedRegime(rgcRequest->setDisplayedRegime.present);
                rgcResult.success = true;
                break;
            }
            }
            FulfillOrLogDrop(m_renderGraphControlCommandBridge, rgcPending->id, rgcResult, "RenderGraphControl");
        }

        // Cluster builds: the one poll site. A finished build runs the follow-up once, then answers its caller.
        MeshClusterBuildRunner* const clusterRunner = m_editorLayer->ClusterBuildRunner();
        if (clusterRunner != nullptr) {
            if (const std::optional<MeshClusterFinishedBuild> finished = clusterRunner->Poll()) {
                RunClusterBuildFollowUp(*finished, m_game, *m_editorLayer);
                if (finished->requestId != 0) {
                    AssetImportCommandResult clusterResult;
                    if (finished->wasClone) {
                        clusterResult.kind = AssetImportCommandKind::CloneMeshClusters;
                        clusterResult.cloneMeshClusters = ToCloneBridgeOutcome(*finished);
                    } else {
                        clusterResult.kind = AssetImportCommandKind::BuildMeshClusters;
                        clusterResult.buildMeshClusters = ToBridgeOutcome(*finished);
                    }
                    FulfillOrLogDrop(m_assetImportCommandBridge, finished->requestId, clusterResult,
                        finished->wasClone ? "CloneMeshClusters" : "BuildMeshClusters");
                }
            }
        }

        // Drains at most one pending asset command (/import_asset or /build_clusters) per frame.
        if (const auto importPending = m_assetImportCommandBridge.TryPeekPendingCommand()) {
            GTE_PROFILE_SCOPE("EditorHost::ExecuteAssetImportCommand");
            const AssetImportCommandRequest& importRequest = importPending->request;
            AssetImportCommandResult importResult;
            importResult.kind = importRequest.kind;
            switch (importRequest.kind) {
            case AssetImportCommandKind::ImportExternalFile: {
                ImportExternalFileOutcome& outcome = importResult.importExternalFile;
                const ProjectAssetImportResult layerResult = m_editorLayer->ImportExternalAssetIntoProject(
                    importRequest.importExternalFile.sourceAbsolutePath,
                    importRequest.importExternalFile.destinationRelativeFolder);
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
                FulfillOrLogDrop(m_assetImportCommandBridge, importPending->id, importResult, "AssetImport");
                break;
            }
            case AssetImportCommandKind::BuildMeshClusters: {
                // The request stays pending while its own build runs; the poll above answers it.
                if (clusterRunner != nullptr && clusterRunner->ActiveRequestId() == importPending->id) {
                    break;
                }
                BuildMeshClustersOutcome& outcome = importResult.buildMeshClusters;
                std::filesystem::path gtaPath;
                if (clusterRunner == nullptr) {
                    outcome = MakeClusterRefusal(BuildMeshClustersStatus::Unavailable,
                        "cluster build is not available in this build");
                } else if (clusterRunner->Busy()) {
                    outcome = MakeClusterRefusal(BuildMeshClustersStatus::Busy, "another cluster build is already running");
                } else if (const std::optional<std::string> invalid = ValidateMeshAssetPath(
                               importRequest.buildMeshClusters.gtaPath, ResolveProjectRootDirectory(), gtaPath)) {
                    outcome = MakeClusterRefusal(BuildMeshClustersStatus::BadRequest, *invalid);
                } else {
                    const BuildMeshClustersCommand& command = importRequest.buildMeshClusters;
                    MeshClusterOptions options;
                    options.weld = command.weld == "position_only" ? ClusterWeldMode::PositionOnly : ClusterWeldMode::Exact;
                    options.maxVerticesPerCluster = command.maxVertices;
                    options.maxTrianglesPerCluster = command.maxTriangles;
                    options.coneWeight = command.coneWeight;
                    clusterRunner->Start(gtaPath, options, importPending->id);
                    break; // Answered by the poll when the build finishes.
                }
                FulfillOrLogDrop(m_assetImportCommandBridge, importPending->id, importResult, "BuildMeshClusters");
                break;
            }
            case AssetImportCommandKind::CloneMeshClusters: {
                // The request stays pending while its own clone runs; the poll above answers it.
                if (clusterRunner != nullptr && clusterRunner->ActiveRequestId() == importPending->id) {
                    break;
                }
                CloneMeshClustersOutcome& outcome = importResult.cloneMeshClusters;
                std::filesystem::path gtaPath;
                if (clusterRunner == nullptr) {
                    outcome = MakeCloneRefusal(BuildMeshClustersStatus::Unavailable,
                        "cluster clone is not available in this build");
                } else if (clusterRunner->Busy()) {
                    outcome = MakeCloneRefusal(BuildMeshClustersStatus::Busy, "another cluster build is already running");
                } else if (const std::optional<std::string> invalid = ValidateMeshAssetPath(
                               importRequest.cloneMeshClusters.gtaPath, ResolveProjectRootDirectory(), gtaPath)) {
                    outcome = MakeCloneRefusal(BuildMeshClustersStatus::BadRequest, *invalid);
                } else if (const std::filesystem::path destination = ChooseClusteredCloneDestination(gtaPath);
                           destination.empty()) {
                    outcome = MakeCloneRefusal(BuildMeshClustersStatus::BadRequest, "no free name for the clustered copy");
                } else {
                    const CloneMeshClustersCommand& command = importRequest.cloneMeshClusters;
                    MeshClusterCloneOptions options;
                    options.autoWeld = command.weld == "auto";
                    options.cluster.weld = command.weld == "position_only" ? ClusterWeldMode::PositionOnly : ClusterWeldMode::Exact;
                    options.cluster.maxVerticesPerCluster = command.maxVertices;
                    options.cluster.maxTrianglesPerCluster = command.maxTriangles;
                    options.cluster.coneWeight = command.coneWeight;
                    clusterRunner->StartClone(gtaPath, destination, options, command.openViewer, importPending->id);
                    break; // Answered by the poll when the clone finishes.
                }
                FulfillOrLogDrop(m_assetImportCommandBridge, importPending->id, importResult, "CloneMeshClusters");
                break;
            }
            }
        }

        // Drains at most one pending "Open Project" load request per frame (submitted from the network thread).
        if (const auto loadPending = m_projectLifecycleLoadCommandBridge.TryPeekPendingCommand()) {
            GTE_PROFILE_SCOPE("EditorHost::ExecuteProjectLifecycleLoad");
            const std::filesystem::path outputDirectory = ResolveProjectAssemblyOutputDirectory(gte::ExecutableDirectory());
            const std::string& name = loadPending->request.projectName;
            LoadProjectAssemblyCommandResult loadResult;
            loadResult.loadSucceeded =
                m_core.GetProjectAssemblyHost().LoadOneProjectAssemblyFromExactPath(
                    outputDirectory / (name + "_Game.dll"), m_core, this)
                && m_core.GetProjectAssemblyHost().LoadOneProjectAssemblyFromExactPathIfExists(
                    outputDirectory / (name + "_Editor.dll"), m_core, this);
            FulfillOrLogDrop(m_projectLifecycleLoadCommandBridge, loadPending->id, loadResult, "ProjectLifecycleLoad");
        }

        // Clears last frame's queued Submit() draws before Game queues this frame's.
        m_renderer.BeginFrame();

        // Builds the frame's render graph.
        m_core.BuildFrame();

        // Game View capture: host-level automation, safe after BuildFrame() because it already fence-waited.
        RenderTexture* gameTargetThisFrame = m_core.GetPrimaryViewTargetThisFrame();
        if (gameTargetThisFrame == nullptr && m_captureBridge.IsCaptureRequested(FrameCaptureKind::GameView)) {
            m_captureBridge.FailPendingRequest(FrameCaptureKind::GameView, FrameCaptureFailureReason::TargetNotAvailable);
        }
        if (gameTargetThisFrame != nullptr && m_captureBridge.IsCaptureRequested(FrameCaptureKind::GameView)) {
            GTE_PROFILE_MARKER("Capture.GameView");
            // The Game View target holds the final display image after the PostComposite chain.
            const CaptureResult captured = m_renderer.CaptureRenderTexturePixels(*gameTargetThisFrame);
            const CapturedPixels& raw = captured.pixels;
            const std::vector<std::uint8_t> rgba8 = captured.status == CaptureStatus::Ok
                ? Encoding::ConvertCapturedPixelsToRgba8(raw, Encoding::HdrVisualization::DisplayReferred)
                : std::vector<std::uint8_t>{};
            if (rgba8.empty()) {
                m_captureBridge.FailPendingRequest(
                    FrameCaptureKind::GameView, FailureReasonForCapture(captured.status));
            } else {
                std::vector<std::uint8_t> png = Encoding::EncodeRgba8ToPng(rgba8.data(), raw.width, raw.height);
                m_captureBridge.FulfillPendingRequest(FrameCaptureKind::GameView,
                    CapturedPngImage{ std::move(png), raw.width, raw.height });
            }
        }

        // Builds every editor panel now that the view textures hold this frame's contents.
        {
            GTE_PROFILE_SCOPE("IEditorLayer::BuildUI");
            // The compositor may be null; the debug snapshot is computed here and passed to BuildUI().
            const RenderFeatureCompositor* renderFeatureCompositor = m_core.GetRenderFeatureCompositor();
            const std::vector<RenderFeatureDebugEntry> renderFeatureEntries =
                renderFeatureCompositor != nullptr ? renderFeatureCompositor->DebugSnapshot()
                                                    : std::vector<RenderFeatureDebugEntry>{};
            m_editorLayer->BuildUI(m_game, m_renderer, m_renderGraph, renderFeatureEntries,
                // The mutable compositor is fetched again: the const local above does not fit BuildUI()'s parameter.
                m_core.GetRenderPassToggleRegistryMutable(), m_core.GetRenderFeatureCompositor(),
                m_core.GetOffscreenBlackboardForFrameDebugger(),
                // Layout threaded into every editor spawn path reached from BuildUI().
                m_core.GetSceneServicesDescriptorSet().LayoutHandle());

            // GET /render_graph data: reuses renderFeatureEntries and ignores the panel's Pause checkbox (always the latest frame).
            rg::RenderGraphMetadata renderGraphMetadata = rg::BuildRenderGraphMetadata(
                m_renderGraph.LastSnapshot(rg::ExecuteTimingMode::SynchronousImmediateReadback),
                m_renderGraph.LastSnapshot(rg::ExecuteTimingMode::PipelinedDeferredReadback), renderFeatureEntries);
            renderGraphMetadata.indirectDraw = m_renderer.IndirectDrawCaps();
            renderGraphMetadata.gpuWait.deviceLost = m_renderer.DeviceLost();
            renderGraphMetadata.gpuWait.consecutiveFrameTimeouts = m_renderer.GpuWaitConsecutiveFrameTimeouts();
            renderGraphMetadata.gpuWait.abandonedObjects = m_renderer.AbandonedObjectCount();
            if (gpuMemoryAgeFrames >= kGpuMemoryRefreshFrames) {
                GTE_PROFILE_MARKER("Periodic.GpuMemorySummary");
                gpuMemory = rg::BuildGpuMemoryMetadata(
                    CollectGpuAllocationSamples(m_renderer), m_game.GetRenderSystem().MeshCount());
                gpuMemoryAgeFrames = 0;
            }
            ++gpuMemoryAgeFrames;
            renderGraphMetadata.gpuMemory = gpuMemory;
            m_captureBridge.PublishRenderGraphMetadata(std::move(renderGraphMetadata));
        }

        // File > Exit and other programmatic close actions end the loop like a Quit event.
        if (m_editorLayer->WantsExit()) {
            running = false;
        }

        // An exit command ends the loop once its reply has had time to leave the process.
        if (exitGate.Due(static_cast<std::uint64_t>(m_engineContext.time.FrameCount()), SDL_GetTicksNS())) {
            running = false;
        }

        // Must run before Present() so the swapchain service sees the capture request this frame.
        if (m_captureBridge.IsCaptureRequested(FrameCaptureKind::Swapchain)) {
            GTE_PROFILE_MARKER("Capture.Swapchain");
            m_renderer.RequestSwapchainCapture();
        }

        // Pipelined swapchain present.
        m_core.Present();

        // Swapchain capture success path; safe here because Present() already fence-waited.
        if (std::optional<CapturedSwapchainPixels> raw = m_renderer.TakeLastCompletedSwapchainCapture()) {
            if (Encoding::IsBgraFormat(raw->format)) {
                Encoding::ConvertBgraToRgbaInPlace(raw->pixels.data(), raw->width, raw->height);
            }
            std::vector<std::uint8_t> png = Encoding::EncodeRgba8ToPng(raw->pixels.data(), raw->width, raw->height);
            m_captureBridge.FulfillPendingRequest(FrameCaptureKind::Swapchain,
                CapturedPngImage{ std::move(png), raw->width, raw->height });
        }

        // Presents panels dragged outside the main OS window (no-op in a release build).
        m_editorLayer->RenderPlatformWindows();

        // GET /get_window: floating (undocked) windows only. Must follow RenderPlatformWindows(), which presents them.
        if (m_captureBridge.IsCaptureRequested(FrameCaptureKind::NamedWindow)) {
            GTE_PROFILE_MARKER("Capture.NamedWindow");
            CaptureResult captured = m_editorLayer->CapturePlatformWindow(m_renderer, m_captureBridge.RequestedWindowTitle());
            CapturedPixels& raw = captured.pixels;
            if (captured.status != CaptureStatus::Ok || raw.pixels.empty()) {
                // Wrong title or still docked: fail fast; the caller should use /get_swapchain.
                m_captureBridge.FailPendingRequest(
                    FrameCaptureKind::NamedWindow, FailureReasonForCapture(captured.status));
            } else {
                if (Encoding::IsBgraFormat(raw.format)) {
                    Encoding::ConvertBgraToRgbaInPlace(raw.pixels.data(), raw.width, raw.height);
                }
                std::vector<std::uint8_t> png = Encoding::EncodeRgba8ToPng(raw.pixels.data(), raw.width, raw.height);
                m_captureBridge.FulfillPendingRequest(
                    FrameCaptureKind::NamedWindow, CapturedPngImage{ std::move(png), raw.width, raw.height });
            }
        }

        // GET /get_texture capture: runs after every graph Execute() so the texture snapshot is the freshest.
        if (m_captureBridge.IsCaptureRequested(FrameCaptureKind::NamedTexture)) {
            GTE_PROFILE_MARKER("Capture.NamedTexture");
            const std::string requestedName = m_captureBridge.RequestedTextureName();
            const DebugTextureChannel requestedChannel = m_captureBridge.RequestedTextureChannel();

            if (const std::optional<rg::DebugTextureSnapshot> snapshot = m_renderGraph.DebugTextureSnapshotFor(requestedName)) {
                const bool wantsDepth = (requestedChannel == DebugTextureChannel::Depth);
                if (wantsDepth && !snapshot->hasDepth) {
                    // Known permanent failure: fail fast (409) instead of waiting out the timeout.
                    m_captureBridge.FailPendingRequest(FrameCaptureKind::NamedTexture, FrameCaptureFailureReason::TargetNotAvailable);
                } else if (!wantsDepth && !snapshot->HasColorImage()) {
                    // A colorless (depth-only) texture never has a color image: copying it would crash, so fail fast.
                    m_captureBridge.FailPendingRequest(FrameCaptureKind::NamedTexture, FrameCaptureFailureReason::TargetNotAvailable);
                } else {
                    // Measured before the capture, which waits for the GPU.
                    const std::uint64_t framesSinceUpdate =
                        m_renderGraph.CurrentDebugTextureFrameCounter() - snapshot->lastUpdatedFrameCounter;

                    CaptureResult captured = m_renderer.CaptureDebugTexturePixels(*snapshot, wantsDepth);
                    CapturedPixels& raw = captured.pixels;

                    if (captured.status != CaptureStatus::Ok) {
                        // Busy or lost GPU reports its real cause; an unreadable format is "not available".
                        m_captureBridge.FailPendingRequest(
                            FrameCaptureKind::NamedTexture, FailureReasonForCapture(captured.status));
                    } else {
                        std::vector<std::uint8_t> converted;
                        const std::uint8_t* encodePixels = raw.pixels.data();
                        bool ok = true;
                        if (wantsDepth) {
                            // In-place is safe: each pixel's input bytes are read before its output bytes are written.
                            ok = Encoding::ConvertDepthToGrayscaleRgba8(raw.pixels.data(), raw.format, raw.width, raw.height, raw.pixels.data());
                        } else if (raw.format == PixelFormat::RGBA16_Float) {
                            converted.resize(static_cast<std::size_t>(raw.width) * static_cast<std::size_t>(raw.height) * 4);
                            ok = Encoding::ConvertRgba16fToRgba8(raw.pixels.data(), raw.format, raw.width, raw.height,
                                converted.data(), Encoding::HdrVisualizationForTextureName(requestedName));
                            encodePixels = converted.data();
                        } else if (raw.format == PixelFormat::R8_UNorm || raw.format == PixelFormat::R32_Float) {
                            converted.resize(static_cast<std::size_t>(raw.width) * static_cast<std::size_t>(raw.height) * 4);
                            ok = Encoding::Convert1ChannelToGrayscaleRgba8(raw.pixels.data(), raw.format, raw.width, raw.height, converted.data());
                            encodePixels = converted.data();
                        } else if (Encoding::IsBgraFormat(raw.format)) {
                            Encoding::ConvertBgraToRgbaInPlace(raw.pixels.data(), raw.width, raw.height);
                        }
                        // else: already tightly packed RGBA8.

                        if (!ok) {
                            // The device's depth or color format is not one the converters recognize.
                            m_captureBridge.FailPendingRequest(FrameCaptureKind::NamedTexture, FrameCaptureFailureReason::TargetNotAvailable);
                        } else {
                            std::vector<std::uint8_t> png = Encoding::EncodeRgba8ToPng(encodePixels, raw.width, raw.height);
                            m_captureBridge.FulfillPendingRequest(FrameCaptureKind::NamedTexture,
                                CapturedPngImage{ std::move(png), raw.width, raw.height, framesSinceUpdate });
                        }
                    }
                }
            } else if (const std::optional<rg::DebugVolumeTextureSnapshot> volumeSnapshot =
                           m_renderGraph.DebugVolumeTextureSnapshotFor(requestedName)) {
                // A registered volume texture renders a fresh raymarch thumbnail on demand.
                if (requestedChannel == DebugTextureChannel::Depth) {
                    // A volume texture has no depth companion: fail fast (409) like the 2D branch.
                    m_captureBridge.FailPendingRequest(FrameCaptureKind::NamedTexture, FrameCaptureFailureReason::TargetNotAvailable);
                } else {
                    // Measured from the snapshot as it was when the request was serviced.
                    const std::uint64_t framesSinceUpdate =
                        m_renderGraph.CurrentDebugTextureFrameCounter() - volumeSnapshot->lastUpdatedFrameCounter;

                    // The preview interpretation is auto-detected from the texture name.
                    const VolumeTexturePreviewInterpretation interpretation =
                        SelectVolumeTexturePreviewInterpretation(requestedName);

                    const CaptureResult preview = m_volumeTexturePreviewRenderer.RenderPreview(
                        m_renderer, volumeSnapshot->target, volumeSnapshot->state, interpretation);

                    if (preview.status != CaptureStatus::Ok) {
                        m_captureBridge.FailPendingRequest(
                            FrameCaptureKind::NamedTexture, FailureReasonForCapture(preview.status));
                    } else {
                        // The preview pixels are already tightly packed RGBA8: no conversion needed.
                        const CapturedPixels& raw = preview.pixels;
                        std::vector<std::uint8_t> png = Encoding::EncodeRgba8ToPng(raw.pixels.data(), raw.width, raw.height);
                        m_captureBridge.FulfillPendingRequest(FrameCaptureKind::NamedTexture,
                            CapturedPngImage{ std::move(png), raw.width, raw.height, framesSinceUpdate });
                    }
                }
            }
            // else: name not registered yet; leave the request pending until it appears or the bridge times out (504).
        }

        // Data source for GET /list_textures; resolved to plain values here, never inside FrameCaptureBridge.
        {
            const std::vector<rg::DebugTextureSnapshot> snapshots = m_renderGraph.ListDebugTextures();
            const std::uint64_t currentFrameCounter = m_renderGraph.CurrentDebugTextureFrameCounter();

            std::vector<PublishedTextureListEntry> published;
            published.reserve(snapshots.size());
            for (const rg::DebugTextureSnapshot& snap : snapshots) {
                PublishedTextureListEntry entry;
                entry.name = snap.name;
                entry.regime = ToDebugTextureRegimeString(snap.regime);
                entry.format = snap.ColorFormatName();
                entry.width = snap.target.extent.width;
                entry.height = snap.target.extent.height;
                entry.hasDepth = snap.hasDepth;
                entry.framesSinceUpdate = currentFrameCounter - snap.lastUpdatedFrameCounter;
                entry.kind = "texture2d";
                published.push_back(std::move(entry));
            }

            // Registered volume textures are listed after the 2D ones.
            for (const rg::DebugVolumeTextureSnapshot& vol : m_renderGraph.ListDebugVolumeTextures()) {
                PublishedTextureListEntry entry;
                entry.name = vol.name;
                entry.regime = ToDebugTextureRegimeString(vol.regime);
                entry.format = vol.ColorFormatName();
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

        // One GPU memory snapshot per profiler frame, taken late so it sees every resource created this frame.
        Profiling::FrameProfiler::Instance().SetMemorySnapshot(BuildMemorySnapshot(m_renderer.GetMemoryTotals()));

        Profiling::FrameProfiler::Instance().EndFrame();

        // Built on demand, when a spike froze, and every few frames so a cold reader still finds one.
        Profiling::FrameProfiler& profiler = Profiling::FrameProfiler::Instance();
        if (m_profilerBridge.ShouldPublish(profiler.CompletedFrameCount(), profiler.Spikes().TotalFrozen())) {
            const ClockCalibration clock = m_renderer.GpuClockCalibration();
            Profiling::SnapshotInputs profilerInputs;
            profilerInputs.deviceLost = m_renderer.DeviceLost();
            profilerInputs.presentUncapped = m_renderer.GetPresentPolicy() == PresentPolicy::Uncapped;
            profilerInputs.consecutiveFrameTimeouts = m_renderer.GpuWaitConsecutiveFrameTimeouts();
            profilerInputs.abandonedObjects = m_renderer.AbandonedObjectCount();
            profilerInputs.gpuClockAligned = clock.state == ClockCalibrationState::Present;
            profilerInputs.timelineReason = ToString(clock.reason);
            // The frame detail goes first so a reader woken by the snapshot already finds it.
            if (Profiling::FrameProfiler::Instance().HistoryCount() > 0) {
                m_profilerBridge.PublishFrameDetail(
                    Profiling::BuildFrameDetail(Profiling::FrameProfiler::Instance().LastCompletedFrame()));
            }
            m_profilerBridge.Publish(Profiling::BuildProfilerSnapshot(Profiling::FrameProfiler::Instance(), profilerInputs));
        }
    }

    return 0;
}

} // namespace gte
