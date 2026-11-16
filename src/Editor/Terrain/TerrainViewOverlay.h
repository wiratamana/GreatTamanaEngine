#pragma once

// ImGui glue of the terrain debug views: node boxes over a view image and the free-fly / orbit camera input.

#include "TerrainUiBridge.h"
#include "../../Math/Vec2.h"

namespace gte {

// Draws the box of every resident node over the image at [rectMin, rectMax) with the current window's draw list.
// Does nothing while the bridge has no nodes (the overlay flag is off).
void DrawTerrainBoundsOverlay(const TerrainUiBridge& bridge, const Mat4& viewProjection, Vec2 rectMin, Vec2 rectMax);

// Reads mouse and keys over the Game image and queues them as camera input. Call right after the image item.
void GatherTerrainCameraInput(TerrainUiBridge& bridge, bool imageHovered);

} // namespace gte
