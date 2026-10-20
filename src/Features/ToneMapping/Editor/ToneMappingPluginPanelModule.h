#pragma once

#include "../../../Core/EditorPanelModule.h"

namespace gte {

class ToneMappingFeature;

// Draws the "ToneMapping" panel through the generic plugin-panel loop. A permanent built-in panel.
class ToneMappingPluginPanelModule final : public IEditorPanelModule_v1 {
public:
    explicit ToneMappingPluginPanelModule(ToneMappingFeature& feature) : m_feature(feature) { }

    ToneMappingPluginPanelModule(const ToneMappingPluginPanelModule&) = delete;
    ToneMappingPluginPanelModule& operator=(const ToneMappingPluginPanelModule&) = delete;

    const char* GetPanelName() const override { return "ToneMapping"; }

    void BuildPanel(IPluginPanelDrawContext& ctx) override;

private:
    ToneMappingFeature& m_feature;
};

} // namespace gte
