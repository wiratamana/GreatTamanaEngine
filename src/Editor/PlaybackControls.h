#pragma once

namespace gte {

struct EditorContext;

// Draws the always-visible Pause/Resume + Step strip inside the dockspace host window.
// Called from DockLayout.cpp before ImGui::DockSpace(); it is not a closable panel.
void BuildPlaybackToolbar(EditorContext& ctx);

} // namespace gte
