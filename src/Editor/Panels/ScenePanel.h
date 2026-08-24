#pragma once

namespace gte {

struct EditorContext;
class EditorCamera;
class Game;
class Renderer;

// The Scene view - its own RenderTexture (ctx.sceneViewDescriptor),
// independent of GamePanel's (ctx.gameViewDescriptor), its own
// independently-orbitable camera (`camera`, see EditorCamera.h) instead of
// whatever ECS entity currently has the active Camera component - THAT is
// still what "Game" renders through - and now a Unity-style translate/
// rotate/scale gizmo (see TransformGizmo.h) for whichever entity is
// currently selected in the Hierarchy (ctx.selection.SelectedEntity()), plus a top-left
// Move/Rotate/Scale switcher overlay. Each panel has its own RenderTexture
// sized to its own panel's aspect ratio (see
// ImGuiEditorLayer::SceneViewTarget()), and each is only actually rendered
// into when its own panel is visible (see EditorContext::sceneViewVisible).
// Also reads ImGui's own mouse state (hover/drag/wheel) here and feeds it
// into `camera.Update()` as plain values - EditorCamera itself has no ImGui
// dependency at all (see its own class comment). `game`/`renderer` are
// needed for `game.GetRegistry()` (to look up
// ctx.selection.SelectedEntity()'s Transform for the gizmo) AND, when
// GTE_ENABLE_PROJECT_PANEL is on, to let a Project-panel asset be dragged
// straight onto the Scene image to instantiate it (see
// Game::CreateMeshEntityFromGtaFile()) - the same drag-and-drop target
// Panels/HierarchyPanel.cpp offers, just attached to the Scene image
// instead. Called once per frame by ImGuiEditorLayer::BuildUI(), before
// BuildGamePanel().
void BuildScenePanel(Game& game, Renderer& renderer, EditorContext& ctx, EditorCamera& camera);

} // namespace gte
