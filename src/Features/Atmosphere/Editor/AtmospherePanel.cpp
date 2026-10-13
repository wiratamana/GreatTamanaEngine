#include "AtmospherePanel.h"

#include "../../../Editor/EditorContext.h"
#include "../AtmosphereParameters.h"
#include "../AtmosphereTypes.h"

#include <imgui.h>

namespace gte {

void BuildAtmospherePanel(EditorContext& /*ctx*/, AtmosphereSettings& settings, Renderer& renderer,
    std::optional<AtmosphereTransmittanceLutValidationResult>& lastValidationResult,
    const rg::RenderGraph& renderGraph,
    std::optional<AtmosphereAerialPerspectiveSkyPurityResult>& lastSkyPurityResult)
{
    ImGui::Begin("Atmosphere");

    ImGui::TextDisabled("Physical Sky Atmosphere controls (see AGENTS.md).");

    // Mirrors Unreal Engine's Sky Atmosphere component layout.
    ImGui::SeparatorText("Planet");
    ImGui::ColorEdit3("Ground Albedo", &settings.groundAlbedo.x);
    ImGui::DragFloat("Ground Radius (km)", &settings.planetRadiusKm, 1.0f, 1.0f, 50000.0f);

    ImGui::SeparatorText("Atmosphere");
    ImGui::DragFloat("Atmosphere Height (km)", &settings.atmosphereThicknessKm, 0.5f, 1.0f, 500.0f);
    ImGui::DragFloat("Multi-Scattering", &settings.multiScatteringStrength, 0.01f, 0.0f, 10.0f);

    ImGui::SeparatorText("Rayleigh");
    ImGui::ColorEdit3("Scattering##Rayleigh", &settings.rayleighScattering.x);
    ImGui::DragFloat("Exponential Distribution (km)##Rayleigh", &settings.rayleighScaleHeightKm, 0.05f, 0.01f, 100.0f);

    ImGui::SeparatorText("Mie");
    ImGui::ColorEdit3("Scattering##Mie", &settings.mieScattering.x);
    ImGui::ColorEdit3("Absorption##Mie", &settings.mieAbsorption.x);
    ImGui::SliderFloat("Anisotropy##Mie", &settings.miePhaseG, -0.99f, 0.99f);
    ImGui::DragFloat("Exponential Distribution (km)##Mie", &settings.mieScaleHeightKm, 0.05f, 0.01f, 100.0f);

    ImGui::SeparatorText("Absorption");
    ImGui::ColorEdit3("Absorption##Ozone", &settings.ozoneAbsorption.x);
    ImGui::DragFloat("Tent Distribution Center (km)", &settings.ozoneTentCenterKm, 0.5f, 0.0f, 100.0f);
    ImGui::DragFloat("Tent Distribution Half-Width (km)", &settings.ozoneTentHalfWidthKm, 0.5f, 0.0f, 100.0f);

    ImGui::SeparatorText("Art Direction");

    ImGui::DragFloat("Aerial Perspective Strength", &settings.aerialPerspectiveStrength, 0.01f, 0.0f, 2.0f);
    ImGui::DragFloat("Aerial Max Distance (km)", &settings.aerialPerspectiveMaxDistanceKm, 0.01f, 0.01f, 50.0f);
    ImGui::DragFloat("Aerial Depth Exponent", &settings.aerialPerspectiveDepthExponent, 0.05f, 1.0f, 4.0f);
    ImGui::SliderInt("Aerial Samples Per Slice", &settings.aerialPerspectiveSamplesPerSlice, 1, 8);
    ImGui::DragFloat(
        "Aerial Scattering Exaggeration", &settings.aerialPerspectiveScatteringExaggeration, 0.1f, 0.1f, 50.0f);
    ImGui::DragFloat("Sky Exposure", &settings.skyExposure, 0.1f, 0.0f, 50.0f);

    ImGui::TextDisabled(
        "Sun direction/color/illuminance are controlled by a DirectionalLight ECS entity - see \"Hierarchy\" > "
        "right-click > \"Create Directional Light\", then edit its Transform/DirectionalLight in \"Inspector\".");

    // GPU-vs-CPU-oracle numeric parity check.
    if (ImGui::Button("Validate Transmittance LUT")) {
        lastValidationResult = ValidateAtmosphereTransmittanceLut(
            renderer, renderGraph, MakeDefaultEarthAtmosphereParameters());
    }
    if (lastValidationResult.has_value()) {
        const AtmosphereTransmittanceLutValidationResult& r = *lastValidationResult;
        const bool passed = r.succeeded && r.texelsExceedingEpsilon == 0;
        ImGui::TextColored(passed ? ImVec4(0.3f, 1.0f, 0.3f, 1.0f) : ImVec4(1.0f, 0.4f, 0.3f, 1.0f), "%s",
            !r.succeeded ? "ERROR" : (passed ? "PASS" : "CHECK"));
        ImGui::TextWrapped("%s", ToDiagnosticString(r).c_str());
    }

    // Regression guard: sky pixels must stay unaffected by this pass.
    if (ImGui::Button("Validate Aerial Perspective Sky Purity")) {
        lastSkyPurityResult = ValidateAerialPerspectiveSkyPurity(renderer, renderGraph, "GameView", "GameViewComposited");
    }
    if (lastSkyPurityResult.has_value()) {
        const AtmosphereAerialPerspectiveSkyPurityResult& r = *lastSkyPurityResult;
        const bool passed = r.Passed();
        ImGui::TextColored(passed ? ImVec4(0.3f, 1.0f, 0.3f, 1.0f) : ImVec4(1.0f, 0.4f, 0.3f, 1.0f), "%s",
            !r.succeeded ? "ERROR" : (passed ? "PASS" : "FAIL"));
        ImGui::TextWrapped("%s", ToDiagnosticString(r).c_str());
    }

    ImGui::End();
}

} // namespace gte
