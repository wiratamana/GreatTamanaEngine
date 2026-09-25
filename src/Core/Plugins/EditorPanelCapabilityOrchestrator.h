#pragma once

#include "IPluginCapabilityOrchestrator.h"

namespace gte {

// editor-core-separation-6 campaign, PHASE3
// (PHASE3_EDITOR_PANEL_ORCHESTRATOR_MIGRATION.md) - a VERBATIM relocation of
// EditorHost.cpp's own former inline IEditorPanelModule_v1 discovery loop
// (editor-core-separation-3, PHASE4) into the IPluginCapabilityOrchestrator
// shape - ZERO observable behavior change. Lives in gte_core (src/Core/
// Plugins/), NOT gte_editor, exactly like EditorPanelRegistry.h itself
// already does - see this phase's own Step 2 evidence for why this is not
// a layering violation. Overrides ONLY OnPluginsLoaded() - editor panels
// are drawn through Dear ImGui, never through the render graph, so this
// class never overrides ContributeRenderGraphPasses() (the interface's own
// default no-op is correct and sufficient).
class EditorPanelCapabilityOrchestrator final : public IPluginCapabilityOrchestrator {
public:
    void OnPluginsLoaded(const std::vector<IPluginModule*>& modules) override;
};

} // namespace gte
