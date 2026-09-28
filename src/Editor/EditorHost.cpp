#include "EditorHost.h"

#include "Logger.h" // LoggerLogSink::Instance() - the ONE real ILogSink this engine ships.
#include "EditorSceneIOCapability.h"
#include "EditorLogQueryCapability.h" // editor-core-separation-2 campaign, PHASE3.
#include "EditorHotReloadDebugCapability.h" // editor-core-separation-12 campaign, PHASE3.
// editor-core-separation-16 campaign (On-Engine Project Workflow plan,
// BIG-STEP 2), PHASE3 - EditorProjectLifecycleCapability/ActiveProjectAssemblyState.
#include "EditorProjectLifecycleCapability.h"
#include "ActiveProjectAssemblyState.h"
// editor-core-separation-13 campaign (Project Assembly Hot Reload plan,
// BIG-STEP 2), PHASE4 - ResolveProjectAssemblyOutputDirectory(), the shared
// gte_core-tier helper this file's own LoadProjectAssemblies() call site
// below now uses instead of an inline "/ \"project_assemblies\"" literal.
#include "../Core/Plugins/ProjectAssemblyBuildRunner.h"
// editor-core-separation-14 campaign (Project Assembly Hot Reload plan,
// BIG-STEP 3), PHASE3 - PerformProjectAssemblyHotReload(), called from this
// file's own new Run() drain point below.
#include "../Core/Plugins/ProjectAssemblyHotReload.h"
#include "ProjectRootPath.h" // editor-core-separation-3 campaign, PHASE2 - ExecutableDirectory().
#include "../Core/EditorPanelRegistry.h" // editor-core-separation-3 campaign, PHASE4.
// editor-core-separation-6 campaign, PHASE7
// (PHASE7_RENDER_GRAPH_PANEL_VISIBILITY.md) - needed for the real
// RenderFeatureCompositor::DebugSnapshot() call below (Core.h itself only
// forward-declares the type for its own GetRenderFeatureCompositor()
// accessor).
#include "../Core/Plugins/RenderFeatureCompositor.h"
#include "../Application/EventTranslator.h"
#include "../Application/EngineCommandDispatch.h"
#include "../Application/MemorySnapshotBuilder.h"
#include "../Encoding/DepthVisualization.h"
#include "../Encoding/HdrColorVisualization.h"
#include "../Encoding/PixelConversion.h"
#include "../Encoding/PngEncoder.h"
#include "../Memory/SdlMemoryTracker.h"
#include "../Profiling/FrameProfiler.h"
#include "../Profiling/ScopeTimer.h"
#include "../Renderer/RenderGraph/RenderGraphBuilder.h"
#include "../Renderer/RenderGraph/RenderGraphDebugTextureRegistry.h"
// editor-core-separation-7 campaign, PHASE4 - GET /render_graph support
// (BuildRenderGraphMetadata(), called below inside the existing
// "IEditorLayer::BuildUI" scoped block).
#include "../Renderer/RenderGraph/RenderGraphMetadata.h"

#include <SDL3/SDL.h>

#include <cassert>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <stdexcept>

