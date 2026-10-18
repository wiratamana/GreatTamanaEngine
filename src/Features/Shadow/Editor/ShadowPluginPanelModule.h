#pragma once

#include "../../../Core/EditorPanelModule.h"

namespace gte {

struct EditorContext;
class ShadowFeature;

// Draws the "Shadow" panel through the generic plugin-panel loop. A permanent built-in panel.
class ShadowPluginPanelModule final : public IEditorPanelModule_v1 {
public:
    ShadowPluginPanelModule(EditorContext& ctx, ShadowFeature& feature) : m_ctx(ctx), m_feature(feature) { }

    ShadowPluginPanelModule(const ShadowPluginPanelModule&) = delete;
    ShadowPluginPanelModule& operator=(const ShadowPluginPanelModule&) = delete;

    const char* GetPanelName() const override { return "Shadow"; }

    void BuildPanel(IPluginPanelDrawContext& ctx) override;

private:
    EditorContext& m_ctx;
    ShadowFeature& m_feature;
};

} // namespace gte
