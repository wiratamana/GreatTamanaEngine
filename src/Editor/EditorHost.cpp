#include "EditorHost.h"

#include "Logger.h" // LoggerLogSink::Instance().
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
#include "VolumeTexturePreviewSelection.h"
#include "../Core/EditorPanelRegistry.h"
// RenderFeatureCompositor::DebugSnapshot() needs the full type (Core.h only forward-declares it).
#include "../Core/Plugins/RenderFeatureCompositor.h"
#include "../Application/EventTranslator.h"
#include "../Application/EngineCommandDispatch.h"
#include "../Application/MemorySnapshotBuilder.h"
#include "../Encoding/CapturedPixelConversion.h" // gte::Encoding::IsBgraFormat() - the one canonical copy.
#include "../Encoding/DepthVisualization.h"
#include "../Encoding/HdrColorVisualization.h"
#include "../Encoding/PixelConversion.h"
#include "../Encoding/PngEncoder.h"
#include "../Encoding/SingleChannelVisualization.h"
#include "../Memory/SdlMemoryTracker.h"
#include "../Profiling/FrameProfiler.h"
#include "../Profiling/ScopeTimer.h"
#include "../Renderer/RenderGraph/RenderGraphBuilder.h"
#include "../Renderer/RenderGraph/RenderGraphDebugTextureRegistry.h"
// BuildRenderGraphMetadata() for GET /render_graph.
#include "../Renderer/RenderGraph/RenderGraphMetadata.h"

#include <SDL3/SDL.h>

#include <cassert>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <stdexcept>

