#include "ToonCharacterPanel.h"

#include "../ToonCharacterTypes.h"

#include <imgui.h>

#include <algorithm>
#include <array>

namespace gte {

namespace {

constexpr std::array<const char*, kToonDebugViewCount> kDebugViewLabels{
    "Off", "Class colours", "Band mask", "Shadow amount", "Outline only", "Albedo only",
    "Overlay: all fragments", "Overlay: mask test", "Overlay: camera-up normal"
};

const char* StatusText(ToonCharacterFrameStatus status) noexcept
{
    switch (status) {
    case ToonCharacterFrameStatus::Ok: return "Ok";
    case ToonCharacterFrameStatus::NotRendered: return "Not rendered (the view is not drawn)";
    case ToonCharacterFrameStatus::FeatureDisabled: return "Feature disabled";
    case ToonCharacterFrameStatus::NoToonParts: return "No toon parts in the scene";
    case ToonCharacterFrameStatus::PassDisabled: return "Surface pass is disabled";
    case ToonCharacterFrameStatus::CaptureArmed: return "Frame capture armed (normal look drawn)";
    case ToonCharacterFrameStatus::DrawRefused: return "Draw refused (see logs)";
    }
    return "Unknown";
}

void DrawStatus(const char* label, const ToonCharacterViewSnapshot& view)
{
    const ImVec4 green(0.4f, 0.9f, 0.4f, 1.0f);
    const ImVec4 orange(1.0f, 0.65f, 0.2f, 1.0f);
    ImGui::TextColored(view.status == ToonCharacterFrameStatus::Ok ? green : orange, "%s: %s", label, StatusText(view.status));
    ImGui::Text("    parts %u, claimed %u", view.candidatePartCount, view.claimedEntityCount);
}

} // namespace

bool BuildToonCharacterPanel(
    ToonCharacterSettings& edit, const ToonCharacterViewSnapshot& game, const ToonCharacterViewSnapshot& scene)
{
    bool changed = false;

    DrawStatus("Game view", game);
    DrawStatus("Scene view", scene);

    changed |= ImGui::Checkbox("Enabled", &edit.enabled);

    ImGui::SeparatorText("Light bands");
    changed |= ImGui::SliderFloat("Band threshold", &edit.bandThreshold, 0.05f, 0.95f);
    changed |= ImGui::SliderFloat("Band softness", &edit.bandSoftness, 0.001f, 0.25f, "%.3f", ImGuiSliderFlags_Logarithmic);
    changed |= ImGui::SliderFloat("Deep threshold", &edit.deepThreshold, 0.0f, 0.95f);
    changed |= ImGui::SliderFloat("Shadow strength", &edit.shadowStrength, 0.0f, 1.0f);
    changed |= ImGui::SliderFloat("Shade blend", &edit.shadeBlend, 0.0f, 1.0f);

    ImGui::SeparatorText("Rim, hair and glow");
    changed |= ImGui::SliderFloat("Rim strength", &edit.rimStrength, 0.0f, 2.0f);
    changed |= ImGui::SliderFloat("Rim width", &edit.rimWidth, 0.0f, 1.0f);
    changed |= ImGui::SliderFloat("Hair highlight", &edit.hairHighlight, 0.0f, 1.0f);
    changed |= ImGui::SliderFloat("Emissive boost", &edit.emissiveBoost, 0.0f, 16.0f);
    changed |= ImGui::Checkbox("Gem heuristic", &edit.gemHeuristic);
    changed |= ImGui::Checkbox("Gold glint", &edit.goldGlint);

    ImGui::SeparatorText("Face");
    changed |= ImGui::SliderFloat("Face flatten", &edit.faceFlatten, 0.0f, 1.0f);

    ImGui::SeparatorText("Outline");
    changed |= ImGui::SliderFloat("Width (px at 720p)", &edit.outlineWidthPx, 0.0f, 8.0f);
    changed |= ImGui::SliderFloat("Darkness", &edit.outlineDarkness, 0.0f, 1.0f);

    ImGui::SeparatorText("Debug");
    int view = std::clamp(edit.debugView, 0, kToonDebugViewCount - 1);
    if (ImGui::Combo("Debug view", &view, kDebugViewLabels.data(), static_cast<int>(kDebugViewLabels.size()))) {
        edit.debugView = std::clamp(view, 0, kToonDebugViewCount - 1);
        changed = true;
    }

    if (ImGui::Button("Reset defaults")) {
        edit = ToonCharacterSettings{};
        changed = true;
    }

    return changed;
}

} // namespace gte
