#pragma once

namespace gte {

struct EditorContext;
struct AtmosphereSettings;

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
void BuildAtmospherePanel(EditorContext& ctx, AtmosphereSettings& settings);

} // namespace gte
