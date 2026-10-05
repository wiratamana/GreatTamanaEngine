#include "AtmospherePluginPanelModule.h"

#include "../AtmosphereFeature.h"

namespace gte {

void AtmospherePluginPanelModule::BuildPanel(IPluginPanelDrawContext& /*ctx*/)
{
    // Ignores the restricted ctx parameter (established convention - see
    // IPluginPanelDrawContext's own doc comment) and calls the real panel
    // builder directly.
    BuildAtmospherePanel(m_ctx, m_feature.Settings(), m_engineRenderer, m_feature.Renderer(),
        m_lastValidationResult, m_lastAerialInspectionResult, m_renderGraph, m_lastSkyPurityResult);
}

} // namespace gte
