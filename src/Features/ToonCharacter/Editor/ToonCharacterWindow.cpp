#include "ToonCharacterWindow.h"

#include "ToonCharacterPanel.h"
#include "../ToonCharacterFeature.h"
#include "../../../Core/FeatureKit/FeatureRegistration.h"
#include "../../../Editor/BuiltinFeatureEditorPanelRegistry.h"
#include "../../../Editor/EditorContext.h"
#include "../../../Editor/EditorFrame.h"

#include <memory>

namespace gte {

void ToonCharacterWindow::OnFrame(EditorFrame& frame)
{
    ToonFeatureReport& report = frame.ctx.toonFeature;
    report.published = true;
    report.graphEnabled = false;
    for (const RenderFeatureDebugEntry& entry : frame.renderFeatureEntries) {
        if (entry.name == kToonCharacterFeatureName) {
            report.graphEnabled = entry.enabled && (entry.enabledPrimary || entry.enabledSecondary);
            break;
        }
    }
    // The Game view is the reference; the Scene view stands in while the Game view is not drawn.
    ToonCharacterViewSnapshot view = m_feature.Snapshot(rg::ViewScope::Primary);
    if (view.status == ToonCharacterFrameStatus::NotRendered) {
        view = m_feature.Snapshot(rg::ViewScope::Secondary);
    }
    report.status = view.status;
    report.candidatePartCount = view.candidatePartCount;
    report.claimedEntityCount = view.claimedEntityCount;
}

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
