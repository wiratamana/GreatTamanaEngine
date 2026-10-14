#pragma once

#include "../../../Core/EditorPanelModule.h"
#include "AtmospherePanel.h"

namespace gte {

struct EditorContext;
class Renderer;
class AtmosphereFeature;

namespace rg {
class RenderGraph;
} // namespace rg

// Adapts BuildAtmospherePanel() onto IEditorPanelModule_v1 so the
// "Atmosphere" panel draws through EditorPanelRegistry's generic
// plugin-panel loop instead of a hardcoded ImGuiEditorLayer call.
//
// Never unregistered (a permanent, built-in panel - see
// EditorLayerAtmosphereBinding.h). Constructed once and owned by
// ImGuiEditorLayer for the rest of the process.
class AtmospherePluginPanelModule final : public IEditorPanelModule_v1 {
public:
    AtmospherePluginPanelModule(EditorContext& ctx, Renderer& /*engineRenderer*/,
        const rg::RenderGraph& /*renderGraph*/, AtmosphereFeature& feature)
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
