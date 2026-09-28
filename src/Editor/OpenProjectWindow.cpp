#include "OpenProjectWindow.h"

#include "../Core/EditorCapabilities.h"

#include <imgui.h>

namespace gte {

void OpenProjectWindow::Open(IProjectLifecycleCapability* capability)
{
    m_rows.clear();
    m_selectedIndex = -1;
    m_errorMessage.clear();
    if (capability == nullptr) return;
    for (const auto& entry : capability->ListProjectAssemblies()) {
        m_rows.push_back({ entry.name, entry.tierName });
    }
}

namespace {
ImVec4 ColorForTierName(const std::string& tierName)
{
    if (tierName == "NotAProject") return ImVec4(0.6f, 0.6f, 0.6f, 1.0f);   // gray
    if (tierName == "NotBuildable") return ImVec4(0.9f, 0.8f, 0.2f, 1.0f);  // yellow
    if (tierName == "NotCompiled") return ImVec4(0.95f, 0.55f, 0.15f, 1.0f); // orange
    if (tierName == "Compiled") return ImVec4(0.35f, 0.85f, 0.35f, 1.0f);   // green
    if (tierName == "AlreadyLoaded") return ImVec4(0.35f, 0.65f, 0.95f, 1.0f); // blue
    return ImVec4(1.0f, 1.0f, 1.0f, 1.0f);
}
} // namespace

void OpenProjectWindow::Build(EditorContext& ctx, IProjectLifecycleCapability* capability)
{
    if (!ctx.openProjectWindowOpen) {
        m_wasOpenLastFrame = false;
        return;
    }
    if (!m_wasOpenLastFrame) {
        Open(capability);
        m_wasOpenLastFrame = true;
    }

    ImGui::SetNextWindowSize(ImVec2(480.0f, 320.0f), ImGuiCond_FirstUseEver);
    constexpr ImGuiWindowFlags flags = ImGuiWindowFlags_NoDocking | ImGuiWindowFlags_NoCollapse;
    if (ImGui::Begin("Open Project", &ctx.openProjectWindowOpen, flags)) {
        ImGui::BeginChild("OpenProjectRows", ImVec2(0, -40.0f), true);
        for (int i = 0; i < static_cast<int>(m_rows.size()); ++i) {
            const Row& row = m_rows[i];
            const bool isSelectable = row.tierName != "NotAProject";
            ImGui::PushStyleColor(ImGuiCol_Text, ColorForTierName(row.tierName));
            const std::string label = row.name + "  [" + row.tierName + "]";
            if (ImGui::Selectable(label.c_str(), m_selectedIndex == i,
                    isSelectable ? 0 : ImGuiSelectableFlags_Disabled)) {
                m_selectedIndex = i;
            }
            ImGui::PopStyleColor();
        }
        ImGui::EndChild();

        if (!m_errorMessage.empty()) {
            ImGui::TextColored(ImVec4(1.0f, 0.4f, 0.4f, 1.0f), "%s", m_errorMessage.c_str());
        }
        if (ImGui::Button("Cancel")) {
            ctx.openProjectWindowOpen = false;
        }
        ImGui::SameLine();
        const bool canOpen = m_selectedIndex >= 0
            && m_rows[static_cast<std::size_t>(m_selectedIndex)].tierName != "NotAProject";
        ImGui::BeginDisabled(!canOpen);
        if (ImGui::Button("Open") && capability != nullptr) {
            const IProjectLifecycleCapability::OpenProjectOutcome outcome =
                capability->OpenProjectAssemblyOnMainThread(m_rows[static_cast<std::size_t>(m_selectedIndex)].name);
            if (outcome.success) {
                ctx.openProjectWindowOpen = false;
                ctx.projectWorkflowStatusMessage = outcome.statusMessage;
                ctx.projectWorkflowStatusIsError = false;
                ctx.projectWorkflowStatusSetTime = std::chrono::steady_clock::now();
            } else {
                m_errorMessage = outcome.errorMessage;
            }
        }
        ImGui::EndDisabled();
    }
    ImGui::End();
}

} // namespace gte