namespace gte {

namespace {

// editor-core-separation-1 campaign, PHASE16 - relocated verbatim from
// Application.cpp (network-impl-2 campaign, Phase 3) - true for the two
// BGRA channel-order formats VulkanSwapchain.cpp's ChooseSurfaceFormat() is
// actually known to negotiate (it prefers VK_FORMAT_B8G8R8A8_UNORM but
// falls back to formats.front(), i.e. whatever the platform/driver reports
// first, if that exact combination isn't available). An unrecognized
// BGRA-like variant this two-value check doesn't catch would silently
// produce a channel-swapped (red/blue reversed) PNG with no error at all -
// an accepted, narrow risk (see network-impl-2's own Non-Goals) - if a
// future "screenshot has wrong colors" report ever shows up, start here.
bool IsBgraFormat(VkFormat format) noexcept
{
    return format == VK_FORMAT_B8G8R8A8_UNORM || format == VK_FORMAT_B8G8R8A8_SRGB;
}

// network-impl-4 campaign, Phase 5 - GET /list_textures' own small,
// human-readable resolution helpers, relocated verbatim from
// Application.cpp. Both are deliberately narrow (only the enumerators/
// formats this engine's render graph can actually produce today), mirroring
// IsBgraFormat()'s own "accepted narrow risk, documented" precedent
// immediately above - an unrecognized regime can never actually occur
// (ExecuteTimingMode has exactly two enumerators, both handled), and an
// unrecognized VkFormat falls back to a safe, clearly-labeled numeric string
// rather than a crash or a silently-wrong label, mirroring
// src/Editor/MemoryPanelData.cpp's own ToString(VkFormat) fallback
// convention.
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

// editor-core-separation-2 campaign, PHASE3
// (PHASE3_LOG_QUERY_CAPABILITY_AND_NETWORKROUTES_CLEANUP.md) - the ONE real
// EditorLogQueryCapability instance this engine ships, wired into
// NetworkServer's constructor below. Pure delegation with no state of its
// own (mirrors s_editorSceneIOCapability's own identical reasoning) - a
// single whole-process-lifetime instance is all this needs. Deliberately a
// NAMESPACE-scope static (not a function-local static declared inside the
// constructor BODY like s_editorSceneIOCapability below) - its ADDRESS is
// needed inside EditorHost's own member-INITIALIZER LIST (to construct
// m_networkServer, since ILogQueryCapability's own wiring destination is
// NetworkServer's constructor argument list directly, unlike
// ISceneIOCapability's own EditorHost-owned-member-plus-manual-threading
// shape - see this phase's own strategy doc, Step 3.5), which runs BEFORE
// the constructor body - a static declared inside the body would not yet
// be in scope at that point. Safe as a plain namespace-scope static despite
// running before main(): this class holds zero data members and its
// constructor touches no other global, so there is no static-initialization-
// order risk to guard against.
EditorLogQueryCapability s_editorLogQueryCapability;

// editor-core-separation-12 campaign (Project Assembly Hot Reload plan,
// BIG-STEP 1) - the ONE real EditorHotReloadDebugCapability instance this
// engine ships, wired into NetworkServer's constructor below AND used
// directly by the ExecuteEngineCommand() call site (Run()'s own
// EngineCommandBridge servicing code) for GetSceneSnapshot. Same
// namespace-scope-static reasoning as s_editorLogQueryCapability
// immediately above.
EditorHotReloadDebugCapability s_editorHotReloadDebugCapability;

// editor-core-separation-16 campaign (On-Engine Project Workflow plan,
// BIG-STEP 2), PHASE3 - the ONE real EditorProjectLifecycleCapability
// instance this engine ships. Wiring into NetworkServer's constructor and
// into m_editorLayer (for the "New Project..." ImGui window) is PHASE4's
// job - this phase only needs the instance to exist and be wireable. Same
// namespace-scope-static reasoning as s_editorHotReloadDebugCapability
// immediately above.
EditorProjectLifecycleCapability s_editorProjectLifecycleCapability;

} // namespace

EditorHost::SdlContext::SdlContext()
{
    // Must be installed before SDL_Init() - indeed, before literally any
    // SDL call - see SdlMemoryTracker's own doc comment for why. Mirrors
    // Application::SdlContext::SdlContext()'s exact precedent
    // (Application.cpp) verbatim. See AGENTS.md ("CPU Dependency Memory
    // Tracking").
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
    // m_hostServices has no constructor dependencies of its own (a
    // stateless adapter) - m_core is constructed injecting m_window (as
    // ISurfaceProvider&) and m_hostServices (as IHostServices&), both
    // already fully constructed by this point (matches this class's own
    // member declaration order, EditorHost.h).
    , m_hostServices()
    , m_core(m_window, m_hostServices)
    // editor-core-separation-1 campaign, PHASE16 - REFERENCE members bound
    // to m_core's own real, owned instances (mirrors
    // Application::Application()'s own identical binding, PHASE12/PHASE13).
    , m_renderer(m_core.GetRenderer())
    , m_renderGraph(m_core.GetRenderGraph())
    // CreateEditorLayer() (src/Editor/EditorLayer.h) - PHASE9's own rename
    // (Locked Design Decision #9) means this resolves unambiguously to
    // ImGuiEditorLayer.cpp's real, ImGui-backed implementation, with
    // exactly ONE definition of this name anywhere in the link.
    , m_editorLayer(CreateEditorLayer(m_window, m_renderer))
    , m_game(m_core.GetGame())
    , m_engineContext(m_core.GetEngineContext())
    , m_atmosphereSettings(m_core.GetAtmosphereSettings())
    , m_atmosphereLutRenderer(m_core.GetAtmosphereLutRenderer())
    // network-impl-2 campaign, Phase 3 - hands FrameCaptureBridge's address
    // into NetworkServer's constructor (a defaulted pointer parameter - see
    // NetworkServer.h) so its /get_game_view route handler can reach it.
    // Safe: m_captureBridge is declared (and thus constructed) before
    // m_networkServer, per EditorHost.h's own member ordering. The four
    // other bridge addresses mirror Application's own identical precedent.
    // editor-core-separation-2 campaign, PHASE3 - the sixth argument,
    // &s_editorLogQueryCapability (this file's own namespace-scope static,
    // above), so GET /get_logs/POST /clear_logs can reach the real Logger
    // singleton through the new ILogQueryCapability bridge.
    // editor-core-separation-8 campaign, PHASE5 - the seventh argument,
    // &m_renderGraphControlCommandBridge, so the 6 new GET /render_graph/*
    // routes can reach it.
    // editor-core-separation-12 campaign (Project Assembly Hot Reload plan,
    // BIG-STEP 1) - the eighth argument, &s_editorHotReloadDebugCapability,
    // so every GET/POST /project_assembly/* route can reach it.
    // editor-core-separation-16 campaign (On-Engine Project Workflow plan,
    // BIG-STEP 2), PHASE4 - the ninth argument, &s_editorProjectLifecycleCapability,
    // so POST /project_assembly/create_project can reach it.
    , m_networkServer(&m_captureBridge, &m_commandBridge, &m_uiCommandBridge, &m_frameDebuggerCommandBridge,
          &m_assetImportCommandBridge, &s_editorLogQueryCapability, &m_renderGraphControlCommandBridge,
          &s_editorHotReloadDebugCapability, &s_editorProjectLifecycleCapability)
{
    // editor-core-separation-1 campaign, PHASE3
    // (PHASE3_LOGGING_GLOBAL_LOGSINK_EXTRACTION.md) - installs the ONE real
    // ILogSink this engine ships into the global sink mechanism
    // (Core/LogSink.h), FIRST, before anything else in this constructor
    // body runs (including NetworkServer::Start() below, which can itself
    // call GTE_LOG_*) - install-once, idempotent.
    gte::InstallLogSink(&gte::LoggerLogSink::Instance());

    // PHASE0_MASTER_STRATEGY.md's Locked Design Decision #8 - the ONE new
    // wiring call this whole campaign exists to add: Core::BuildFrame()'s
    // own render-graph-frame-building hooks (Phase 13) need a genuinely
    // non-null m_editorLayer from this point onward, forever. The
    // debug-only assert below exists specifically to catch a future
    // "forgot/mistyped this exact call, or passed the wrong pointer"
    // regression immediately at startup.
    assert(m_editorLayer != nullptr && "EditorHost: m_editorLayer must be non-null before SetEditorLayerHook()");
    m_core.SetEditorLayerHook(m_editorLayer.get());

    // editor-core-separation-16 campaign (On-Engine Project Workflow plan,
    // BIG-STEP 2), PHASE4 - gives the real ImGui implementation
    // (ImGuiEditorLayer) a live IProjectLifecycleCapability* so its "New
    // Project..." window can call CreateNewProjectAssembly() directly. Safe
    // here: s_editorProjectLifecycleCapability (PHASE3's own namespace-scope
    // static) already exists via ordinary static initialization, strictly
    // before this constructor body ever runs.
    m_editorLayer->SetProjectLifecycleCapability(&s_editorProjectLifecycleCapability);

    // editor-core-separation-13 campaign (Project Assembly Hot Reload plan,
    // BIG-STEP 2), PHASE4 - hands EditorHotReloadDebugCapability a live
    // ProjectAssemblyHost& (via Core::GetProjectAssemblyHost()), strictly
    // AFTER m_core already exists but BEFORE m_networkServer.Start(8080)
    // below ever accepts a real HTTP request - see
    // EditorHotReloadDebugCapability::SetProjectAssemblyHost()'s own doc
    // comment for the full "why a setter, not a constructor parameter"
    // reasoning. Placed UNCONDITIONALLY (never inside the
    // `#if GTE_ENABLE_PROJECT_ASSEMBLIES` guard below) because
    // Core::m_projectAssemblyHost itself is an unconditional Core member -
    // only the LoadProjectAssemblies() CALL SITE below is gated.
    s_editorHotReloadDebugCapability.SetProjectAssemblyHost(m_core.GetProjectAssemblyHost());

    // editor-core-separation-16 campaign (On-Engine Project Workflow plan,
    // BIG-STEP 2), PHASE3 - gives ActiveProjectAssemblyState a live
    // ProjectAssemblyHost& so GetActive()'s own isLoaded field is real, not
    // always-false. Same "setter, not a constructor parameter" placement as
    // every capability wiring call immediately above.
    ActiveProjectAssemblyState::Instance().SetProjectAssemblyHost(m_core.GetProjectAssemblyHost());

    // editor-core-separation-14 campaign (Project Assembly Hot Reload plan,
    // BIG-STEP 3), PHASE3 - hands EditorHotReloadDebugCapability a live
    // ProjectAssemblyHotReloadCommandBridge& (m_hotReloadCommandBridge, this
    // class's own member, already constructed by this point in the
    // initializer list), so TriggerHotReload() can submit into it. Same
    // "setter, not a constructor parameter" placement as the call above.
    s_editorHotReloadDebugCapability.SetHotReloadCommandBridge(m_hotReloadCommandBridge);

    // editor-core-separation-15 campaign (Project Assembly Hot Reload plan,
    // BIG-STEP 4), PHASE4 - hands EditorHotReloadDebugCapability a live
    // EngineCommandBridge& (m_commandBridge, EditorHost's own GENERAL
    // command bridge - already constructed by this point in the
    // initializer list), so SetProbeHotReloadMarkerValueForTesting() can
    // submit into it. Same "setter, not a constructor parameter" placement
    // as the two calls above.
    s_editorHotReloadDebugCapability.SetEngineCommandBridge(m_commandBridge);

    // editor-core-separation-1 campaign, PHASE16 - hands Core the ONE
    // callback that actually calls IEditorLayer::Render(cmd) - an explicitly
    // HOST-LEVEL IEditorLayer method (Locked Design Decision #8's second
    // bucket) that EditorHost (not Core) must keep calling directly against
    // its own m_editorLayer. This lambda captures `this` (EditorHost's own
    // `this`, never Core's), so the real call still textually happens here
    // - Core merely stores/forwards this std::function value, never calling
    // IEditorLayer::Render() itself. Mirrors Application's own identical
    // wiring exactly (PHASE13) - set once, since this callback's own
    // behavior never varies frame-to-frame.
    m_core.SetPresentImGuiRecorder([this](VkCommandBuffer cmd) { m_editorLayer->Render(cmd); });

    // editor-core-separation-3 campaign, PHASE4
    // (PHASE4_EDITOR_PANEL_CAPABILITY_AND_REGISTRY.md) - seeds
    // EditorPanelRegistry with every built-in panel name BEFORE any plugin
    // panel is ever registered (so AllNames()'s own registration order
    // always lists every built-in panel first, plugins after) - this exact
    // literal list must match EditorPanelCatalog.h's own former
    // kKnownEditorPanelNames[] content byte-for-byte (confirmed before this
    // file was deleted by this same phase).
    //
    // editor-core-separation-6 campaign, PHASE3
    // (PHASE3_EDITOR_PANEL_ORCHESTRATOR_MIGRATION.md) - moved to run BEFORE
    // Core::LoadPlugins() (below), not after, so the "built-ins first"
    // invariant still holds now that EditorPanelCapabilityOrchestrator
    // registers every plugin panel automatically INSIDE
    // Core::LoadPlugins() itself (Locked Design Decision #6).
    EditorPanelRegistry::Instance().RegisterBuiltinPanelName("Hierarchy");
    EditorPanelRegistry::Instance().RegisterBuiltinPanelName("Inspector");
    EditorPanelRegistry::Instance().RegisterBuiltinPanelName("Scene");
    EditorPanelRegistry::Instance().RegisterBuiltinPanelName("Game");
    EditorPanelRegistry::Instance().RegisterBuiltinPanelName("Memory");
    EditorPanelRegistry::Instance().RegisterBuiltinPanelName("Profiler");
    EditorPanelRegistry::Instance().RegisterBuiltinPanelName("Render Graph");
    EditorPanelRegistry::Instance().RegisterBuiltinPanelName("Jobs");
    EditorPanelRegistry::Instance().RegisterBuiltinPanelName("Atmosphere");
    EditorPanelRegistry::Instance().RegisterBuiltinPanelName("Log");
#if GTE_ENABLE_PROJECT_PANEL
    EditorPanelRegistry::Instance().RegisterBuiltinPanelName("Project");
#endif

#if GTE_ENABLE_PLUGINS
    // PHASE0_MASTER_STRATEGY.md's Locked Design Decision #9 - loaded exactly
    // once, here, at EditorHost construction time. The plugins/ folder lives
    // NEXT TO the built executable (ProjectRootPath::ExecutableDirectory(),
    // the same SDL_GetBasePath() base-path resolution
    // ResolveProjectRootDirectory() already uses for the Project panel's own
    // root folder, minus the "Project" subfolder - see that file for the
    // precedent this mirrors) - never relative to the current working
    // directory, which is not guaranteed to be the exe's own folder.
    //
    // editor-core-separation-6 campaign, PHASE3
    // (PHASE3_EDITOR_PANEL_ORCHESTRATOR_MIGRATION.md) - Core::LoadPlugins()
    // now ALSO invokes every registered IPluginCapabilityOrchestrator's own
    // OnPluginsLoaded(), including the new EditorPanelCapabilityOrchestrator
    // (see Core.cpp's RegisterBuiltinCapabilityOrchestrators()) - this
    // REPLACES the former inline IEditorPanelModule_v1 discovery loop that
    // used to run here, right after this call, with zero observable
    // behavior change (same EditorPanelRegistry::RegisterPluginPanel() call,
    // same AllLoadedModules() list, same QueryCapability() name).
    m_core.LoadPlugins(gte::ExecutableDirectory() / "plugins");
#endif

    // editor-core-separation-11 campaign (Project Assembly system), PHASE5 -
    // loaded exactly once, here, at EditorHost construction time, mirroring
    // Core::LoadPlugins()'s own call immediately above. Deliberately its OWN,
    // separate scan (GTE_PROJECT_ASSEMBLY_OUTPUT_DIR's own runtime folder,
    // "<exe dir>/project_assemblies/" - NEVER "<exe dir>/plugins/") - the two
    // coexisting systems must never be confused for one another. `this`
    // (EditorHost) is passed as the live gte::EditorHost& every loaded
    // "*_Editor.dll" receives. Gated by GTE_ENABLE_PROJECT_ASSEMBLIES - its
    // OWN flag, never GTE_ENABLE_PLUGINS (PHASE0_MASTER_STRATEGY.md, Finding
    // D).
#if GTE_ENABLE_PROJECT_ASSEMBLIES
    m_core.LoadProjectAssemblies(ResolveProjectAssemblyOutputDirectory(gte::ExecutableDirectory()), this);
#endif

    // editor-core-separation-1 campaign, PHASE16 - wires the ONE real
    // ISceneIOCapability implementation this engine ships
    // (Editor/EditorSceneIOCapability.h) into the nullable pointer
    // EngineCommandDispatch.cpp (Core-destined, always-compiled) consults at
    // runtime. This is the PERMANENT home for this wiring (Application's own
    // SetSceneIOCapability()-based version, PHASE5/PHASE6, was explicitly
    // documented as temporary). A function-local `static` (mirrors
    // LoggerLogSink::Instance()'s own Meyers-singleton precedent) -
    // EditorSceneIOCapability is pure delegation with no state of its own,
    // so one whole-process-lifetime instance is all this needs.
    static EditorSceneIOCapability s_editorSceneIOCapability;
    m_sceneIOCapability = &s_editorSceneIOCapability;

#if GTE_ENABLE_NETWORK
    // Loopback-only (127.0.0.1 is baked into NetworkServer itself - Start()
    // deliberately has no host parameter, see Phase 2), port 8080 - see
    // AGENTS.md, "Networking". A bind failure (e.g. another instance of the
    // engine already running and holding port 8080) is logged by
    // NetworkServer itself and is NON-FATAL - the rest of EditorHost still
    // starts normally either way.
    m_networkServer.Start(8080);
#endif

    // logger-1 campaign, Phase 2 - proves GTE_LOG_* is reachable, right
    // after every other subsystem (including the network server, above) is
    // already constructed/started.
    GTE_LOG_INFO("EditorHost",
        "EditorHost constructed: SdlContext -> Window -> Core -> CreateEditorLayer() -> "
        "Core::SetEditorLayerHook() all completed, with a genuinely non-null IEditorLayer*, "
        "every automation bridge attached, and NetworkServer started.");
}

EditorHost::~EditorHost() = default;

int EditorHost::Run()
{
    // editor-core-separation-1 campaign, PHASE16
    // (PHASE16_EDITORHOST_MAIN_LOOP_AND_AUTOMATION_BRIDGES.md) - the REAL
    // main loop, copied byte-for-byte (member names/order preserved) from
    // Application::Run()'s own post-PHASE13 shape (see
    // PHASE13_COMPLETION_REPORT.md for the full itemized accounting of
    // which IEditorLayer call sites live here, host-level, versus inside
    // Core::BuildFrame()). This REPLACES PHASE15's own deliberate, temporary
    // stub - EditorHost is now FULLY functionally equivalent to the old
    // Application-based executable.
    bool running = true;
    // Identifies EditorHost's own main game window among possibly several
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
        // rather than adding a new, Profiler-panel-specific control.
        m_renderer.SetGpuTimingCaptureEnabled(Profiling::FrameProfiler::Instance().IsCaptureEnabled());
        // B.1 (B1_REAL_GPU_TIMING_STRATEGY_v1.md) - the same runtime toggle,
        // now ALSO applied to the render graph's own, independent
        // RenderGraphTimestampPool.
        m_renderGraph.SetGpuTimingCaptureEnabled(Profiling::FrameProfiler::Instance().IsCaptureEnabled());

        // Clear last frame's transient "just pressed/released" flags and
        // per-frame mouse/wheel deltas before this frame's events arrive.
        inputState.BeginFrame();

        {
            GTE_PROFILE_SCOPE("EditorHost::PollEvents");
            SDL_Event sdlEvent;
            while (SDL_PollEvent(&sdlEvent)) {
                // Editor gets first look at every raw event (its own SDL3
                // backend tracks mouse/keyboard for ImGui widgets). HOST-LEVEL
                // (Locked Design Decision #8's second bucket) - stays
                // directly on m_editorLayer, never routed through Core.
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
                    // editor-core-separation-1 campaign, PHASE13 - keeps
                    // Core's own present-regime "no Game/Scene panel
                    // visible, render Game directly to swapchain" fallback
                    // aspect-ratio computation correct across a live resize
                    // - see Core::NotifyWindowResized()'s own doc comment.
                    m_core.NotifyWindowResized(resized.width, resized.height);
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
                // "click-through" problem).
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
        // Physics/Animation run this frame.
        if (const std::optional<EngineCommandRequest> request = m_commandBridge.TryPeekPendingCommandRequest()) {
            GTE_PROFILE_SCOPE("EditorHost::ExecuteEngineCommand");
            const EngineCommandResult result =
                ExecuteEngineCommand(m_game, m_renderer, m_sceneIOCapability, &s_editorHotReloadDebugCapability, *request);
            m_commandBridge.FulfillCommand(result);
        }

        // editor-core-separation-14 campaign (Project Assembly Hot Reload plan,
        // BIG-STEP 3), PHASE3 - drained at most once per frame, exactly like the
        // existing EngineCommandBridge block immediately above. Unlike that
        // bridge, servicing this request BLOCKS this same thread for the ENTIRE
        // duration of PerformProjectAssemblyHotReload() once PHASE4 replaces its
        // temporary body (LDD-HR4) - this is intentional: the whole point of this
        // feature is a hard, synchronous freeze. No other per-frame work below
        // this point runs until it returns.
        if (const std::optional<std::string> requestedProject = m_hotReloadCommandBridge.TryPeekPendingProjectName()) {
            GTE_PROFILE_SCOPE("EditorHost::PerformProjectAssemblyHotReload");
            // Resolved HERE, on the main thread (gte_editor-tier - this file already
            // includes ProjectRootPath.h/ProjectAssemblyBuildRunner.h for its own
            // LoadProjectAssemblies() call above), and passed into the orchestrator
            // as plain std::filesystem::path VALUES - PerformProjectAssemblyHotReload()
            // itself lives in src/Core/Plugins/ (gte_core-tier, see that header's own
            // doc comment) and must NEVER call gte::ExecutableDirectory() itself
            // (gte_editor-tier, defined only in ProjectRootPath.cpp) - see
            // PHASE0_MASTER_STRATEGY.md, Section 2.2 item 6, for the full layering
            // hazard this avoids.
            const std::filesystem::path outputDirectory = ResolveProjectAssemblyOutputDirectory(gte::ExecutableDirectory());
            const std::filesystem::path buildDirectory = ResolveCMakeBuildDirectory(gte::ExecutableDirectory());
            // editor-core-separation-15 campaign (Project Assembly Hot Reload
            // plan, BIG-STEP 4), PHASE1 - resolved HERE for the exact same
            // reason outputDirectory/buildDirectory are: this file is
            // gte_editor-tier (already includes ProjectRootPath.h - confirmed,
            // this translation unit's own #include list), and
            // PerformProjectAssemblyHotReload() itself is gte_core-tier and
            // must never resolve this path internally.
            const std::filesystem::path projectRootDirectory = ResolveProjectRootDirectory();
            PerformProjectAssemblyHotReload(*requestedProject, m_core, m_renderer, this, outputDirectory, buildDirectory, projectRootDirectory);
            m_hotReloadCommandBridge.FulfillPending();
        }

        const Uint64 nowTicksNs = SDL_GetTicksNS();
        const double deltaSeconds = static_cast<double>(nowTicksNs - lastTicksNs) / 1000000000.0;
        lastTicksNs = nowTicksNs;

        // frame-debugger-1 campaign, PHASE4 - read the Editor's own Pause/Step
        // toolbar state (see PHASE3's IEditorLayer::IsPlaybackPaused()/
        // TryConsumeStepRequest()) - HOST-LEVEL (Locked Design Decision #8's
        // second bucket).
        const bool playbackPaused = m_editorLayer->IsPlaybackPaused();
        // Deliberately called EVERY frame, unconditionally (never short-circuited
        // by `playbackPaused &&`) so a stray/stale pending step request can never
        // linger un-cleared.
        const bool stepRequestedRaw = m_editorLayer->TryConsumeStepRequest();
        const bool steppedThisFrame = playbackPaused && stepRequestedRaw;

        // task_manager/frame-debugger-3 campaign, PHASE3 - records "a Step
        // happened this frame" for the Frame Debugger's own later use -
        // HOST-LEVEL, stays directly on m_editorLayer.
        if (steppedThisFrame) {
            m_editorLayer->NotifyFrameDebuggerStepConsumed();
        }

        // editor-core-separation-1 campaign, PHASE13 - advances Time
        // (respecting the already-resolved Pause/Step booleans above) and
        // dispatches Game::Update().
        InputFrame inputFrame;
        inputFrame.inputState = &inputState;
        inputFrame.playbackPaused = playbackPaused;
        inputFrame.stepRequested = stepRequestedRaw;
        m_core.Update(inputFrame, static_cast<float>(deltaSeconds));

        // logger-1 campaign, Phase 2 - stamps every Logger entry recorded
        // from here until the next Advance() (now inside Core::Update()
        // above) with THIS frame's number. Safe to call unconditionally -
        // this call itself never touches m_editorLayer, so it stays here
        // directly on EditorHost rather than moving into Core (Core must
        // never #include Editor/Logger.h).
        gte::Logger::SetCurrentFrame(m_engineContext.time.FrameCount());

        m_editorLayer->NewFrame();

        // network-impl-7 campaign - drains at most ONE pending
        // GET /activate_tab request per frame, IMMEDIATELY after
        // NewFrame() and BEFORE BuildUI().
        if (const std::optional<EditorUiCommandRequest> uiRequest = m_uiCommandBridge.TryPeekPendingCommandRequest()) {
            GTE_PROFILE_SCOPE("EditorHost::ExecuteEditorUiCommand");
            EditorUiCommandResult uiResult;
            uiResult.kind = uiRequest->kind;
            // GPU-Driven Frustum Culling + Indirect Draw campaign
            // (render-pass-5), PHASE6 - a second EditorUiCommandKind now
            // exists (SpawnGpuDrivenTestBatch).
            if (uiRequest->kind == EditorUiCommandKind::SpawnGpuDrivenTestBatch) {
                const GpuDrivenTestBatchSpawnResult spawned = m_editorLayer->SpawnGpuDrivenTestBatch(
                    m_game, m_renderer, uiRequest->spawnGpuDrivenTestBatch.instanceCount);
                uiResult.spawnGpuDrivenTestBatch.success = spawned.success;
                uiResult.spawnGpuDrivenTestBatch.editorAvailable = spawned.editorAvailable;
                uiResult.spawnGpuDrivenTestBatch.errorMessage = spawned.errorMessage;
                uiResult.spawnGpuDrivenTestBatch.instanceCount = spawned.instanceCount;
            } else {
                const TabActivationResult activation = m_editorLayer->ActivateTab(uiRequest->activateTab.tabName);
                uiResult.activateTab.tabExists = activation.tabExists;
                uiResult.activateTab.success = activation.tabExists;
            }
            m_uiCommandBridge.FulfillCommand(uiResult);
        }

        // task_manager/frame-debugger-3 campaign, PHASE7 - drains at most
        // ONE pending Frame Debugger command per frame, at the SAME point in
        // the loop EditorUiCommandBridge's own pump immediately above
        // already runs.
        if (const std::optional<FrameDebuggerCommandRequest> fdRequest =
                m_frameDebuggerCommandBridge.TryPeekPendingCommandRequest()) {
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

        // editor-core-separation-8 campaign, PHASE5
        // (PHASE5_CROSS_THREAD_BRIDGE_AND_HTTP_ENDPOINTS.md) - drains at most
        // ONE pending render-graph-control command per frame, at the SAME
        // point in the loop every other bridge's own pump immediately above
        // already runs. Built-in-pass/plugin-feature mutations go straight
        // to m_core (Core-owned state); Blur/GBuffer mutations go through
        // m_editorLayer (Editor-owned EditorContext state) - see
        // PHASE0_MASTER_STRATEGY.md's Step 2.6 for exactly why these two
        // categories are routed differently even though they share this one
        // bridge.
        if (const std::optional<RenderGraphControlCommandRequest> rgcRequest =
                m_renderGraphControlCommandBridge.TryPeekPendingCommandRequest()) {
            GTE_PROFILE_SCOPE("EditorHost::ExecuteRenderGraphControlCommand");
            RenderGraphControlCommandResult rgcResult;
            rgcResult.kind = rgcRequest->kind;
            switch (rgcRequest->kind) {
            case RenderGraphControlCommandKind::SetBuiltInPassEnabled: {
                const bool applied = m_core.GetRenderPassToggleRegistryMutable().SetEnabled(
                    rgcRequest->setPassEnabled.name, rgcRequest->setPassEnabled.enabled);
                rgcResult.success = applied;
                if (!applied) {
                    rgcResult.errorMessage = "\"" + rgcRequest->setPassEnabled.name + "\" cannot be disabled (deny-listed).";
                }
                break;
            }
            case RenderGraphControlCommandKind::ListPassStates: {
                for (const rg::RenderPassToggleState& state : m_core.GetRenderPassToggleRegistryMutable().ListAll()) {
                    RenderGraphControlPassStateOutcome outcome;
                    outcome.name = state.name;
                    outcome.enabled = state.enabled;
                    outcome.everDeclaredThisSession = state.everDeclaredThisSession;
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
            case RenderGraphControlCommandKind::SetBlurEnabled:
                m_editorLayer->SetShowBlurredSceneOutput(rgcRequest->setBlurEnabled.enabled);
                rgcResult.success = true;
                break;
            case RenderGraphControlCommandKind::SetGBufferEnabled:
                m_editorLayer->SetShowGBufferValidationOutput(rgcRequest->setGBufferEnabled.enabled);
                rgcResult.success = true;
                break;
            }
            m_renderGraphControlCommandBridge.FulfillCommand(rgcResult);
        }

        // task_manager/stl-parser-2, PHASE1 - drains at most ONE pending
        // /import_asset request per frame.
        if (const std::optional<AssetImportCommandRequest> importRequest =
                m_assetImportCommandBridge.TryPeekPendingCommandRequest()) {
            GTE_PROFILE_SCOPE("EditorHost::ExecuteAssetImportCommand");
            AssetImportCommandResult importResult;
            importResult.kind = importRequest->kind;
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

        // editor-core-separation-1 campaign, PHASE13 - the REAL per-frame
        // Render Graph orchestration now lives entirely inside
        // Core::BuildFrame().
        m_core.BuildFrame();

        // network-impl-2 campaign, Phase 3 - the FrameCaptureBridge
        // fast-fail + success-path capture for the Game View - a HOST-LEVEL
        // AUTOMATION concern (design doc Section 6.1) that stays here,
        // running right AFTER BuildFrame() returns (safe: BuildFrame()
        // already fence-waited internally before returning).
        RenderTexture* gameTargetThisFrame = m_core.GetGameViewTargetThisFrame();
        if (gameTargetThisFrame == nullptr && m_captureBridge.IsCaptureRequested(FrameCaptureKind::GameView)) {
            m_captureBridge.FailPendingRequest(FrameCaptureKind::GameView, FrameCaptureFailureReason::TargetNotAvailable);
        }
        if (gameTargetThisFrame != nullptr && m_captureBridge.IsCaptureRequested(FrameCaptureKind::GameView)) {
            // Atmosphere Scattering + Aerial Perspective campaign,
            // Phase 7 - captures "GameViewComposited" (the atmosphere-
            // composited output) instead of the original, pre-composite
            // render target.
            RenderTexture* captureSource = m_atmosphereLutRenderer.CompositedOutput("GameViewComposited");
            if (captureSource == nullptr) {
                captureSource = gameTargetThisFrame;
            }
            Renderer::CapturedRawPixels raw = m_renderer.CaptureRenderTexturePixels(*captureSource);
            if (IsBgraFormat(raw.format)) {
                Encoding::ConvertBgraToRgbaInPlace(raw.pixels.data(), raw.width, raw.height);
            }
            std::vector<std::uint8_t> png = Encoding::EncodeRgba8ToPng(raw.pixels.data(), raw.width, raw.height);
            m_captureBridge.FulfillPendingRequest(FrameCaptureKind::GameView,
                CapturedPngImage{ std::move(png), raw.width, raw.height });
        }

        // Build every editor panel (Hierarchy/Inspector/Scene/Game/Memory/menu
        // bar) now that the Game/Scene view textures (if any) have this
        // frame's contents. HOST-LEVEL (Locked Design Decision #8's second
        // bucket) - stays directly on m_editorLayer.
        {
            GTE_PROFILE_SCOPE("IEditorLayer::BuildUI");
            // editor-core-separation-6 campaign, PHASE7
            // (PHASE7_RENDER_GRAPH_PANEL_VISIBILITY.md) - mirrors
            // m_core.GetGpuDrivenBatchDebugInfo()'s own exact "computed here,
            // passed as a plain trailing argument" precedent immediately
            // below: null-checked, since GetRenderFeatureCompositor() can, in
            // principle, be null (see Core.h's own doc comment) even though
            // RegisterBuiltinCapabilityOrchestrators() always registers one
            // today.
            const RenderFeatureCompositor* renderFeatureCompositor = m_core.GetRenderFeatureCompositor();
            const std::vector<RenderFeatureDebugEntry> renderFeatureEntries =
                renderFeatureCompositor != nullptr ? renderFeatureCompositor->DebugSnapshot()
                                                    : std::vector<RenderFeatureDebugEntry>{};
            m_editorLayer->BuildUI(m_game, m_renderer, m_renderGraph, m_atmosphereSettings, m_atmosphereLutRenderer,
                m_core.GetGpuDrivenBatchDebugInfo(), renderFeatureEntries,
                // editor-core-separation-8 campaign, PHASE3 - 2 new trailing
                // arguments. NOTE: deliberately calling
                // m_core.GetRenderFeatureCompositor() a SECOND, fresh time
                // here rather than reusing the `renderFeatureCompositor`
                // local declared a few lines above - that local is typed
                // `const RenderFeatureCompositor*` (its own declared type
                // never changed; only Core::GetRenderFeatureCompositor()'s
                // OWN return type widened in PHASE2) and would not compile
                // against BuildUI()'s new plain (non-const)
                // `RenderFeatureCompositor*` parameter. This second call is
                // cheap (Core.h's own noexcept accessor just returns an
                // already-cached pointer).
                m_core.GetRenderPassToggleRegistryMutable(), m_core.GetRenderFeatureCompositor());

            // editor-core-separation-7 campaign, PHASE4 - GET /render_graph
            // support. Reuses renderFeatureEntries (still in scope here) so
            // RenderFeatureCompositor::DebugSnapshot() is never called a
            // second time this Run() iteration (PHASE0_MASTER_STRATEGY.md's
            // Locked Design Decision #7). Independent of RenderGraphPanel's
            // own "Pause" checkbox - always the truly latest frame's real
            // data (Locked Design Decision #9).
            m_captureBridge.PublishRenderGraphMetadata(rg::BuildRenderGraphMetadata(
                m_renderGraph.LastSnapshot(rg::ExecuteTimingMode::SynchronousImmediateReadback),
                m_renderGraph.LastSnapshot(rg::ExecuteTimingMode::PipelinedDeferredReadback),
                m_core.GetGpuDrivenBatchDebugInfo(), renderFeatureEntries));
        }

        // File > Exit (or any other future programmatic "close" UI action)
        // ends the loop exactly like a Quit event/closing the OS window.
        if (m_editorLayer->WantsExit()) {
            running = false;
        }

        // network-impl-2 campaign, Phase 5 - must run BEFORE Core::Present()
        // below so SwapchainCaptureService::RecordCaptureIfRequested() sees
        // m_captureRequested == true in time this frame.
        if (m_captureBridge.IsCaptureRequested(FrameCaptureKind::Swapchain)) {
            m_renderer.RequestSwapchainCapture();
        }

        // editor-core-separation-1 campaign, PHASE13 - Call 2 of 2: the
        // PIPELINED swapchain-present regime, now entirely inside
        // Core::Present().
        m_core.Present();

        // network-impl-2 campaign, Phase 5 - the FrameCaptureBridge
        // success-path capture for the swapchain - HOST-LEVEL AUTOMATION,
        // running right AFTER Present() returns (safe - Present()'s own
        // Renderer::PresentViaRenderGraph() call already fence-waits before
        // returning).
        if (std::optional<CapturedSwapchainPixels> raw = m_renderer.TakeLastCompletedSwapchainCapture()) {
            if (IsBgraFormat(raw->format)) {
                Encoding::ConvertBgraToRgbaInPlace(raw->pixels.data(), raw->width, raw->height);
            }
            std::vector<std::uint8_t> png = Encoding::EncodeRgba8ToPng(raw->pixels.data(), raw->width, raw->height);
            m_captureBridge.FulfillPendingRequest(FrameCaptureKind::Swapchain,
                CapturedPngImage{ std::move(png), raw->width, raw->height });
        }

        // Update/present any panel the user has dragged outside the main OS
        // window (Dear ImGui multi-viewport/"platform windows" - a no-op in
        // a release build, see NullEditorLayer::RenderPlatformWindows()).
        m_editorLayer->RenderPlatformWindows();

        // network-impl-4 campaign, Phase 4 - GET /get_texture's own capture
        // path. Placed here (unconditionally, once per Run() iteration,
        // AFTER every render-graph Execute() call this frame) so
        // RenderGraph::DebugTextureSnapshotFor() sees the freshest possible
        // registry state.
        if (m_captureBridge.IsCaptureRequested(FrameCaptureKind::NamedTexture)) {
            const std::string requestedName = m_captureBridge.RequestedTextureName();
            const DebugTextureChannel requestedChannel = m_captureBridge.RequestedTextureChannel();

            if (const std::optional<rg::DebugTextureSnapshot> snapshot = m_renderGraph.DebugTextureSnapshotFor(requestedName)) {
                const bool wantsDepth = (requestedChannel == DebugTextureChannel::Depth);
                if (wantsDepth && !snapshot->hasDepth) {
                    // Positively known, permanent-for-this-registration failure -
                    // fail fast (409) rather than waiting out the full timeout.
                    m_captureBridge.FailPendingRequest(FrameCaptureKind::NamedTexture, FrameCaptureFailureReason::TargetNotAvailable);
                } else {
                    // PHASE0_MASTER_STRATEGY.md's (network-impl-4 campaign)
                    // Locked Design Decision 4 - computed BEFORE
                    // WaitForGpuIdle()/the readback below, from the snapshot
                    // as it was at the moment this request was actually
                    // serviced.
                    const std::uint64_t framesSinceUpdate =
                        m_renderGraph.CurrentDebugTextureFrameCounter() - snapshot->lastUpdatedFrameCounter;

                    m_renderer.WaitForGpuIdle();

                    const VkImage image = wantsDepth ? snapshot->target.depthImage : snapshot->target.image;
                    const VkImageAspectFlags aspect = wantsDepth ? VK_IMAGE_ASPECT_DEPTH_BIT : VK_IMAGE_ASPECT_COLOR_BIT;
                    const VkFormat format = wantsDepth ? snapshot->target.depthFormat : snapshot->target.format;
                    const rg::ResourceState state = wantsDepth ? snapshot->depthState : snapshot->colorState;

                    // Atmosphere Scattering + Aerial Perspective campaign,
                    // Phase 4 - the FIRST capturable color texture that is
                    // genuinely NOT 4 bytes/pixel (VK_FORMAT_R16G16B16A16_SFLOAT
                    // is 8 - see AtmosphereLutRenderer's own Multi-Scattering
                    // LUT output).
                    const bool isHdrColor = !wantsDepth && format == VK_FORMAT_R16G16B16A16_SFLOAT;
                    const int bytesPerPixel = isHdrColor ? 8 : 4;

                    Renderer::CapturedRawPixels raw =
                        m_renderer.CaptureImagePixels(image, aspect, format, snapshot->target.extent, state, bytesPerPixel);

                    // A separate, always-4-bytes/pixel buffer PNG encoding
                    // actually reads from - identical to raw.pixels for
                    // every "traditional" 4-bytes/pixel capture, but a
                    // genuinely different, freshly-allocated buffer for the
                    // HDR case above.
                    std::vector<std::uint8_t> hdrConvertedPixels;
                    const std::uint8_t* encodePixels = raw.pixels.data();

                    bool ok = true;
                    if (wantsDepth) {
                        // Safe to write in-place into the SAME buffer it
                        // reads from - every pixel's 4 input bytes are fully
                        // read into a local temporary BEFORE any of that
                        // pixel's own 4 output bytes are written, and
                        // input/output share the exact same per-pixel byte
                        // offset (no shift).
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
                        // ConvertDepthToGrayscaleRgba8() recognizes, or the
                        // color format isn't one ConvertHdrRgba16fToRgba8()
                        // recognizes - an accepted, documented, narrow risk.
                        m_captureBridge.FailPendingRequest(FrameCaptureKind::NamedTexture, FrameCaptureFailureReason::TargetNotAvailable);
                    } else {
                        std::vector<std::uint8_t> png = Encoding::EncodeRgba8ToPng(encodePixels, raw.width, raw.height);
                        m_captureBridge.FulfillPendingRequest(FrameCaptureKind::NamedTexture,
                            CapturedPngImage{ std::move(png), raw.width, raw.height, framesSinceUpdate });
                    }
                }
            } else if (const std::optional<rg::DebugVolumeTextureSnapshot> volumeSnapshot =
                           m_renderGraph.DebugVolumeTextureSnapshotFor(requestedName)) {
                // network-impl-6 campaign, Phase 4 - a name that isn't a
                // registered 2D texture but IS a registered volume texture
                // now renders a fresh raymarch thumbnail on demand instead
                // of copying already-rendered pixels.
                if (requestedChannel == DebugTextureChannel::Depth) {
                    // A volume texture has no depth-companion concept at all
                    // (see VolumeTarget.h) - the same positively-known,
                    // permanent-failure fast-fail (409) the 2D branch above
                    // uses.
                    m_captureBridge.FailPendingRequest(FrameCaptureKind::NamedTexture, FrameCaptureFailureReason::TargetNotAvailable);
                } else {
                    // Computed BEFORE WaitForGpuIdle()/the render below, from
                    // the snapshot as it was at the moment this request was
                    // actually serviced - exactly mirroring the 2D branch's
                    // own identical reasoning above.
                    const std::uint64_t framesSinceUpdate =
                        m_renderGraph.CurrentDebugTextureFrameCounter() - volumeSnapshot->lastUpdatedFrameCounter;

                    m_renderer.WaitForGpuIdle();

                    // atmosphere-scattering-2 campaign, Phase 4 - auto-detect
                    // the Aerial Perspective volume by name so its preview
                    // uses the atmosphere-aware transmittance/in-scattering
                    // interpretation instead of the generic density/color
                    // one - zero new HTTP endpoint/query parameter.
                    //
                    // frame-debugger-5 campaign, PHASE4 - this rule is now a
                    // shared, named, pure function
                    // (VolumeTexturePreviewRenderer.h's
                    // SelectVolumeTexturePreviewInterpretation()).
                    const VolumeTexturePreviewInterpretation interpretation =
                        SelectVolumeTexturePreviewInterpretation(requestedName);

                    const VolumeTexturePreviewRenderer::CapturedRawPixels raw = m_volumeTexturePreviewRenderer.RenderPreview(
                        m_renderer, volumeSnapshot->target, volumeSnapshot->state, interpretation);

                    // raw.pixels is already tightly-packed RGBA8 - no BGRA
                    // swizzle, no HDR conversion, no depth-to-grayscale
                    // conversion needed at all, unlike the 2D branch's
                    // several format-dependent branches above.
                    std::vector<std::uint8_t> png = Encoding::EncodeRgba8ToPng(raw.pixels.data(), raw.width, raw.height);
                    m_captureBridge.FulfillPendingRequest(FrameCaptureKind::NamedTexture,
                        CapturedPngImage{ std::move(png), raw.width, raw.height, framesSinceUpdate });
                }
            }
            // else: this name has never been registered as EITHER kind yet
            // this session - leave the request pending; either it starts
            // rendering within the bridge's existing fixed timeout, or the
            // caller eventually gets HTTP 504.
        }

        // network-impl-4 campaign, Phase 5 - GET /list_textures' only data
        // source. Resolved to plain PublishedTextureListEntry values HERE,
        // never inside FrameCaptureBridge itself.
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
                entry.framesSinceUpdate = currentFrameCounter - snap.lastUpdatedFrameCounter;
                entry.kind = "texture2d"; // network-impl-6 campaign, Phase 5 - explicit at every call site.
                published.push_back(std::move(entry));
            }

            // network-impl-6 campaign, Phase 5 - every registered VOLUME
            // texture also gets its own PublishedTextureListEntry, appended
            // right after every 2D one.
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

        // Phase 5 (GPU memory usage over time) - one real GPU memory
        // snapshot per profiler frame, taken as late as possible in the
        // frame (still inside this BeginFrame()/EndFrame() bracket) so it
        // reflects every resource created/destroyed anywhere this frame,
        // including by IEditorLayer::BuildUI()'s own Inspector/Project-panel
        // asset loading above.
        Profiling::FrameProfiler::Instance().SetMemorySnapshot(BuildMemorySnapshot(m_renderer.GetMemoryTotals()));

        Profiling::FrameProfiler::Instance().EndFrame();
    }

    return 0;
}

} // namespace gte
