#include "AtmospherePanel.h"

#include "../../../Editor/EditorContext.h"
#include "../AtmosphereTypes.h"

#include <imgui.h>

namespace gte {

bool BuildAtmospherePanel(EditorContext& /*ctx*/, AtmosphereSettings& edit, const AtmosphereAerialSnapshot& effective)
{
    bool changed = false;
    ImGui::Begin("Atmosphere");

    ImGui::TextDisabled("Physical sky atmosphere controls.");

    ImGui::SeparatorText("Planet");
    changed |= ImGui::ColorEdit3("Ground Albedo", &edit.groundAlbedo.x);
    changed |= ImGui::DragFloat("Ground Radius (km)", &edit.planetRadiusKm, 1.0f, 1.0f, 50000.0f);

    ImGui::SeparatorText("Atmosphere");
    changed |= ImGui::DragFloat("Atmosphere Height (km)", &edit.atmosphereThicknessKm, 0.5f, 1.0f, 500.0f);
    changed |= ImGui::DragFloat("Multi-Scattering", &edit.multiScatteringStrength, 0.01f, 0.0f, 10.0f);

    ImGui::SeparatorText("Rayleigh");
    changed |= ImGui::ColorEdit3("Scattering##Rayleigh", &edit.rayleighScattering.x);
    changed |= ImGui::DragFloat("Exponential Distribution (km)##Rayleigh", &edit.rayleighScaleHeightKm, 0.05f, 0.01f, 100.0f);

    ImGui::SeparatorText("Mie");
    changed |= ImGui::ColorEdit3("Scattering##Mie", &edit.mieScattering.x);
    changed |= ImGui::ColorEdit3("Absorption##Mie", &edit.mieAbsorption.x);
    changed |= ImGui::SliderFloat("Anisotropy##Mie", &edit.miePhaseG, -0.99f, 0.99f);
    changed |= ImGui::DragFloat("Exponential Distribution (km)##Mie", &edit.mieScaleHeightKm, 0.05f, 0.01f, 100.0f);

    ImGui::SeparatorText("Absorption");
    changed |= ImGui::ColorEdit3("Absorption##Ozone", &edit.ozoneAbsorption.x);
    changed |= ImGui::DragFloat("Tent Distribution Center (km)", &edit.ozoneTentCenterKm, 0.5f, 0.0f, 100.0f);
    changed |= ImGui::DragFloat("Tent Distribution Half-Width (km)", &edit.ozoneTentHalfWidthKm, 0.5f, 0.0f, 100.0f);

    ImGui::SeparatorText("Art Direction");

    changed |= ImGui::DragFloat("Aerial Perspective Strength", &edit.aerialPerspectiveStrength, 0.01f, 0.0f, 2.0f);
    changed |= ImGui::Checkbox("Aerial Auto Scale", &edit.autoScale);
    ImGui::TextDisabled("Auto scale fits the haze range and exaggeration to the visible depth.");

    // With auto scale on, the two sliders are fallbacks; show what the Game view really uses.
    ImGui::BeginDisabled(edit.autoScale);
    changed |= ImGui::DragFloat("Aerial Max Distance (km)", &edit.aerialPerspectiveMaxDistanceKm, 0.01f, 0.01f, 50.0f);
    changed |= ImGui::DragFloat(
        "Aerial Scattering Exaggeration", &edit.aerialPerspectiveScatteringExaggeration, 0.1f, 0.1f, 50.0f);
    ImGui::EndDisabled();
    if (edit.autoScale) {
        ImGui::TextDisabled("In use: %.2f km, %.2fx, air shadows %.0f%%", effective.maxDistanceKm,
            effective.exaggeration, effective.shadowBlend * 100.0f);
    }

    changed |= ImGui::DragFloat("Aerial Depth Exponent", &edit.aerialPerspectiveDepthExponent, 0.05f, 1.0f, 4.0f);
    changed |= ImGui::SliderInt("Aerial Samples Per Slice", &edit.aerialPerspectiveSamplesPerSlice, 1, 16);
    changed |= ImGui::DragFloat("Sky Intensity", &edit.skyIntensity, 0.1f, 0.0f, 50.0f);
    changed |= ImGui::Checkbox("Scene Light Follows Atmosphere", &edit.sceneLightFollowsAtmosphere);
    ImGui::TextDisabled("Sun and sky light of surfaces and light shafts fade and redden with the sun height.");

    ImGui::TextDisabled(
        "Sun direction/color/illuminance are controlled by a DirectionalLight ECS entity - see \"Hierarchy\" > "
        "right-click > \"Create Directional Light\", then edit its Transform/DirectionalLight in \"Inspector\". "
        "Color and illuminance describe the sun straight overhead.");

    ImGui::End();
    return changed;
}

} // namespace gte
