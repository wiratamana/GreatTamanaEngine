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

} // namespace gte
