#pragma once

// editor-core-separation-3 campaign, PHASE4
// (PHASE4_EDITOR_PANEL_CAPABILITY_AND_REGISTRY.md) - gte_editor's own
// implementation of IPluginPanelDrawContext.

#include "../../Core/EditorPanelModule.h" // relocated here, better-render-pass-2 PHASE1.

namespace gte {

// gte_editor's own implementation - constructed FRESH, once per visible
// plugin panel, per frame, immediately after the host's own
// ImGui::Begin(panelName) call (ImGuiEditorLayer.cpp) and destroyed before
// the matching ImGui::End() - stateless, holds nothing, simply forwards
// each call into the real ImGui:: API, which is always safe to call here
// (this code compiles into gte_editor.a itself, sharing the one true
// GImGui context - PHASE0_MASTER_STRATEGY.md, Step 2.4).
class PluginPanelDrawContextAdapter final : public IPluginPanelDrawContext {
public:
    void Text(const char* text) override;
    bool Button(const char* label) override;
    void Separator() override;
};

} // namespace gte
