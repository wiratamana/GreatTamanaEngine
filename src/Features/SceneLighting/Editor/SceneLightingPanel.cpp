#include "SceneLightingPanel.h"

#include "../SceneLightingTypes.h"

#include <imgui.h>

#include <algorithm>
#include <array>

namespace gte {

namespace {

constexpr std::array<const char*, kMaterialDebugViewCount> kDebugViewLabels{ "Off", "Shading normal", "Roughness",
    "Metallic", "Occlusion", "Tangent", "Handedness", "Facing", "Base color" };

bool EditColor(const char* label, float& r, float& g, float& b)
{
    float rgb[3] = { r, g, b };
    if (!ImGui::ColorEdit3(label, rgb, ImGuiColorEditFlags_Float | ImGuiColorEditFlags_HDR)) {
        return false;
    }
    r = rgb[0];
    g = rgb[1];
    b = rgb[2];
    return true;
}

} // namespace

bool BuildSceneLightingPanel(SceneLightingSettings& edit)
{
    bool changed = false;

    ImGui::SeparatorText("Debug");
    int view = static_cast<int>(edit.materialDebugView);
    if (ImGui::Combo("Material debug view", &view, kDebugViewLabels.data(), static_cast<int>(kDebugViewLabels.size()))) {
        edit.materialDebugView = static_cast<MaterialDebugView>(std::clamp(view, 0, kMaterialDebugViewCount - 1));
        changed = true;
    }

    ImGui::SeparatorText("Ambient");
    changed |= EditColor("Sky ambient", edit.skyAmbientR, edit.skyAmbientG, edit.skyAmbientB);
    changed |= EditColor("Ground ambient", edit.groundAmbientR, edit.groundAmbientG, edit.groundAmbientB);
    changed |= ImGui::SliderFloat("Ambient intensity", &edit.ambientIntensity, 0.0f, 8.0f);

    if (ImGui::Button("Reset to defaults")) {
        edit = SceneLightingSettings{};
        changed = true;
    }

    return changed;
}

} // namespace gte
