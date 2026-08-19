#pragma once

namespace gte {

struct EditorContext;

// The real Game view - writes ctx.desiredExtent every frame from this
// panel's own current content-region size (read back next frame by
// ImGuiEditorLayer::GameViewTarget(), which resizes the actual Game-view
// RenderTexture to match - see its comment for why that can't safely happen
// here, mid-frame). This is what makes the Game-view RenderTexture always
// match exactly this panel's own current size/shape (Unity's "Free Aspect"
// behavior), whatever ScenePanel (or any other panel) happens to be sized
// at. Called once per frame by ImGuiEditorLayer::BuildUI(), after
// BuildScenePanel().
void BuildGamePanel(EditorContext& ctx);

} // namespace gte
