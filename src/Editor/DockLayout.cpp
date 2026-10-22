#include "DockLayout.h"
#include "EditorContext.h"
#include "../Core/EditorPanelRegistry.h"
#include "PlaybackControls.h"
#include "SceneIO.h"
#include "SponzaSceneOpener.h"
#include "../Core/EditorCapabilities.h" // IHotReloadDebugCapability - dereferenced below, needs the full type.
#include "ActiveProjectAssemblyState.h"

#include "../Game/Game.h"

// imgui_internal.h provides the DockBuilder* API; there is no public alternative.
#include <imgui.h>
#include <imgui_internal.h>

#include <chrono>
#include <string>

namespace gte {

namespace {

// True if the dockspace node is missing, or any panel window exists but was never docked (DockId == 0).
bool DefaultDockLayoutIsNeeded(ImGuiID dockspaceId)
{
    if (ImGui::DockBuilderGetNode(dockspaceId) == nullptr) {
        return true;
    }
    for (const std::string& panelName : EditorPanelRegistry::Instance().AllNames()) {
        const ImGuiWindow* window = ImGui::FindWindowByName(panelName.c_str());
        if (window != nullptr && window->DockId == 0) {
            return true;
        }
    }
    return false;
}

// Default layout, built once: Hierarchy left, Inspector right, Scene/Game tabs center, wide bottom strip.
// The bottom strip is split first so it spans the full width.
void BuildDefaultDockLayout(ImGuiID dockspaceId, ImVec2 size)
{
    ImGui::DockBuilderRemoveNode(dockspaceId);
    ImGui::DockBuilderAddNode(dockspaceId, ImGuiDockNodeFlags_DockSpace);
    ImGui::DockBuilderSetNodeSize(dockspaceId, size);

    ImGuiID center = dockspaceId;
    const ImGuiID bottom = ImGui::DockBuilderSplitNode(center, ImGuiDir_Down, 0.25f, nullptr, &center);
    const ImGuiID left = ImGui::DockBuilderSplitNode(center, ImGuiDir_Left, 0.20f, nullptr, &center);
    const ImGuiID right = ImGui::DockBuilderSplitNode(center, ImGuiDir_Right, 0.28f, nullptr, &center);

    ImGui::DockBuilderDockWindow("Hierarchy", left);
    ImGui::DockBuilderDockWindow("Inspector", right);
    ImGui::DockBuilderDockWindow("Scene", center);
    ImGui::DockBuilderDockWindow("Game", center);
    ImGui::DockBuilderDockWindow("Memory", bottom);
    ImGui::DockBuilderDockWindow("Profiler", bottom);
    // "Render Graph" is a floating window and is not docked here.
    ImGui::DockBuilderDockWindow("Jobs", bottom);
    ImGui::DockBuilderDockWindow("Log", bottom);
#if GTE_ENABLE_PROJECT_PANEL
    // Tabbed alongside "Memory", like Unity's bottom "Project" tab.
    ImGui::DockBuilderDockWindow("Project", bottom);
#endif

    // Every plugin panel (built-in or Project Assembly) is tabbed into the bottom node.
    for (const auto& entry : EditorPanelRegistry::Instance().PluginPanels()) {
        ImGui::DockBuilderDockWindow(entry.name.c_str(), bottom);
    }
    ImGui::DockBuilderFinish(dockspaceId);
}
} // namespace

void BuildDockspaceAndMenuBar(EditorContext& ctx, Game& game, Renderer& renderer,
    IHotReloadDebugCapability* hotReloadDebugCapability, DescriptorSetLayoutHandle sceneServicesSetLayout)
{
    const ImGuiViewport* viewport = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(viewport->WorkPos);
    ImGui::SetNextWindowSize(viewport->WorkSize);
    ImGui::SetNextWindowViewport(viewport->ID);

    constexpr ImGuiWindowFlags hostFlags = ImGuiWindowFlags_NoDocking | ImGuiWindowFlags_NoTitleBar
        | ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove
        | ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoNavFocus | ImGuiWindowFlags_NoBackground
        | ImGuiWindowFlags_MenuBar;

    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
    ImGui::Begin("EditorDockSpaceHost", nullptr, hostFlags);
    ImGui::PopStyleVar(3);

    // Runs Save/Load and stamps the ctx.sceneIo* status; shared by the File menu and the Ctrl+S/O shortcuts.
    auto reportStatus = [&ctx](bool ok, const std::string& okMessage, const std::string& failMessage) {
        ctx.sceneIoStatusMessage = ok ? okMessage : failMessage;
        ctx.sceneIoStatusIsError = !ok;
        ctx.sceneIoStatusSetTime = std::chrono::steady_clock::now();
    };

    if (ImGui::BeginMenuBar()) {
        if (ImGui::BeginMenu("File")) {
            if (ImGui::MenuItem("Save Scene", "Ctrl+S")) {
                const bool saved = SaveScene(game);
                reportStatus(saved, "Saved scene to " + DefaultScenePath().string(), "Failed to save scene.");
            }
            if (ImGui::MenuItem("Open Scene", "Ctrl+O")) {
                const bool loaded = LoadScene(game, renderer, sceneServicesSetLayout);
                reportStatus(loaded, "Loaded scene from " + DefaultScenePath().string(),
                    "Failed to load scene (missing or malformed file).");
            }
            if (ImGui::MenuItem("Open Sponza Scene")) {
                const OpenSponzaSceneResult opened = OpenSponzaScene(game, renderer, sceneServicesSetLayout);
                reportStatus(opened.success, opened.message, opened.message);
            }
            ImGui::Separator();
            // Sets a flag instead of calling exit() so shutdown goes through the normal RAII teardown.
            if (ImGui::MenuItem("Exit")) {
                ctx.exitRequested = true;
            }
            ImGui::EndMenu();
        }
        if (ImGui::BeginMenu("Window")) {
            // Floating (not docked) window; the checkmark follows ctx.frameDebuggerWindowOpen,
            // including a close via the titlebar [x].
            ImGui::MenuItem("Frame Debugger", nullptr, &ctx.frameDebuggerWindowOpen);
            // Render Graph is a floating window too (not docked), same
            // open/close convention as Frame Debugger above.
            ImGui::MenuItem("Render Graph", nullptr, &ctx.renderGraphWindowOpen);
            // Render Features is a floating window too (not docked), same
            // open/close convention as Frame Debugger/Render Graph above.
            ImGui::MenuItem("Render Features", nullptr, &ctx.renderFeaturesWindowOpen);
            ImGui::EndMenu();
        }
        if (ImGui::BeginMenu("Project")) {
            if (ImGui::MenuItem("New Project...")) {
                ctx.newProjectWindowOpen = true;
            }
            // Opens OpenProjectWindow.h.
            if (ImGui::MenuItem("Open Project...")) {
                ctx.openProjectWindowOpen = true;
            }
            ImGui::Separator();
            // Stays enabled while a build runs so a click can report "already in progress";
            // only the label gains a "(compiling...)" suffix.
            {
                const ActiveProjectAssemblyInfo active = ActiveProjectAssemblyState::Instance().GetActive();
                const bool buildInFlight = (hotReloadDebugCapability != nullptr && active.hasActiveProject)
                    && hotReloadDebugCapability->IsCompileInFlight(active.name);
                std::string compileLabel = active.hasActiveProject ? ("Compile '" + active.name + "'") : "Compile";
                if (buildInFlight) {
                    compileLabel += " (compiling...)";
                }
                const bool compileEnabled = active.hasActiveProject && hotReloadDebugCapability != nullptr;
                if (ImGui::MenuItem(compileLabel.c_str(), nullptr, false, compileEnabled)) {
                    const bool started = hotReloadDebugCapability->TriggerCompileOnly(active.name);
                    ctx.projectWorkflowStatusMessage = started
                        ? ("Compiling '" + active.name + "' - watch the Log panel for progress.")
                        : ("A build for '" + active.name + "' is already in progress.");
                    ctx.projectWorkflowStatusIsError = !started;
                    ctx.projectWorkflowStatusSetTime = std::chrono::steady_clock::now();
                }
            }
            ImGui::EndMenu();
        }
        ImGui::EndMenuBar();
    }

    // Pause/Step playback toolbar, a fixed strip under the menu bar (see PlaybackControls.h).
    BuildPlaybackToolbar(ctx);

    {
        ImGuiIO& io = ImGui::GetIO();
        // Ignore Ctrl+S/O while a text field has focus.
        if (!io.WantTextInput && io.KeyCtrl) {
            if (ImGui::IsKeyPressed(ImGuiKey_S, /*repeat=*/false)) {
                const bool saved = SaveScene(game);
                reportStatus(saved, "Saved scene to " + DefaultScenePath().string(), "Failed to save scene.");
            }
            if (ImGui::IsKeyPressed(ImGuiKey_O, /*repeat=*/false)) {
                const bool loaded = LoadScene(game, renderer, sceneServicesSetLayout);
                reportStatus(loaded, "Loaded scene from " + DefaultScenePath().string(),
                    "Failed to load scene (missing or malformed file).");
            }
        }
    }

    const ImGuiID dockspaceId = ImGui::GetID("EditorDockSpace");
    ImGui::DockSpace(dockspaceId, ImVec2(0.0f, 0.0f), ImGuiDockNodeFlags_PassthruCentralNode);

    // Runs only until ctx.dockLayoutEnsured latches. ImGui reports DockId == 0 mid-drag, so checking
    // every frame would rebuild the default layout and cancel the user's split/undock drag.
    // Also repairs a stale imgui.ini that predates some panels.
    if (!ctx.dockLayoutEnsured) {
        bool allPanelsAccountedFor = true;
        for (const std::string& panelName : EditorPanelRegistry::Instance().AllNames()) {
            // A panel that never called Begin() yet has no window; wait and re-check next frame.
            if (ImGui::FindWindowByName(panelName.c_str()) == nullptr) {
                allPanelsAccountedFor = false;
                break;
            }
        }

        if (DefaultDockLayoutIsNeeded(dockspaceId)) {
            BuildDefaultDockLayout(dockspaceId, viewport->WorkSize);
            ctx.dockLayoutEnsured = true; // We just fixed it ourselves - trust it, never recheck.
        } else if (allPanelsAccountedFor) {
            // Every panel has a live window with a real dock (e.g. a valid saved imgui.ini): stop checking.
            ctx.dockLayoutEnsured = true;
        }
        // else: some panel has not had its first Begin() yet; re-check next frame.
    }

    // Short-lived colored status line for the last Save/Open Scene action.
    if (!ctx.sceneIoStatusMessage.empty()) {
        constexpr std::chrono::milliseconds kSceneIoStatusLifetime{ 4000 };
        if (std::chrono::steady_clock::now() - ctx.sceneIoStatusSetTime < kSceneIoStatusLifetime) {
            const ImVec4 color
                = ctx.sceneIoStatusIsError ? ImVec4(1.0f, 0.4f, 0.4f, 1.0f) : ImVec4(0.6f, 0.85f, 0.6f, 1.0f);
            ImGui::TextColored(color, "%s", ctx.sceneIoStatusMessage.c_str());
        } else {
            ctx.sceneIoStatusMessage.clear();
        }
    }

    // Shared status toast for "Project" menu actions; same pattern as the scene IO status above.
    if (!ctx.projectWorkflowStatusMessage.empty()) {
        constexpr std::chrono::milliseconds kProjectWorkflowStatusLifetime{ 4000 };
        if (std::chrono::steady_clock::now() - ctx.projectWorkflowStatusSetTime < kProjectWorkflowStatusLifetime) {
            const ImVec4 color = ctx.projectWorkflowStatusIsError ? ImVec4(1.0f, 0.4f, 0.4f, 1.0f)
                                                                   : ImVec4(0.6f, 0.85f, 0.6f, 1.0f);
            ImGui::TextColored(color, "%s", ctx.projectWorkflowStatusMessage.c_str());
        } else {
            ctx.projectWorkflowStatusMessage.clear();
        }
    }

    ImGui::End();
}

// SetWindowFocus() only takes a name, so the window is looked up a second time by name here.
bool FindAndFocusEditorWindow(const char* panelName)
{
    const ImGuiWindow* window = ImGui::FindWindowByName(panelName);
    if (window == nullptr) {
        return false;
    }
    ImGui::SetWindowFocus(panelName);
    return true;
}

SceneGameSplitResult SetSceneGameSplit(const EditorContext& ctx, bool enabled)
{
    SceneGameSplitResult result;
    if (!ctx.dockLayoutEnsured) {
        result.error = "the dock layout is not built yet";
        return result;
    }
    const ImGuiWindow* scene = ImGui::FindWindowByName("Scene");
    const ImGuiWindow* game = ImGui::FindWindowByName("Game");
    if (scene == nullptr || game == nullptr) {
        result.error = "the Scene or Game window does not exist yet";
        return result;
    }
    if (scene->DockId == 0 || game->DockId == 0) {
        result.error = "the Scene and Game windows must both be docked";
        return result;
    }

    // Game always gets a real dock node, so the default-layout rebuild never sees it as undocked.
    const ImGuiID sceneNode = scene->DockId;
    if (enabled && game->DockId == sceneNode) {
        ImGuiID gameNode = 0;
        ImGui::DockBuilderSplitNode(sceneNode, ImGuiDir_Right, 0.5f, &gameNode, nullptr);
        ImGui::DockBuilderDockWindow("Game", gameNode);
    } else if (!enabled && game->DockId != sceneNode) {
        ImGui::DockBuilderDockWindow("Game", sceneNode);
    }
    result.applied = true;
    result.split = enabled;
    return result;
}

} // namespace gte
