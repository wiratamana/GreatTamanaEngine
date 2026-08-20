#pragma once

namespace gte {

struct EditorContext;
class EditorCamera;

// The Scene view - its own RenderTexture (ctx.sceneViewDescriptor),
// independent of GamePanel's (ctx.gameViewDescriptor), AND now its own
// independently-orbitable camera (`camera`, see EditorCamera.h) instead of
// whatever ECS entity currently has the active Camera component - THAT is
// still what "Game" renders through. Each panel has its own RenderTexture
// sized to its own panel's aspect ratio (see
// ImGuiEditorLayer::SceneViewTarget()), and each is only actually rendered
// into when its own panel is visible (see EditorContext::sceneViewVisible).
// Also reads ImGui's own mouse state (hover/drag/wheel) here and feeds it
// into `camera.Update()` as plain values - EditorCamera itself has no ImGui
// dependency at all (see its own class comment). Called once per frame by
// ImGuiEditorLayer::BuildUI(), before BuildGamePanel().
void BuildScenePanel(EditorContext& ctx, EditorCamera& camera);

} // namespace gte
