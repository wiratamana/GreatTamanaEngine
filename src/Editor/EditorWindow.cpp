#include "EditorWindow.h"

#include "EditorFrame.h"
#include "../Profiling/ScopeTimer.h"

#include <imgui.h>
#include <imgui_internal.h> // FindWindowByName(), FindWindowSettingsByID(), ImHashStr()

namespace gte {

namespace {
constexpr const char* kTabMenuId = "##EditorWindowMenu";
} // namespace

// True when ImGui will place the window inside a dock node (now, or from the saved layout, or by its default place).
bool EditorWindow::IsDockedNow() const
{
    if (const ImGuiWindow* window = ImGui::FindWindowByName(m_name.c_str())) {
        return window->DockId != 0;
    }
    if (const ImGuiWindowSettings* saved = ImGui::FindWindowSettingsByID(ImHashStr(m_name.c_str()))) {
        return saved->DockId != 0;
    }
    return m_dock != WindowDock::Floating;
}

// Right after Begin() the last item is this window's tab (docked) or title bar (floating).
void EditorWindow::DrawTabMenu()
{
    const bool docked = ImGui::IsWindowDocked();
    if (!ImGui::BeginPopupContextItem(kTabMenuId, ImGuiPopupFlags_MouseButtonRight)) {
        return;
    }
    if (ImGui::MenuItem(docked ? "Close Tab" : "Close Window")) {
        m_open = false;
    }
    ImGui::EndPopup();
}

void EditorWindow::Draw(EditorFrame& frame)
{
    if (!m_hasProfileName) {
        m_profileName = Profiling::InternName((std::string("Panel.") + m_name).c_str());
        m_hasProfileName = true;
    }
    GTE_PROFILE_SCOPE_ID(m_profileName);

    OnFrame(frame);

    if (!m_open) {
        m_visible = false;
        OnHidden(frame);
        return;
    }

    const ImGuiViewport* mainViewport = ImGui::GetMainViewport();
    if (m_dock == WindowDock::Floating) {
        ImGui::SetNextWindowPos(mainViewport->GetCenter(), ImGuiCond_FirstUseEver, ImVec2(0.5f, 0.5f));
    }
    if (m_defaultWidth > 0.0f && m_defaultHeight > 0.0f) {
        ImGui::SetNextWindowSize(ImVec2(m_defaultWidth, m_defaultHeight), ImGuiCond_FirstUseEver);
    }
    if (m_showRequested) {
        m_showRequested = false;
        ImGui::SetNextWindowFocus();
        if (!IsDockedNow()) {
            ImGui::SetNextWindowViewport(mainViewport->ID);
            ImGui::SetNextWindowPos(mainViewport->GetCenter(), ImGuiCond_Always, ImVec2(0.5f, 0.5f));
        }
    }

    // A docked tab has no X button; the tab right-click menu closes it instead.
    ImGuiWindowClass windowClass;
    windowClass.TabItemFlagsOverrideSet = ImGuiTabItemFlags_NoCloseButton;
    ImGui::SetNextWindowClass(&windowClass);

    m_visible = ImGui::Begin(m_name.c_str(), &m_open, ExtraFlags() | ImGuiWindowFlags_NoCollapse);
    DrawTabMenu();
    if (m_visible) {
        OnGui(frame);
    }
    ImGui::End();
    if (!m_visible) {
        OnHidden(frame);
    }
}

} // namespace gte
