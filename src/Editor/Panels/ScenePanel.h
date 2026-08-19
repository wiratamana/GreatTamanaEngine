#pragma once

namespace gte {

struct EditorContext;

// The Scene view - its own RenderTexture (ctx.sceneViewDescriptor),
// independent of GamePanel's (ctx.gameViewDescriptor). Both currently show
// the identical scene through whatever entity has the active Camera
// component (see ECS/Components/Camera.h) - there is no independently-
// orbitable EDITOR-only camera yet, so "Scene" and "Game" always agree on
// viewpoint, but each has its own RenderTexture sized to its own panel's
// aspect ratio (see ImGuiEditorLayer::SceneViewTarget()), and each is only
// actually rendered into when its own panel is visible (see
// EditorContext::sceneViewVisible - a real, independently-orbitable Scene
// camera remains a natural follow-up, at which point this becomes genuinely
// different from "Game" rather than just a second render of the same
// viewpoint). Called once per frame by ImGuiEditorLayer::BuildUI(), before
// BuildGamePanel().
void BuildScenePanel(EditorContext& ctx);

} // namespace gte
