#include "ToonCharacterWindow.h"

#include "ToonCharacterPanel.h"
#include "../ToonCharacterFeature.h"
#include "../../../Core/FeatureKit/FeatureRegistration.h"
#include "../../../Editor/BuiltinFeatureEditorPanelRegistry.h"

#include <memory>

namespace gte {

void ToonCharacterWindow::OnGui(EditorFrame& /*frame*/)
{
    ToonCharacterSettings edit = m_feature.Settings();
    if (BuildToonCharacterPanel(
            edit, m_feature.Snapshot(rg::ViewScope::Primary), m_feature.Snapshot(rg::ViewScope::Secondary))) {
        m_feature.ApplySettings(edit);
    }
}

namespace {
std::unique_ptr<EditorWindow> CreateToonCharacterWindow(
    IEngineFeatureModule& module, EditorContext&, Renderer&, const rg::RenderGraph&)
{
    return std::make_unique<ToonCharacterWindow>(fx::FeatureOf<ToonCharacterFeature>(module));
}
} // namespace

GTE_REGISTER_BUILTIN_FEATURE_EDITOR_PANEL("ToonCharacter", &CreateToonCharacterWindow);

} // namespace gte
