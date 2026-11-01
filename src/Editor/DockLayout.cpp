#include "DockLayout.h"
#include "EditorContext.h"
#include "EditorWindow.h"
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

#include <algorithm>
#include <chrono>
#include <string>
#include <vector>

namespace gte {

namespace {

ImGuiID DockNodeFor(WindowDock dock, ImGuiID left, ImGuiID center, ImGuiID right, ImGuiID bottom)
{
    switch (dock) {
    case WindowDock::Left: return left;
    case WindowDock::Center: return center;
    case WindowDock::Right: return right;
    case WindowDock::Bottom: return bottom;
    case WindowDock::Floating: break;
    }
    return 0;
}

// Default layout, built only on a first run: Hierarchy left, Inspector right, Scene/Game center tabs, wide bottom strip.
// The bottom strip is split first so it spans the full width. Each window docks where its DefaultDock() says.
void BuildDefaultDockLayout(ImGuiID dockspaceId, ImVec2 size)
{
    ImGui::DockBuilderRemoveNode(dockspaceId);
    ImGui::DockBuilderAddNode(dockspaceId, ImGuiDockNodeFlags_DockSpace);
    ImGui::DockBuilderSetNodeSize(dockspaceId, size);

    ImGuiID center = dockspaceId;
    const ImGuiID bottom = ImGui::DockBuilderSplitNode(center, ImGuiDir_Down, 0.25f, nullptr, &center);
    const ImGuiID left = ImGui::DockBuilderSplitNode(center, ImGuiDir_Left, 0.20f, nullptr, &center);
    const ImGuiID right = ImGui::DockBuilderSplitNode(center, ImGuiDir_Right, 0.28f, nullptr, &center);

    for (const EditorPanelRegistry::WindowEntry& entry : EditorPanelRegistry::Instance().Windows()) {
        const ImGuiID node = DockNodeFor(entry.window->DefaultDock(), left, center, right, bottom);
        if (node != 0) {
            ImGui::DockBuilderDockWindow(entry.name.c_str(), node);
        }
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

    // Outcome of a Hi-Z demo menu action the host ran last frame.
    if (ctx.hizDemoMenuResult.fresh) {
        reportStatus(!ctx.hizDemoMenuResult.isError, ctx.hizDemoMenuResult.message, ctx.hizDemoMenuResult.message);
        ctx.hizDemoMenuResult.fresh = false;
    }

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
                const OpenSponzaResult opened = OpenSponza(game, renderer, sceneServicesSetLayout);
                reportStatus(opened.success, opened.message, opened.message);
                if (opened.success) {
                    ctx.hizDemoMenuAction = HizDemoMenuAction::SceneReplaced;
                }
            }
            // The host runs these on the main thread next frame and reports the outcome back.
            if (ImGui::MenuItem("Open Hi-Z Demo Scene")) {
                ctx.hizDemoMenuAction = HizDemoMenuAction::Open;
            }
            if (ImGui::MenuItem("Close Hi-Z Demo", nullptr, false, ctx.hizDemo.active)) {
                ctx.hizDemoMenuAction = HizDemoMenuAction::Close;
            }
            ImGui::Separator();
            // Sets a flag instead of calling exit() so shutdown goes through the normal RAII teardown.
            if (ImGui::MenuItem("Exit")) {
                ctx.exitRequested = true;
            }
            ImGui::EndMenu();
        }
        if (ImGui::BeginMenu("Window")) {
            // One flat list of every registered window, sorted by name. A click opens it, raises it and centers it when it floats.
            std::vector<EditorWindow*> windows;
            for (const EditorPanelRegistry::WindowEntry& entry : EditorPanelRegistry::Instance().Windows()) {
                windows.push_back(entry.window);
            }
            std::sort(windows.begin(), windows.end(),
                [](const EditorWindow* a, const EditorWindow* b) { return WindowNameLess(a->Name(), b->Name()); });
            for (EditorWindow* window : windows) {
                if (ImGui::MenuItem(window->Name(), nullptr, window->IsOpen())) {
                    window->Show();
                }
            }
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
    // Asked before DockSpace(): DockSpace() creates the node itself, so afterwards it always exists.
    const bool hadSavedLayout = ImGui::DockBuilderGetNode(dockspaceId) != nullptr;
    ImGui::DockSpace(dockspaceId, ImVec2(0.0f, 0.0f), ImGuiDockNodeFlags_PassthruCentralNode);

    // A saved layout is never rebuilt. Only a first run (no dock space in imgui.ini) gets the default layout.
    if (!ctx.dockLayoutEnsured) {
        ctx.dockLayoutEnsured = true;
        if (!hadSavedLayout) {
            BuildDefaultDockLayout(dockspaceId, viewport->WorkSize);
        }
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

    // Game always keeps a dock node of its own.
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
