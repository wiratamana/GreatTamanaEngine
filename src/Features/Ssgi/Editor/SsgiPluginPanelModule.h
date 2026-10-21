#pragma once

#include "../../../Core/EditorPanelModule.h"

namespace gte {

class SsgiFeature;

// Draws the "Ssgi" panel through the generic plugin-panel loop. A permanent built-in panel.
class SsgiPluginPanelModule final : public IEditorPanelModule_v1 {
public:
    explicit SsgiPluginPanelModule(SsgiFeature& feature) : m_feature(feature) { }

    SsgiPluginPanelModule(const SsgiPluginPanelModule&) = delete;
    SsgiPluginPanelModule& operator=(const SsgiPluginPanelModule&) = delete;

    const char* GetPanelName() const override { return "Ssgi"; }

    void BuildPanel(IPluginPanelDrawContext& ctx) override;

private:
    SsgiFeature& m_feature;
};

} // namespace gte
