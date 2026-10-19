#pragma once

#include "../../../Core/EditorPanelModule.h"

namespace gte {

class GtaoFeature;

// Draws the "Gtao" panel through the generic plugin-panel loop. A permanent built-in panel.
class GtaoPluginPanelModule final : public IEditorPanelModule_v1 {
public:
    explicit GtaoPluginPanelModule(GtaoFeature& feature) : m_feature(feature) { }

    GtaoPluginPanelModule(const GtaoPluginPanelModule&) = delete;
    GtaoPluginPanelModule& operator=(const GtaoPluginPanelModule&) = delete;

    const char* GetPanelName() const override { return "Gtao"; }

    void BuildPanel(IPluginPanelDrawContext& ctx) override;

private:
    GtaoFeature& m_feature;
};

} // namespace gte
