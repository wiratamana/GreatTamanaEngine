#pragma once

#include "../../../Core/EditorPanelModule.h"

namespace gte {

class SceneLightingFeature;

// Draws the "SceneLighting" panel through the generic plugin-panel loop. A permanent built-in panel.
class SceneLightingPluginPanelModule final : public IEditorPanelModule_v1 {
public:
    explicit SceneLightingPluginPanelModule(SceneLightingFeature& feature) : m_feature(feature) { }

    SceneLightingPluginPanelModule(const SceneLightingPluginPanelModule&) = delete;
    SceneLightingPluginPanelModule& operator=(const SceneLightingPluginPanelModule&) = delete;

    const char* GetPanelName() const override { return "SceneLighting"; }

    void BuildPanel(IPluginPanelDrawContext& ctx) override;

private:
    SceneLightingFeature& m_feature;
};

} // namespace gte
