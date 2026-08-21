#pragma once

namespace gte {

class Game;
class Renderer;
struct EditorContext;

// Lists every entity that has a Transform (this engine's ECS has no
// separate "GameObject"/name concept yet - see ECS/Components/Transform.h -
// so a Transform is the closest thing to "something that belongs in the
// Hierarchy", the same role it plays in Unity). Clicking an entry calls
// ctx.selection.SelectEntity() (see Selection.h), which InspectorPanel then
// displays/edits.
//
// Right-clicking empty space in the panel opens a Unity-style "Create 3D
// Object" context menu (Cube/Sphere/Capsule/Cone/Plane) that spawns a new
// entity via Game::CreatePrimitiveEntity() and selects it - see
// PrimitiveMeshGenerator.h for the shapes themselves. `game`/`renderer` are
// needed for exactly that (Game::CreatePrimitiveEntity() needs a live
// Renderer to build/upload a shape's GPU mesh the first time it's
// requested) - nothing else in this panel touches either beyond
// `game.GetRegistry()`, same narrow "Editor only calls Game's own public
// API" boundary as before (see EditorLayer.h's BuildUI() comment).
//
// Called once per frame by ImGuiEditorLayer::BuildUI(), before
// BuildInspectorPanel().
void BuildHierarchyPanel(Game& game, Renderer& renderer, EditorContext& ctx);

} // namespace gte
