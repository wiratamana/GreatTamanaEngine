#pragma once

namespace gte {

struct EditorContext;
class Game;
class Renderer;

// Hosts a full-viewport, invisible window carrying the top menu bar (File >
// Save Scene/Open Scene/Exit, ...) and the DockSpace every other Editor panel
// docks into, and ensures the default Unity-style Hierarchy/Inspector/
// Scene+Game layout gets built exactly once (see DockLayout.cpp for the full
// one-shot rationale - it matters for correctness, not just as an
// optimization). Also handles the Ctrl+S/Ctrl+O global keyboard shortcuts for
// Save/Open Scene (Editor/SceneIO.h). Called once per frame by
// ImGuiEditorLayer::BuildUI(), before any panel builder (Panels/*.h) runs.
void BuildDockspaceAndMenuBar(EditorContext& ctx, Game& game, Renderer& renderer);

// network-impl-7 campaign - the ONE place ImGui::FindWindowByName()
// (imgui_internal.h) is called from for the GET /activate_tab feature,
// mirroring this file's own pre-existing "the ONE file with an
// imgui_internal.h dependency" role (see DefaultDockLayoutIsNeeded() above).
// Looks up `panelName` by EXACT name; if a live ImGuiWindow with that exact
// name currently exists, calls ImGui::SetWindowFocus(panelName) (bringing
// it to the front - for a docked window, this selects it as that dock
// node's active tab, exactly like a user clicking the tab) and returns
// true. Returns false, and does nothing else, if no such window exists yet
// this session (e.g. requested before this panel's own first Begin() call
// this session - an accepted, narrow race - see IEditorLayer::ActivateTab()'s
// own doc comment, EditorLayer.h). Does NOT itself validate `panelName`
// against EditorPanelCatalog.h's known list - that validation already
// happened one layer up, in NetworkRoutes.cpp (Phase 4), before this call
// was ever reached; this function is a dumb, generic "does a window with
// this exact name exist right now" primitive, reusable for any future
// need, not specific to the known-panel-catalog restriction.
bool FindAndFocusEditorWindow(const char* panelName);

} // namespace gte
