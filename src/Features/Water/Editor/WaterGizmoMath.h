#pragma once

// Pure math of the water gizmos: mouse rays, ground picking, handle lists and hit tests. No ImGui.

#include "../../../Math/Mat4.h"
#include "../../../Math/Vec2.h"
#include "../../../Math/Vec3.h"
#include "../World/WaterEdit.h"
#include "../World/WaterTypes.h"

#include <cstdint>
#include <span>
#include <vector>

namespace gte {

struct WaterRay {
    double origin[3] = {};
    double direction[3] = {}; // Unit length.
};

// Ray through a pixel of the view image. `viewProjection` is the engine's flipY matrix (depth 0..1).
bool ScreenToRay(const Mat4& viewProjection, Vec2 rectMin, Vec2 rectMax, Vec2 pixel, WaterRay& out);

// Where the ray crosses the horizontal plane at height `y` (in front of the origin only).
bool RayHitPlaneY(const WaterRay& ray, double y, double& x, double& z);

// First crossing of the ray with the ground: marches `maxDistance` metres, then bisects.
bool RayHitGround(const WaterRay& ray, const water::HeightFn& ground, double maxDistance, double& x, double& y, double& z);

// One draggable point of a body outline or river path. A midpoint handle inserts a new point when used.
struct WaterHandle {
    std::uint32_t bodyId = 0;
    int index = 0;        // Point index; for a midpoint, the index the new point would get.
    bool midpoint = false;
    Vec3 world;
    Vec2 screen;
};

// Appends the handles of `body` that are in front of the camera.
void BuildWaterHandles(const water::WaterBody& body, const Mat4& viewProjection, Vec2 rectMin, Vec2 rectMax, std::vector<WaterHandle>& out);

// Nearest handle within `radius` pixels; corner handles win over midpoints. -1 when none.
int PickWaterHandle(std::span<const WaterHandle> handles, Vec2 pixel, float radius);

// Index a point clicked on the outline or path would be inserted at, and the pixel distance to it. -1 when no edge is in front.
int NearestOutlineInsertIndex(const water::WaterBody& body, const Mat4& viewProjection, Vec2 rectMin, Vec2 rectMax, Vec2 pixel,
    float& distancePixels);

// World height of the drag plane for one point of a body.
double HandlePlaneY(const water::WaterBody& body, int index) noexcept;

} // namespace gte
