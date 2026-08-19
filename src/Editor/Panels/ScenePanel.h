#pragma once

namespace gte {

struct EditorContext;

// Placeholder Scene view. IMPORTANT LIMITATION: the engine has no separate
// editor scene camera yet (no Camera component/view-projection matrix at
// all - see README.md, "Rendering"), so this just displays the SAME texture
// as GamePanel (ctx.gameViewDescriptor) for now. Deliberately does NOT touch
// ctx.desiredExtent (only GamePanel does), so resizing this panel alone
// never resizes the real Game-view RenderTexture - a real,
// independently-orbitable Scene camera is a natural follow-up once Camera
// exists as an ECS component. Called once per frame by
// ImGuiEditorLayer::BuildUI(), before BuildGamePanel().
void BuildScenePanel(EditorContext& ctx);

} // namespace gte
