#pragma once

namespace gte {

class Game;
class Renderer;
struct EditorContext;

// Lists every entity that has a Transform (this engine's ECS has no
// separate "GameObject"/name concept yet - see ECS/Components/Transform.h -
// so a Transform is the closest thing to "something that belongs in the
// Hierarchy", the same role it plays in Unity) as a real, indented parent/
// child TREE (ECS/TransformHierarchy.h's GetChildren(), walked recursively
// from the root entities down) rather than a flat list - matching Unity's
// own Hierarchy now that Transform carries a real parent/child relationship
// (see ECS/Components/Transform.h's `parent`/`siblingIndex` fields).
// Clicking an entry calls ctx.selection.SelectEntity() (see Selection.h),
// which InspectorPanel then displays/edits.
//
// Drag-and-drop, Unity-style:
//   - Dragging one entity row onto ANOTHER entity row's MIDDLE ~50%
//     reparents the dragged entity as that row's new LAST child
//     (ECS/TransformHierarchy.h's SetParent(), worldPositionStays = true -
//     it doesn't visually jump in "Scene"/"Game").
//   - Dragging onto another row's TOP ~25%/BOTTOM ~25% instead REORDERS the
//     dragged entity as a new sibling immediately before/after that row
//     (SetParent() to the same parent, then SetSiblingIndex()) - this is
//     how sibling reordering happens, since this engine's ECS has no
//     separate "move up/down" command.
//   - Dragging onto empty panel space (or dropping an entity that's
//     currently a child anywhere in that empty-space target) DETACHES it
//     back to the scene root (SetParent(..., kInvalidEntity)).
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
