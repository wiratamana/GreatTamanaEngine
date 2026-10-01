#pragma once

// Relocated here by the `better-render-pass-2` campaign (PHASE1) - Project
// Assembly's own custom Editor panel capability implements this interface
// directly and must keep working after `plugins/gte_plugin_abi` is fully
// removed (PHASE4). Originally introduced by the editor-core-separation-3
// campaign, PHASE4 (PHASE4_EDITOR_PANEL_CAPABILITY_AND_REGISTRY.md), as part
// of `plugins/gte_plugin_abi/IEditorPanelModule.h`/`IPluginPanelDrawContext.h` -
// both interfaces copied verbatim (same method signatures, same names) since
// `EditorPanelRegistry.cpp`, `PluginPanelDrawContextAdapter.cpp`, and every
// Project Assembly `_Editor.dll` call these by exact name.

namespace gte {

class IPluginPanelDrawContext;

// A plugin/Project Assembly implements this to own one dockable Editor
// panel. Queried (for a real plugin) via
// IPluginModule::QueryCapability("IEditorPanelModule_v1"). Never queried by
// a Player-shaped host process at all - this is exactly what makes an
// editor-tier plugin sitting in a Player's own plugins/ folder harmless:
// nothing ever calls QueryCapability() with this exact string outside
// gte_editor's own code.
class IEditorPanelModule_v1 {
public:
    virtual ~IEditorPanelModule_v1() = default;

    // A short, stable, static-duration string literal (never a
    // freshly-allocated buffer) - becomes this panel's own ImGui window
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
    // a real ImGui::* function directly (the ImGui-shared-global-context
    // hazard).
    virtual void BuildPanel(IPluginPanelDrawContext& ctx) = 0;
};

inline constexpr const char* kIEditorPanelModule_v1_Name = "IEditorPanelModule_v1";

// The ONLY way a plugin/Project Assembly ever draws ImGui content. Dear
// ImGui keeps exactly one live, mutable global context per process
// (GImGui) - a plugin .dll calling a real ImGui::* function directly would
// operate on its OWN, separate, never-initialized context and crash
// immediately, completely independent of the plugin ABI fingerprint gate,
// which only proves layout compatibility, never "these two separately-
// linked copies of ImGui share one live context." Every method here is
// implemented host-side, inside gte_editor.a
// (PluginPanelDrawContextAdapter), which already correctly shares the one
// true GImGui context - the caller never touches ImGui directly, at all,
// ever.
//
// Deliberately minimal - exactly enough for "one trivial ImGui control". A
// future _v2 (additive, never redefining this one) is where a genuinely
// richer widget surface (sliders, tables, tree nodes, ...) would be
// designed, once a real future capability actually needs it.
class IPluginPanelDrawContext {
public:
    virtual ~IPluginPanelDrawContext() = default;

    // Mirrors ImGui::TextUnformatted()'s own behavior (no printf-style
    // format string parsing on this side of the boundary - a caller must
    // format its own string fully before calling this, since a va_list/
    // format string is exactly the kind of "not a plain built-in type"
    // surface this ABI forbids).
    virtual void Text(const char* text) = 0;

    // Returns true exactly once, on the frame the button is clicked -
    // mirrors ImGui::Button()'s own real return-value contract.
    virtual bool Button(const char* label) = 0;

    virtual void Separator() = 0;
};

} // namespace gte
