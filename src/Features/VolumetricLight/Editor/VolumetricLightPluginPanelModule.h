#pragma once

#include "../../../Core/EditorPanelModule.h"

namespace gte {

class VolumetricLightFeature;

// Draws the "VolumetricLight" panel through the generic plugin-panel loop. A permanent built-in panel.
class VolumetricLightPluginPanelModule final : public IEditorPanelModule_v1 {
public:
    explicit VolumetricLightPluginPanelModule(VolumetricLightFeature& feature) : m_feature(feature) { }

    VolumetricLightPluginPanelModule(const VolumetricLightPluginPanelModule&) = delete;
    VolumetricLightPluginPanelModule& operator=(const VolumetricLightPluginPanelModule&) = delete;

    const char* GetPanelName() const override { return "VolumetricLight"; }

    void BuildPanel(IPluginPanelDrawContext& ctx) override;

private:
    VolumetricLightFeature& m_feature;
};

} // namespace gte
