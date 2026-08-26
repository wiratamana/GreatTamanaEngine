#include "DockLayout.h"

#include "EditorContext.h"

// imgui_internal.h is needed (only here, among the Editor's panel/layout
// files) for the DockBuilder* API used to lay out the default Unity-style
// panel arrangement below - it is not part of ImGui's stable public API,
// but building a default dock layout programmatically has no supported
// alternative in Dear ImGui today.
#include <imgui.h>
#include <imgui_internal.h>

namespace gte {

namespace {

// Every panel name the one-shot default-layout logic below cares about -
// factored out so DefaultDockLayoutIsNeeded() and the one-shot check inside
// BuildDockspaceAndMenuBar() can never silently drift apart from each other
// (or from BuildDefaultDockLayout()'s own DockBuilderDockWindow() calls) as
// panels are added/removed. "Project" (Panels/ProjectPanel.h) is only
// listed when GTE_ENABLE_PROJECT_PANEL is ON - a build with the Project
// panel switched off never expects a "Project" window to exist at all, so
// it must not be part of what this one-shot logic waits for/rebuilds
// around.
constexpr const char* kAllPanelNames[] = {
    "Hierarchy",
    "Inspector",
    "Scene",
    "Game",
    "Memory",
    "Profiler",
    "Render Graph",
#if GTE_ENABLE_PROJECT_PANEL
    "Project",
#endif
};

// True if the dockspace node itself doesn't exist yet, OR if any of our
// four panels currently exists as a window but has never actually been
// docked (DockId == 0) - see BuildDockspaceAndMenuBar()'s comment for why
// this is checked only until latched, never on every frame forever.
bool DefaultDockLayoutIsNeeded(ImGuiID dockspaceId)
{
    if (ImGui::DockBuilderGetNode(dockspaceId) == nullptr) {
        return true;
    }
    for (const char* panelName : kAllPanelNames) {
        const ImGuiWindow* window = ImGui::FindWindowByName(panelName);
        if (window != nullptr && window->DockId == 0) {
            return true;
        }
    }
    return false;
}

// Unity-style default arrangement, built exactly once (see caller):
//   +----------+-------------------------------+----------+
//   |          |         (menu bar)             |          |
//   | Hierarchy|      Scene | Game (tabs)        | Inspector|
//   |  (left)  |         (center)                |  (right) |
//   |          +---------------------------------+          |
//   |          |             Memory (bottom)      |          |
//   +----------+-------------------------------+----------+
// "Scene" and "Game" are docked into the SAME center node (as tabs) - the
// user can drag the "Scene" tab out to split it away from "Game" at any
// time afterwards (see BuildDockspaceAndMenuBar()'s comment). "Memory"
// (Unity-Memory-Profiler-style GPU memory panel, see Panels/MemoryPanel.cpp)
// gets its own full-width strip along the bottom, mirroring where Unity's
// own Profiler window normally lives - split off the dockspace BEFORE the
// left/right panels below so it spans the complete width rather than just
// the center column.
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
    // "Profiler" (Phase 7 - Panels/ProfilerPanel.h) is docked unconditionally
    // alongside "Memory", exactly like "Memory" itself - it has no
    // GTE_ENABLE_PROJECT_PANEL dependency at all.
    ImGui::DockBuilderDockWindow("Profiler", bottom);
    // "Render Graph" (Phase 8 - Panels/RenderGraphPanel.h) - same "docked
    // unconditionally alongside Memory/Profiler" treatment; it has no
    // GTE_ENABLE_PROJECT_PANEL dependency either.
    ImGui::DockBuilderDockWindow("Render Graph", bottom);
#if GTE_ENABLE_PROJECT_PANEL
    // Tabbed alongside "Memory" - Unity's own default layout also puts
    // "Project" (and "Console") along the bottom.
    ImGui::DockBuilderDockWindow("Project", bottom);
#endif

    ImGui::DockBuilderFinish(dockspaceId);
}
} // namespace

void BuildDockspaceAndMenuBar(EditorContext& ctx)
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

    if (ImGui::BeginMenuBar()) {
        if (ImGui::BeginMenu("File")) {
            // Simple example of a menu item that exits the application
            // programmatically: sets ctx.exitRequested, which
            // ImGuiEditorLayer::WantsExit() checks once per frame, rather
            // than calling exit()/SDL_Quit() directly here, so shutdown
            // still goes through Application's normal RAII teardown.
            if (ImGui::MenuItem("Exit")) {
                ctx.exitRequested = true;
            }
            ImGui::EndMenu();
        }
        ImGui::EndMenuBar();
    }

    const ImGuiID dockspaceId = ImGui::GetID("EditorDockSpace");
    ImGui::DockSpace(dockspaceId, ImVec2(0.0f, 0.0f), ImGuiDockNodeFlags_PassthruCentralNode);

    // See below for why this whole block only ever runs until
    // ctx.dockLayoutEnsured latches true, never again after that - this is
    // what lets the user freely drag/split/undock any panel afterwards
    // without ever fighting this code.
    //
    // This one-shot-ness is not just an optimization - it's required for
    // correctness. Dragging a tab to split/detach it (e.g. pulling "Scene"
    // away from "Game") makes Dear ImGui briefly report that window's
    // DockId as 0 WHILE THE DRAG IS STILL IN PROGRESS, before the drop
    // target is chosen - a naive "rebuild the default layout whenever any
    // of our panels has DockId == 0" check run every frame would catch
    // exactly that transient mid-drag state and immediately call
    // DockBuilderRemoveNode() + redock everything back to the default
    // layout, cancelling the user's drag before they can ever complete it.
    // That made splitting/undocking ANY of these panels look completely
    // impossible. Checking only until the layout is confirmed once (then
    // never again) fixes this while still auto-repairing a stale
    // imgui.ini saved by an older build of this engine, from before these
    // panels existed (which would otherwise leave them as permanent tiny
    // undocked floating windows).
    if (!ctx.dockLayoutEnsured) {
        bool allPanelsAccountedFor = true;
        for (const char* panelName : kAllPanelNames) {
            // A panel that has never called Begin() yet this session (e.g.
            // this is the very first frame ever, before this same
            // BuildUI() call reaches BuildHierarchyPanel()/etc.) doesn't
            // exist as an ImGuiWindow yet - we can't yet be sure whether
            // it'll end up docked or not, so don't latch "ensured" on this
            // frame; just wait and check again next frame instead.
            if (ImGui::FindWindowByName(panelName) == nullptr) {
                allPanelsAccountedFor = false;
                break;
            }
        }

        if (DefaultDockLayoutIsNeeded(dockspaceId)) {
            BuildDefaultDockLayout(dockspaceId, viewport->WorkSize);
            ctx.dockLayoutEnsured = true; // We just fixed it ourselves - trust it, never recheck.
        } else if (allPanelsAccountedFor) {
            // Nothing needed fixing AND we've actually observed a real,
            // live window for all four panels with a real dock already
            // (e.g. a valid saved imgui.ini) - safe to stop checking
            // forever.
            ctx.dockLayoutEnsured = true;
        }
        // else: not enough information yet (some panel hasn't had its
        // first Begin() this session) - leave ctx.dockLayoutEnsured false
        // and re-evaluate next frame.
    }

    ImGui::End();
}

} // namespace gte
