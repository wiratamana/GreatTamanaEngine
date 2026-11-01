#include "AtmosphereWindow.h"

#include "AtmospherePanel.h"
#include "../AtmosphereFeature.h"
#include "../../../Core/FeatureKit/FeatureRegistration.h"
#include "../../../Editor/BuiltinFeatureEditorPanelRegistry.h"

#include <memory>

namespace gte {

void AtmosphereWindow::OnGui(EditorFrame& /*frame*/)
{
    AtmosphereSettings edit = m_feature.Settings();
    if (BuildAtmospherePanel(edit, m_feature.AerialSnapshot(rg::ViewScope::Primary))) {
        m_feature.ApplySettings(edit);
    }
}

namespace {
std::unique_ptr<EditorWindow> CreateAtmosphereWindow(
    IEngineFeatureModule& module, EditorContext&, Renderer&, const rg::RenderGraph&)
{
    return std::make_unique<AtmosphereWindow>(fx::FeatureOf<AtmosphereFeature>(module));
}
} // namespace

GTE_REGISTER_BUILTIN_FEATURE_EDITOR_PANEL("Atmosphere", &CreateAtmosphereWindow);

} // namespace gte
