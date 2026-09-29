#include "CreateAssetWindow.h"

#include "../Core/EditorCapabilities.h"

#include <imgui.h>

#include <cstring>

namespace gte {

namespace {
const char* TitleForKind(AssetScaffoldKind kind)
{
    switch (kind) {
    case AssetScaffoldKind::RenderPass: return "Create New Render Pass";
    case AssetScaffoldKind::ComputeShader: return "Create New Compute Shader";
    case AssetScaffoldKind::ShaderPair: return "Create New Vertex/Fragment Shader Pair";
    }
    return "Create New Asset"; // unreachable - silences a "not all control paths return a value" warning.
}
} // namespace

void CreateAssetWindow::Open()
{
    std::memset(m_nameBuffer, 0, sizeof(m_nameBuffer));
    m_errorMessage.clear();
}

void CreateAssetWindow::Build(EditorContext& ctx, IAssetScaffoldingCapability* capability)
{
    if (!ctx.createAssetWindowOpen) {
        m_wasOpenLastFrame = false;
        return;
    }
    if (!m_wasOpenLastFrame) {
        Open();
        m_wasOpenLastFrame = true;
    }

    const AssetScaffoldKind kind = ctx.createAssetWindowPendingKind;

    ImGui::SetNextWindowSize(ImVec2(460.0f, 160.0f), ImGuiCond_FirstUseEver);
    constexpr ImGuiWindowFlags flags = ImGuiWindowFlags_NoDocking | ImGuiWindowFlags_NoCollapse;
    if (ImGui::Begin(TitleForKind(kind), &ctx.createAssetWindowOpen, flags)) {
        ImGui::InputText("Name", m_nameBuffer, sizeof(m_nameBuffer));
        if (!m_errorMessage.empty()) {
            ImGui::TextColored(ImVec4(1.0f, 0.4f, 0.4f, 1.0f), "%s", m_errorMessage.c_str());
        }
        ImGui::Separator();
        if (ImGui::Button("Cancel")) {
            ctx.createAssetWindowOpen = false;
        }
        ImGui::SameLine();
        if (ImGui::Button("Create") && capability != nullptr) {
            const IAssetScaffoldingCapability::ScaffoldOutcome outcome =
                capability->CreateAssetScaffold(kind, m_nameBuffer);
            if (outcome.success) {
                ctx.createAssetWindowOpen = false;
                // The reminder is the single most important thing the user
                // needs to read after this action (PHASE0_MASTER_STRATEGY.md's
                // own restated Step 2 caveat) - surfaced prominently via the
                // SAME shared status-toast field every "Project" menu action
                // already uses, not just a plain "created" message.
                std::string message = "Created " + std::to_string(outcome.createdFiles.size()) + " file(s).";
                if (!outcome.reminderMessage.empty()) {
                    message += " " + outcome.reminderMessage;
                }
                ctx.projectWorkflowStatusMessage = message;
                ctx.projectWorkflowStatusIsError = false;
                ctx.projectWorkflowStatusSetTime = std::chrono::steady_clock::now();
            } else {
                m_errorMessage = outcome.errorMessage; // window stays open, user can fix and retry.
            }
        }
    }
    ImGui::End();
}

} // namespace gte
