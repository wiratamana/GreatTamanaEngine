#pragma once

// Cull view controls and HUD drawn over the top-right corner of the Scene image.

#include <imgui.h>

namespace gte {

struct EditorContext;

// Draws nothing while no feature is linked or the Scene view is stale. Sits below `switcherBottom` (screen Y).
// Returns true when the cursor is over the drawn area, so the caller can skip picking under it.
bool BuildScenePanelCullView(EditorContext& ctx, ImVec2 imageMin, ImVec2 imageSize, float switcherBottom);

} // namespace gte
