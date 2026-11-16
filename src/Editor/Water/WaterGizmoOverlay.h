#pragma once

// ImGui glue of the water gizmos: draws the outlines and handles over a view image and turns mouse input into body edits.

#include "WaterUiBridge.h"

#include "../../Math/Mat4.h"
#include "../../Math/Vec2.h"

namespace gte {

// Call inside the view panel, right after its ImGui::Image. `hovered` is the image's hover state.
// Left drag moves a handle, a midpoint handle or Ctrl+click on the outline inserts a point, Delete removes the selected one.
void DrawWaterGizmos(WaterUiBridge& bridge, const Mat4& viewProjection, Vec2 rectMin, Vec2 rectMax, bool hovered);

} // namespace gte
