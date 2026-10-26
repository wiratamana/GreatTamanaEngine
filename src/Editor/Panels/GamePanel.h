#pragma once

namespace gte {

struct EditorContext;

// The Game panel. Writes its content size to ctx.desiredExtent and its visibility to ctx.gameViewVisible each frame;
// ImGuiEditorLayer applies both to the primary view's texture next frame.
void BuildGamePanel(EditorContext& ctx);

} // namespace gte
