#include "NewProjectWindow.h"

#include "../Core/EditorCapabilities.h"

#include <imgui.h>

#include <cstring>

namespace gte {

void NewProjectWindow::Open()
{
    std::memset(m_nameBuffer, 0, sizeof(m_nameBuffer));
    m_errorMessage.clear();
}

void NewProjectWindow::Build(EditorContext& ctx, IProjectLifecycleCapability* capability)
{
    if (!ctx.newProjectWindowOpen) {
        m_wasOpenLastFrame = false;
        return;
    }
    if (!m_wasOpenLastFrame) {
        Open();
        m_wasOpenLastFrame = true;
    }

    ImGui::SetNextWindowSize(ImVec2(420.0f, 160.0f), ImGuiCond_FirstUseEver);
    constexpr ImGuiWindowFlags flags = ImGuiWindowFlags_NoDocking | ImGuiWindowFlags_NoCollapse;
    if (ImGui::Begin("Create New Project", &ctx.newProjectWindowOpen, flags)) {
        ImGui::InputText("Project Name", m_nameBuffer, sizeof(m_nameBuffer));
        if (!m_errorMessage.empty()) {
            ImGui::TextColored(ImVec4(1.0f, 0.4f, 0.4f, 1.0f), "%s", m_errorMessage.c_str());
        }
        ImGui::Separator();
        if (ImGui::Button("Cancel")) {
            ctx.newProjectWindowOpen = false;
        }
        ImGui::SameLine();
        if (ImGui::Button("Create") && capability != nullptr) {
            const IProjectLifecycleCapability::CreateProjectOutcome outcome =
                capability->CreateNewProjectAssembly(m_nameBuffer);
            if (outcome.success) {
                ctx.newProjectWindowOpen = false;
                ctx.projectWorkflowStatusMessage = "Created project at " + outcome.createdSourceDirectory;
                ctx.projectWorkflowStatusIsError = false;
                ctx.projectWorkflowStatusSetTime = std::chrono::steady_clock::now();
            } else {
                // Window stays open, user can fix and retry - never
                // auto-closes on failure (this campaign's own explicit
                // Definition of Done item).
                m_errorMessage = outcome.errorMessage;
            }
        }
    }
    ImGui::End();
}

} // namespace gte