namespace gte {

namespace {

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

EditorHost::EditorHost(const std::string& title, int width, int height)
    : m_sdlContext()
    , m_window(title, width, height)
    // m_core is built from m_window (ISurfaceProvider) and m_hostServices (IHostServices); both exist by now.
    , m_hostServices()
    , m_core(m_window, m_hostServices)
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
          &m_featureDebugCommandBridge)
{
    // Installs the real log sink first: everything below, including the network server start, may log.
    gte::InstallLogSink(&gte::LoggerLogSink::Instance());

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

    // Scene IO for the engine command dispatch. Stateless delegation, so one process-lifetime instance is enough.
    static EditorSceneIOCapability s_editorSceneIOCapability;
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

int EditorHost::Run()
{
    bool running = true;
    // Identifies this host's window among the SDL windows ImGui multi-viewport may create; an SDL window ID never changes.
    const Uint32 mainWindowId = m_window.Id();
    Uint64 lastTicksNs = SDL_GetTicksNS();

    InputState inputState;

    while (running) {
        // Brackets the whole frame for the profiler; a no-op while profiling is disabled.
        Profiling::FrameProfiler::Instance().BeginFrame();

        // GPU timing follows the editor's "Capture" checkbox, for the renderer and for the graph's own timestamp pool.
        m_renderer.SetGpuTimingCaptureEnabled(Profiling::FrameProfiler::Instance().IsCaptureEnabled());
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

        // Drains at most one pending GET /activate_tab request per frame, after NewFrame() and before BuildUI().
        if (const auto uiPending = m_uiCommandBridge.TryPeekPendingCommand()) {
            const EditorUiCommandRequest* const uiRequest = &uiPending->request;
            GTE_PROFILE_SCOPE("EditorHost::ExecuteEditorUiCommand");
            EditorUiCommandResult uiResult;
            uiResult.kind = uiRequest->kind;
            // Two command kinds: spawn a GPU-driven test batch, or activate a tab.
            if (uiRequest->kind == EditorUiCommandKind::SpawnGpuDrivenTestBatch) {
                const GpuDrivenTestBatchSpawnResult spawned = m_editorLayer->SpawnGpuDrivenTestBatch(m_game, m_renderer,
                    uiRequest->spawnGpuDrivenTestBatch.instanceCount, m_core.GetSceneServicesDescriptorSet().LayoutHandle());
                uiResult.spawnGpuDrivenTestBatch.success = spawned.success;
                uiResult.spawnGpuDrivenTestBatch.editorAvailable = spawned.editorAvailable;
                uiResult.spawnGpuDrivenTestBatch.errorMessage = spawned.errorMessage;
                uiResult.spawnGpuDrivenTestBatch.instanceCount = spawned.instanceCount;
            } else {
                const TabActivationResult activation = m_editorLayer->ActivateTab(uiRequest->activateTab.tabName);
                uiResult.activateTab.tabExists = activation.tabExists;
                uiResult.activateTab.success = activation.tabExists;
            }
            FulfillOrLogDrop(m_uiCommandBridge, uiPending->id, uiResult, "EditorUi");
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
                    rgcResult.success = compositor->SetFeatureEnabled(
                        rgcRequest->setFeatureEnabled.name, rgcRequest->setFeatureEnabled.enabled);
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

        // Drains at most one pending /import_asset request per frame.
        if (const auto importPending = m_assetImportCommandBridge.TryPeekPendingCommand()) {
            GTE_PROFILE_SCOPE("EditorHost::ExecuteAssetImportCommand");
            const AssetImportCommandRequest& importRequest = importPending->request;
            AssetImportCommandResult importResult;
            importResult.kind = importRequest.kind;
            const ProjectAssetImportResult layerResult = m_editorLayer->ImportExternalAssetIntoProject(
                importRequest.importExternalFile.sourceAbsolutePath,
                importRequest.importExternalFile.destinationRelativeFolder);
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
            FulfillOrLogDrop(m_assetImportCommandBridge, importPending->id, importResult, "AssetImport");
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
        RenderTexture* gameTargetThisFrame = m_core.GetGameViewTargetThisFrame();
        if (gameTargetThisFrame == nullptr && m_captureBridge.IsCaptureRequested(FrameCaptureKind::GameView)) {
            m_captureBridge.FailPendingRequest(FrameCaptureKind::GameView, FrameCaptureFailureReason::TargetNotAvailable);
        }
        if (gameTargetThisFrame != nullptr && m_captureBridge.IsCaptureRequested(FrameCaptureKind::GameView)) {
            // The Game View target holds the final display image after the PostComposite chain.
            const Renderer::CapturedRawPixels raw = m_renderer.CaptureRenderTexturePixels(*gameTargetThisFrame);
            const std::vector<std::uint8_t> rgba8 =
                Encoding::ConvertCapturedPixelsToRgba8(raw, Encoding::HdrVisualization::DisplayReferred);
            if (rgba8.empty()) {
                m_captureBridge.FailPendingRequest(FrameCaptureKind::GameView, FrameCaptureFailureReason::TargetNotAvailable);
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
            m_editorLayer->BuildUI(m_game, m_renderer, m_renderGraph,
                m_core.GetGpuDrivenBatchDebugInfo(), renderFeatureEntries,
                // The mutable compositor is fetched again: the const local above does not fit BuildUI()'s parameter.
                m_core.GetRenderPassToggleRegistryMutable(), m_core.GetRenderFeatureCompositor(),
                m_core.GetOffscreenBlackboardForFrameDebugger(),
                // Layout threaded into every editor spawn path reached from BuildUI().
                m_core.GetSceneServicesDescriptorSet().LayoutHandle());

            // GET /render_graph data: reuses renderFeatureEntries and ignores the panel's Pause checkbox (always the latest frame).
            m_captureBridge.PublishRenderGraphMetadata(rg::BuildRenderGraphMetadata(
                m_renderGraph.LastSnapshot(rg::ExecuteTimingMode::SynchronousImmediateReadback),
                m_renderGraph.LastSnapshot(rg::ExecuteTimingMode::PipelinedDeferredReadback),
                m_core.GetGpuDrivenBatchDebugInfo(), renderFeatureEntries));
        }

        // File > Exit and other programmatic close actions end the loop like a Quit event.
        if (m_editorLayer->WantsExit()) {
            running = false;
        }

        // Must run before Present() so the swapchain service sees the capture request this frame.
        if (m_captureBridge.IsCaptureRequested(FrameCaptureKind::Swapchain)) {
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
            CapturedPixels raw = m_editorLayer->CapturePlatformWindow(m_renderer, m_captureBridge.RequestedWindowTitle());
            if (raw.pixels.empty()) {
                // Wrong title or still docked: fail fast; the caller should use /get_swapchain.
                m_captureBridge.FailPendingRequest(FrameCaptureKind::NamedWindow, FrameCaptureFailureReason::TargetNotAvailable);
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

                    Renderer::CapturedRawPixels raw = m_renderer.CaptureDebugTexturePixels(*snapshot, wantsDepth);

                    if (raw.pixels.empty()) {
                        // Unrecognized format: fail cleanly instead of encoding garbage.
                        m_captureBridge.FailPendingRequest(FrameCaptureKind::NamedTexture, FrameCaptureFailureReason::TargetNotAvailable);
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
                        } else if (raw.format == PixelFormat::R8_UNorm) {
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

                    const VolumeTexturePreviewRenderer::CapturedRawPixels raw = m_volumeTexturePreviewRenderer.RenderPreview(
                        m_renderer, volumeSnapshot->target, volumeSnapshot->state, interpretation);

                    // raw.pixels is already tightly packed RGBA8: no conversion needed.
                    std::vector<std::uint8_t> png = Encoding::EncodeRgba8ToPng(raw.pixels.data(), raw.width, raw.height);
                    m_captureBridge.FulfillPendingRequest(FrameCaptureKind::NamedTexture,
                        CapturedPngImage{ std::move(png), raw.width, raw.height, framesSinceUpdate });
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
    }

    return 0;
}

} // namespace gte
