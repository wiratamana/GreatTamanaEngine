#pragma once

#include "../../../Core/EditorPanelModule.h"
#include "AtmospherePanel.h"

namespace gte {

struct EditorContext;
class AtmosphereFeature;

// Adapts BuildAtmospherePanel() onto IEditorPanelModule_v1 so the "Atmosphere" panel draws through the generic
// plugin-panel loop. A permanent built-in panel, owned by ImGuiEditorLayer.
class AtmospherePluginPanelModule final : public IEditorPanelModule_v1 {
public:
    AtmospherePluginPanelModule(EditorContext& ctx, AtmosphereFeature& feature)
        : m_ctx(ctx), m_feature(feature)
    {
    }

    AtmospherePluginPanelModule(const AtmospherePluginPanelModule&) = delete;
    AtmospherePluginPanelModule& operator=(const AtmospherePluginPanelModule&) = delete;

    const char* GetPanelName() const override { return "Atmosphere"; }

    void BuildPanel(IPluginPanelDrawContext& /*ctx*/) override;

private:
    EditorContext& m_ctx;
    AtmosphereFeature& m_feature;
};

} // namespace gte
