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
