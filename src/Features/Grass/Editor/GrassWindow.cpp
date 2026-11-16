#include "GrassWindow.h"

#include "GrassPanel.h"
#include "../GrassFeature.h"
#include "../../../Core/FeatureKit/FeatureRegistration.h"
#include "../../../Editor/BuiltinFeatureEditorPanelRegistry.h"

#include <imgui.h>

#include <memory>

namespace gte {

void GrassWindow::OnGui(EditorFrame& /*frame*/)
{
    GrassSettings edit = m_feature.UserSettings();
    if (BuildGrassPanel(edit, m_feature.Snapshot(rg::ViewScope::Primary), m_feature.Snapshot(rg::ViewScope::Secondary))) {
        m_feature.ApplySettings(edit);
    }

    // Without a terrain height tile from the editor, a made-up landscape lets the grass be tuned on its own.
    ImGui::SeparatorText("Test landscape");
    bool synthetic = m_feature.SyntheticTileEnabled();
    if (ImGui::Checkbox("Made-up ground that follows the camera", &synthetic)) {
        if (synthetic) {
            m_feature.EnableSyntheticTile(GrassSyntheticSpec{});
        } else {
            m_feature.DisableSyntheticTile();
        }
    }
    if (synthetic) {
        const char* const kinds[] = { "Flat", "Hills", "Hills and a cliff" };
        GrassSyntheticSpec spec = m_feature.SyntheticSpec();
        int kind = static_cast<int>(spec.kind);
        bool changed = ImGui::Combo("Shape", &kind, kinds, 3);
        changed |= ImGui::Checkbox("Lake", &spec.lake);
        changed |= ImGui::SliderFloat("Ground height (m)", &spec.baseHeight, -20.0f, 100.0f);
        changed |= ImGui::SliderFloat("Sea level (m)", &spec.seaLevel, -100.0f, 100.0f);
        if (changed) {
            spec.kind = static_cast<GrassSyntheticKind>(kind);
            m_feature.EnableSyntheticTile(spec);
        }
    }
}

namespace {
std::unique_ptr<EditorWindow> CreateGrassWindow(
    IEngineFeatureModule& module, EditorContext&, Renderer&, const rg::RenderGraph&)
{
    return std::make_unique<GrassWindow>(fx::FeatureOf<GrassFeature>(module));
}
} // namespace

GTE_REGISTER_BUILTIN_FEATURE_EDITOR_PANEL("Grass", &CreateGrassWindow);

} // namespace gte
