#include "AtmospherePanel.h"

#include "../EditorContext.h"
#include "../../Renderer/Atmosphere/AtmosphereParameters.h"
#include "../../Renderer/Atmosphere/AtmosphereTypes.h"

#include <imgui.h>

namespace gte {

void BuildAtmospherePanel(EditorContext& /*ctx*/, AtmosphereSettings& settings, Renderer& renderer,
    AtmosphereLutRenderer& atmosphereLutRenderer,
    std::optional<AtmosphereTransmittanceLutValidationResult>& lastValidationResult)
{
    ImGui::Begin("Atmosphere");

    ImGui::TextDisabled("Tunable, non-spatial atmosphere knobs (see AGENTS.md).");
    ImGui::Separator();

    ImGui::ColorEdit3("Ground Albedo Tint", &settings.groundAlbedoTint.x);
    ImGui::DragFloat("Aerial Perspective Strength", &settings.aerialPerspectiveStrength, 0.01f, 0.0f, 2.0f);

    // atmosphere-scattering-2 campaign, Phase 1 - single source of truth for
    // the aerial-perspective froxel volume's own ray-march tunables (see
    // AtmosphereTypes.h's own AtmosphereFrameUniforms/AtmosphereSettings doc
    // comments). Defaults (10.0/2.0/8/1.0) are IDENTICAL to the pre-Phase-1
    // hardcoded shader constants - a pure plumbing refactor, zero visual
    // change, until Phase 3 changes these DEFAULTS (never this UI).
    ImGui::DragFloat("Aerial Max Distance (km)", &settings.aerialPerspectiveMaxDistanceKm, 0.01f, 0.01f, 50.0f);
    ImGui::DragFloat("Aerial Depth Exponent", &settings.aerialPerspectiveDepthExponent, 0.05f, 1.0f, 4.0f);
    ImGui::SliderInt("Aerial Samples Per Slice", &settings.aerialPerspectiveSamplesPerSlice, 1, 8);
    ImGui::DragFloat(
        "Aerial Scattering Exaggeration", &settings.aerialPerspectiveScatteringExaggeration, 0.1f, 0.1f, 50.0f);
    ImGui::TextDisabled(
        "Max Distance/Depth Exponent/Samples Per Slice are now live (previously hardcoded shader constants). "
        "Scattering Exaggeration is plumbed through but NOT YET consumed by any shader math - it will start "
        "affecting the render once atmosphere-scattering-2 campaign Phase 3 lands.");
    ImGui::DragFloat("Sky Exposure", &settings.skyExposure, 0.1f, 0.0f, 50.0f);

    ImGui::Separator();
    ImGui::TextDisabled(
        "Sun direction/color/illuminance are controlled by a DirectionalLight ECS entity - see \"Hierarchy\" > "
        "right-click > \"Create Directional Light\", then edit its Transform/DirectionalLight in \"Inspector\".");

    // Phase 9 (ATMOSPHERE_PHASE9_VALIDATION_DEBUG_TOOLING_AND_DOCS_v1.md,
    // Step 3.2) - the debug-slice slider. 0..31 is a deliberate, hardcoded
    // range matching the aerial-perspective volume's own fixed 128x128x32
    // resolution (AtmosphereLutRenderer.cpp) - no configurable froxel grid
    // resolution is exposed to the Editor, matching Phase 6's own identical
    // precedent.
    ImGui::Separator();
    ImGui::SliderInt("Aerial Perspective Debug Slice", &settings.aerialPerspectiveDebugSliceIndex, 0, 31);
    ImGui::TextDisabled(
        "Mirrors one Z-slice of the Game View's own aerial-perspective volume into "
        "\"AtmosphereAerialPerspectiveVolumeDebugSlice\" (see GET /get_texture).");

    // Phase 9, Step 3.1 - the numeric parity tool's own button + readout.
    ImGui::Separator();
    if (ImGui::Button("Validate Transmittance LUT")) {
        lastValidationResult = ValidateAtmosphereTransmittanceLut(
            renderer, atmosphereLutRenderer, MakeDefaultEarthAtmosphereParameters());
    }
    if (lastValidationResult.has_value()) {
        const AtmosphereTransmittanceLutValidationResult& r = *lastValidationResult;
        const bool passed = r.succeeded && r.texelsExceedingEpsilon == 0;
        ImGui::TextColored(passed ? ImVec4(0.3f, 1.0f, 0.3f, 1.0f) : ImVec4(1.0f, 0.4f, 0.3f, 1.0f), "%s",
            !r.succeeded ? "ERROR" : (passed ? "PASS" : "CHECK"));
        ImGui::TextWrapped("%s", ToDiagnosticString(r).c_str());
    }

    ImGui::End();
}

} // namespace gte
