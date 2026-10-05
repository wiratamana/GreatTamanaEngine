#pragma once

#include "../../../Core/EditorPanelModule.h"
#include "AtmospherePanel.h"
#include "AtmosphereAerialPerspectiveLutInspection.h"
#include "AtmosphereAerialPerspectiveSkyPurityValidation.h"
#include "AtmosphereTransmittanceLutValidation.h"

#include <optional>

namespace gte {

struct EditorContext;
class Renderer;
class AtmosphereFeature;

namespace rg {
class RenderGraph;
} // namespace rg

// Adapts BuildAtmospherePanel() onto IEditorPanelModule_v1 so the
// "Atmosphere" panel draws through EditorPanelRegistry's generic
// plugin-panel loop instead of a hardcoded ImGuiEditorLayer call. Owns the
// three cross-frame validation-result caches directly, mirroring how a
// real Project Assembly panel owns its own state.
//
// Never unregistered (a permanent, built-in panel - see
// EditorLayerAtmosphereBinding.h). Constructed once and owned by
// ImGuiEditorLayer for the rest of the process.
class AtmospherePluginPanelModule final : public IEditorPanelModule_v1 {
public:
    AtmospherePluginPanelModule(EditorContext& ctx, Renderer& engineRenderer,
        const rg::RenderGraph& renderGraph, AtmosphereFeature& feature)
        : m_ctx(ctx), m_engineRenderer(engineRenderer), m_renderGraph(renderGraph), m_feature(feature)
    {
    }

    AtmospherePluginPanelModule(const AtmospherePluginPanelModule&) = delete;
    AtmospherePluginPanelModule& operator=(const AtmospherePluginPanelModule&) = delete;

    const char* GetPanelName() const override { return "Atmosphere"; }

    void BuildPanel(IPluginPanelDrawContext& /*ctx*/) override;

private:
    EditorContext& m_ctx;

    // m_engineRenderer is the engine's main Renderer; m_feature.Renderer()
    // below returns this feature's own, separate AtmosphereLutRenderer -
    // kept textually distinct to avoid confusing the two.
    Renderer& m_engineRenderer;
    const rg::RenderGraph& m_renderGraph;
    AtmosphereFeature& m_feature;

    std::optional<AtmosphereTransmittanceLutValidationResult> m_lastValidationResult;
    std::optional<AtmosphereAerialPerspectiveLutInspectionResult> m_lastAerialInspectionResult;
    std::optional<AtmosphereAerialPerspectiveSkyPurityResult> m_lastSkyPurityResult;
};

} // namespace gte
