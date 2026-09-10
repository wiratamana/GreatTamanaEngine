#include "AtmospherePanel.h"

#include "../EditorContext.h"
#include "../../Renderer/Atmosphere/AtmosphereTypes.h"

#include <imgui.h>

namespace gte {

void BuildAtmospherePanel(EditorContext& /*ctx*/, AtmosphereSettings& settings)
{
    ImGui::Begin("Atmosphere");

    ImGui::TextDisabled("Tunable, non-spatial atmosphere knobs (see AGENTS.md).");
    ImGui::Separator();

    ImGui::ColorEdit3("Ground Albedo Tint", &settings.groundAlbedoTint.x);
    ImGui::DragFloat("Aerial Perspective Strength", &settings.aerialPerspectiveStrength, 0.01f, 0.0f, 2.0f);
    ImGui::DragFloat("Sky Exposure", &settings.skyExposure, 0.1f, 0.0f, 50.0f);

    ImGui::Separator();
    ImGui::TextDisabled(
        "Sun direction/color/illuminance are controlled by a DirectionalLight ECS entity - see \"Hierarchy\" > "
        "right-click > \"Create Directional Light\", then edit its Transform/DirectionalLight in \"Inspector\".");

    ImGui::End();
}

} // namespace gte
