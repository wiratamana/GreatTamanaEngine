#pragma once

#include "../AtmosphereAerialPerspectiveLutInspection.h"
#include "../AtmosphereAerialPerspectiveSkyPurityValidation.h"
#include "../AtmosphereTransmittanceLutValidation.h"

#include <optional>

namespace gte {

struct EditorContext;
struct AtmosphereSettings;
class Renderer;
class AtmosphereLutRenderer;

namespace rg {
class RenderGraph;
} // namespace rg

// Atmosphere Scattering + Aerial Perspective campaign, Phase 8
// (task_manager/atmosphere-scattering-1/ATMOSPHERE_PHASE8_SUN_ECS_AND_EDITOR_CONTROLS_v1.md)
// - a small, stateless, free-function panel editing the small set of
// tunable, non-spatial atmosphere knobs (AtmosphereTypes.h's
// AtmosphereSettings), the exact "stateless free function taking
// EditorContext&" convention AGENTS.md's "Editor Module Structure" section
// documents for HierarchyPanel/InspectorPanel/MemoryPanel (NOT a small
// stateful class like ProfilerPanel/RenderGraphPanel/JobsPanel - this
// panel has no cross-frame state of its own to justify that heavier
// pattern). Docked alongside "Memory"/"Profiler"/"Render Graph"/"Project"
// along the bottom (see DockLayout.cpp). `settings` is Application's own
// AtmosphereSettings (Application::m_atmosphereSettings), read/written
// directly by reference - the same way InspectorPanel reads/writes a
// selected entity's Camera component by reference, just without an ECS
// entity backing this one. Called once per frame by
// ImGuiEditorLayer::BuildUI(), alongside the other bottom-docked panels.
//
// Phase 9 (ATMOSPHERE_PHASE9_VALIDATION_DEBUG_TOOLING_AND_DOCS_v1.md, Step
// 3.1/3.2) grew this panel's own responsibility with two new pieces,
// keeping the panel itself STILL a stateless free function - the panel's
// own genuinely cross-frame state (`lastValidationResult`, the "last
// result" readout) is owned by the CALLER (ImGuiEditorLayer, mirroring how
// it already owns m_gameView/m_sceneView etc.) and passed in by reference,
// rather than turning this panel into a small stateful class:
//   - a "Validate Transmittance LUT" button + result readout (calls
//     AtmosphereTransmittanceLutValidation.h's
//     ValidateAtmosphereTransmittanceLut());
//   - a "Aerial Perspective Debug Slice" slider
//     (AtmosphereSettings::aerialPerspectiveDebugSliceIndex).
//
// atmosphere-scattering-2 campaign, Phase 5
// (task_manager/atmosphere-scattering-2/PHASE5_AERIAL_LUT_NUMERIC_VALIDATION_TOOL.md)
// adds a THIRD such piece, following the exact same "caller owns the
// cross-frame result, this panel stays stateless" convention -
// `lastAerialInspectionResult` (an "Inspect Aerial Perspective LUT" button +
// printed min/max/mean transmittance/in-scattering readout, calling
// AtmosphereAerialPerspectiveLutInspection.h's InspectAerialPerspectiveVolume()).
//
// atmosphere-scattering-4 campaign, Phase 3
// (task_manager/atmosphere-scattering-4/PHASE3_REGRESSION_DIAGNOSTIC_TOOLING.md)
// adds a FOURTH such piece, same convention - `renderGraph` (the same
// `const rg::RenderGraph&` ImGuiEditorLayer::BuildUI() already has in scope,
// passed straight through) and `lastSkyPurityResult` (a "Validate Aerial
// Perspective Sky Purity" button + PASS/FAIL/ERROR readout, calling
// AtmosphereAerialPerspectiveSkyPurityValidation.h's
// ValidateAerialPerspectiveSkyPurity()).
void BuildAtmospherePanel(EditorContext& ctx, AtmosphereSettings& settings, Renderer& renderer,
    AtmosphereLutRenderer& atmosphereLutRenderer,
    std::optional<AtmosphereTransmittanceLutValidationResult>& lastValidationResult,
    std::optional<AtmosphereAerialPerspectiveLutInspectionResult>& lastAerialInspectionResult,
    const rg::RenderGraph& renderGraph,
    std::optional<AtmosphereAerialPerspectiveSkyPurityResult>& lastSkyPurityResult);

} // namespace gte
