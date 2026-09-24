#pragma once

// editor-core-separation-3 campaign, PHASE4
// (PHASE4_EDITOR_PANEL_CAPABILITY_AND_REGISTRY.md) - maps to the source
// design doc's Milestone 2: the capability a plugin implements to own one
// real, dockable Editor panel.

namespace gte {

class IPluginPanelDrawContext;

// A plugin implements this to own one dockable Editor panel - source design
// doc, Section 2/6. Queried via
// IPluginModule::QueryCapability("IEditorPanelModule_v1"). Never queried by
// a Player-shaped host process at all (source design doc, Section 7) -
// this is exactly what makes an editor-tier plugin sitting in a Player's
// own plugins/ folder harmless: nothing ever calls QueryCapability() with
// this exact string outside gte_editor's own code.
class IEditorPanelModule_v1 {
public:
    virtual ~IEditorPanelModule_v1() = default;

    // A short, stable, static-duration string literal (never a
    // freshly-allocated buffer - Locked Design Decision #3,
    // PHASE0_MASTER_STRATEGY.md) - becomes this panel's own ImGui window
    // title/dock-registry key. Called once, right after this plugin loads
    // (EditorPanelRegistry population, gte_editor) - the returned pointer
    // must remain valid for the plugin's entire loaded lifetime (i.e. it
    // must not be a value computed fresh per call and freed afterward).
    virtual const char* GetPanelName() const = 0;

    // Called once per frame, ONLY while this panel is genuinely visible
    // (the same "only do real work while actually visible/docked-open"
    // discipline every existing Panels/*.cpp builder already follows) -
    // gte_editor's own code has already called ImGui::Begin(GetPanelName())
    // before this, and will call ImGui::End() immediately after - this
    // method draws ONLY this panel's own content, via `ctx`, never calling
    // a real ImGui::* function directly (PHASE0_MASTER_STRATEGY.md, Step
    // 2.4 - the ImGui-shared-global-context hazard).
    virtual void BuildPanel(IPluginPanelDrawContext& ctx) = 0;
};

inline constexpr const char* kIEditorPanelModule_v1_Name = "IEditorPanelModule_v1";

} // namespace gte
