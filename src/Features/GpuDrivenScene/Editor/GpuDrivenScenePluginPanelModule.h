#pragma once

#include "../../../Core/EditorPanelModule.h"

namespace gte {

struct EditorContext;
class GpuDrivenSceneFeature;

// Draws the "GPU Driven Scene" panel through the generic plugin-panel loop. A permanent built-in panel.
// Hands the feature's cull link to the editor so the Scene panel can reach it.
class GpuDrivenScenePluginPanelModule final : public IEditorPanelModule_v1 {
public:
    GpuDrivenScenePluginPanelModule(EditorContext& ctx, GpuDrivenSceneFeature& feature);

    GpuDrivenScenePluginPanelModule(const GpuDrivenScenePluginPanelModule&) = delete;
    GpuDrivenScenePluginPanelModule& operator=(const GpuDrivenScenePluginPanelModule&) = delete;

    const char* GetPanelName() const override { return "GPU Driven Scene"; }

    void BuildPanel(IPluginPanelDrawContext& ctx) override;

private:
    GpuDrivenSceneFeature& m_feature;
};

} // namespace gte
