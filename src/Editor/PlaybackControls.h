#pragma once

namespace gte {

struct EditorContext;

// frame-debugger-1 campaign (task_manager/frame-debugger-1/
// PHASE3_EDITOR_PAUSE_STEP_STATE_AND_TOOLBAR_UI.md) - renders the small,
// ALWAYS-visible Pause/Resume + Step control strip, directly inside the
// Editor's own full-viewport dockspace host window
// ("EditorDockSpaceHost" - see DockLayout.cpp) - deliberately NOT a
// dockable/closable panel under Panels/ (unlike Hierarchy/Inspector/Scene/
// Game/...), since a Unity-style playback toolbar must always stay
// visible in a fixed place, never accidentally closed/dragged away by the
// user. Called from DockLayout.cpp's BuildDockspaceAndMenuBar(), right
// after the menu bar block (ImGui::EndMenuBar()), BEFORE the
// ImGui::DockSpace(...) call itself.
void BuildPlaybackToolbar(EditorContext& ctx);

} // namespace gte
