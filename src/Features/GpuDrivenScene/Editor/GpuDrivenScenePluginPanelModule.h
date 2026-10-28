#pragma once

#include "../../../Core/EditorPanelModule.h"

namespace gte {

class GpuDrivenSceneFeature;

// Draws the "Culling" panel through the generic plugin-panel loop. A permanent built-in panel.
class GpuDrivenScenePluginPanelModule final : public IEditorPanelModule_v1 {
public:
    explicit GpuDrivenScenePluginPanelModule(GpuDrivenSceneFeature& feature) : m_feature(feature) { }

    GpuDrivenScenePluginPanelModule(const GpuDrivenScenePluginPanelModule&) = delete;
    GpuDrivenScenePluginPanelModule& operator=(const GpuDrivenScenePluginPanelModule&) = delete;

    const char* GetPanelName() const override { return "Culling"; }

    void BuildPanel(IPluginPanelDrawContext& ctx) override;

private:
    GpuDrivenSceneFeature& m_feature;
};

} // namespace gte
