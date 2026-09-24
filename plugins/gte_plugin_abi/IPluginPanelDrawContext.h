#pragma once

// editor-core-separation-3 campaign, PHASE4
// (PHASE4_EDITOR_PANEL_CAPABILITY_AND_REGISTRY.md).

namespace gte {

// The ONLY way a plugin ever draws ImGui content - source design doc
// Section 6 never actually addresses this (see
// PHASE0_MASTER_STRATEGY.md, Step 2.4, for the full reasoning this
// campaign adds on top of the source doc). Dear ImGui keeps exactly one
// live, mutable global context per process (GImGui) - a plugin .dll
// calling a real ImGui::* function directly would operate on its OWN,
// separate, never-initialized context and crash immediately, completely
// independent of the fingerprint gate (Section 8), which only proves
// layout compatibility, never "these two separately-linked copies of
// ImGui share one live context." Every method here is implemented
// host-side, inside gte_editor.a (PluginPanelDrawContextAdapter), which
// already correctly shares the one true GImGui context - the plugin never
// touches ImGui directly, at all, ever.
//
// Deliberately minimal for this campaign's own Milestone 2 scope - exactly
// enough for "one trivial ImGui control" (source design doc, Section 11).
// A future _v2 (additive, never redefining this one) is where a genuinely
// richer widget surface (sliders, tables, tree nodes, ...) would be
// designed, once a real future capability actually needs it.
class IPluginPanelDrawContext {
public:
    virtual ~IPluginPanelDrawContext() = default;

    // Mirrors ImGui::TextUnformatted()'s own behavior (no printf-style
    // format string parsing on this side of the boundary - a plugin must
    // format its own string fully before calling this, since a va_list/
    // format string is exactly the kind of "not a plain built-in type"
    // surface Locked Design Decision #3 forbids).
    virtual void Text(const char* text) = 0;

    // Returns true exactly once, on the frame the button is clicked -
    // mirrors ImGui::Button()'s own real return-value contract.
    virtual bool Button(const char* label) = 0;

    virtual void Separator() = 0;
};

} // namespace gte
