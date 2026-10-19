#pragma once

#include "../../../Core/EditorPanelModule.h"

namespace gte {

struct EditorContext;
class GtaoFeature;

// Draws the "Gtao" panel through the generic plugin-panel loop. A permanent built-in panel.
class GtaoPluginPanelModule final : public IEditorPanelModule_v1 {
public:
    GtaoPluginPanelModule(EditorContext& ctx, GtaoFeature& feature) : m_ctx(ctx), m_feature(feature) { }

    GtaoPluginPanelModule(const GtaoPluginPanelModule&) = delete;
    GtaoPluginPanelModule& operator=(const GtaoPluginPanelModule&) = delete;

    const char* GetPanelName() const override { return "Gtao"; }

    void BuildPanel(IPluginPanelDrawContext& ctx) override;

private:
    EditorContext& m_ctx;
    GtaoFeature& m_feature;
};

} // namespace gte
