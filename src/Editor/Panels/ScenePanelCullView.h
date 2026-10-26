#pragma once

// Cull view controls and HUD drawn over the top-right corner of the Scene image.

#include <imgui.h>

namespace gte {

struct EditorContext;

// Draws nothing while no feature is linked, the Scene view did not render or its report is stale.
// The box sits below `switcherBottom` (screen Y of the gizmo switcher row).
// Returns true when the cursor is over the drawn area, so the caller can skip picking under it.
bool BuildScenePanelCullView(EditorContext& ctx, ImVec2 imageMin, ImVec2 imageSize, float switcherBottom);

} // namespace gte
