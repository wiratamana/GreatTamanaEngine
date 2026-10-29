#include "GpuDrivenScenePanel.h"

#include "GpuDrivenScenePanelText.h"
#include "../GpuDrivenSceneTypes.h"

#include <imgui.h>

#include <string>

namespace gte {

namespace {

const ImVec4 kWarnColor{ 1.0f, 0.65f, 0.2f, 1.0f };
const ImVec4 kGoodColor{ 0.4f, 0.9f, 0.4f, 1.0f };

void DrawLine(const std::string& line)
{
    if (!line.empty()) {
        ImGui::TextUnformatted(line.c_str());
    }
}

void DrawViewInfo(const char* label, const GpuDrivenSceneSettings& s, const GpuDrivenSceneViewSnapshot& v, bool secondary,
    const CullTimeSplit& time)
{
    ImGui::PushID(label);
    ImGui::SeparatorText(label);
    if (!v.rendered) {
        ImGui::TextDisabled("Not rendered");
        ImGui::PopID();
        return;
    }
    DrawLine(CullTimeLine(time));
    DrawLine(CullCameraLine(s, v));
    DrawLine(CpuPathLine(v));
    DrawLine(CpuCullStateLine(v));
    DrawLine(GpuPathLine(v));
    DrawLine(SkippedPartsLine(v));
    DrawLine(CpuReasonsLine(v));
    if (s.mode == GpuCullMode::HiZ) {
        DrawLine(HizLine(v, secondary));
    }
    DrawLine(ReasonsLine(v));
    const std::string inert = InertLine(s, v);
    if (!inert.empty()) {
        ImGui::TextColored(kWarnColor, "%s", inert.c_str());
    }
    const std::string_view noGpu = NoGpuPathLine(s, v);
    if (!noGpu.empty()) {
        ImGui::TextColored(kWarnColor, "%.*s", static_cast<int>(noGpu.size()), noGpu.data());
    }
    ImGui::Text("Buffer memory: %.1f MB", static_cast<double>(v.gpu.ledgerBytes) / (1024.0 * 1024.0));
    if (!v.gpu.refusal.empty()) {
        ImGui::TextColored(kWarnColor, "Not drawing on the GPU: %s", v.gpu.refusal.c_str());
    }
    ImGui::PopID();
}

bool DrawModeCombo(GpuCullMode& current)
{
    bool changed = false;
    const std::string currentName(fx::EnumName(kCullModeTable, current));
    if (ImGui::BeginCombo("Culling mode", currentName.c_str())) {
        for (const auto& entry : kCullModeTable) {
            const std::string name(entry.name);
            if (ImGui::Selectable(name.c_str(), entry.value == current)) {
                current = entry.value;
                changed = true;
            }
        }
        ImGui::EndCombo();
    }
    return changed;
}

// Combo over the debug view table. Returns true when the selection changed.
bool DrawDebugViewCombo(GpuDrivenSceneDebugView& current)
{
    bool changed = false;
    const std::string currentName(fx::EnumName(kGpuDrivenSceneDebugViewTable, current));
    if (ImGui::BeginCombo("Debug view", currentName.c_str())) {
        for (const auto& entry : kGpuDrivenSceneDebugViewTable) {
            const std::string name(entry.name);
            if (ImGui::Selectable(name.c_str(), entry.value == current)) {
                current = entry.value;
                changed = true;
            }
        }
        ImGui::EndCombo();
    }
    return changed;
}

} // namespace

bool BuildGpuDrivenScenePanel(GpuDrivenSceneSettings& edit, bool supported, const GpuDrivenSceneViewSnapshot& game,
    const GpuDrivenSceneViewSnapshot& scene, const CullTimeSplits& times)
{
    bool changed = false;

    changed |= DrawModeCombo(edit.mode);
    const std::string_view modeLine = CullModeLine(edit.mode);
    ImGui::TextDisabled("%.*s", static_cast<int>(modeLine.size()), modeLine.data());
    const std::string_view help = CullHelpLine();
    ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
    ImGui::TextWrapped("%.*s", static_cast<int>(help.size()), help.data());
    ImGui::PopStyleColor();

    DrawViewInfo("Game view", edit, game, false, times.primary);
    DrawViewInfo("Scene view", edit, scene, true, times.secondary);

    if (ImGui::CollapsingHeader("Advanced")) {
        if (supported) {
            ImGui::TextColored(kGoodColor, "Device supports the cluster draw");
        } else {
            ImGui::TextColored(kWarnColor, "Device cannot run the cluster draw; the CPU draw is used");
        }
        changed |= ImGui::Checkbox("GPU path enabled", &edit.enabled);
        changed |= ImGui::Checkbox("Back-face cone culling", &edit.cone);
        if (edit.cone) {
            const std::string_view warning = ConeWarningLine();
            ImGui::PushStyleColor(ImGuiCol_Text, kWarnColor);
            ImGui::TextWrapped("%.*s", static_cast<int>(warning.size()), warning.data());
            ImGui::PopStyleColor();
        }
        changed |= ImGui::Checkbox("Count visible clusters and reasons (extra passes)", &edit.collectStats);
        changed |= DrawDebugViewCombo(edit.debugView);
    }

    if (ImGui::Button("Reset to defaults")) {
        edit = GpuDrivenSceneSettings{};
        changed = true;
    }
    return changed;
}

} // namespace gte
