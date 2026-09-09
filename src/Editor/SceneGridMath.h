#pragma once

#include "../Math/Mat4.h"
#include "../Math/Vec2.h"
#include "../Math/Vec3.h"

namespace gte {

// The result of casting the Scene camera's ray through one screen pixel
// (in NDC space) against the world's Y = 0 ground plane - the pure CPU
// mirror of the per-pixel computation SceneGrid.frag (see
// PHASE2_GRID_SHADERS.md) performs on the GPU. See this header's own file
// comment in SceneGridMath.cpp for why the two must be kept in lockstep,
// and PHASE0_MASTER_STRATEGY.md's "Locked Design Decisions" for why this
// is computed via a raw ray-plane intersection rather than any real 3D
// geometry.
struct GridPlaneHit {
    // False whenever this pixel's camera ray never meaningfully hits the
    // ground plane in front of the camera and within the view frustum's
    // own depth range - a ray parallel to the plane (looking exactly
    // level with the horizon), a plane behind the camera, or a plane
    // intersection beyond the far clip plane. A caller (the shader)
    // discards the pixel entirely whenever this is false - see
    // SceneGrid.frag.
    bool valid = false;

    // World-space position of the intersection - only meaningful when
    // valid == true.
    Vec3 worldPosition{};

    // The exact NDC depth ([0, 1], matching this engine's Vulkan
    // zero-to-one depth range - see Mat4::PerspectiveFovLH_ZO) this
    // intersection point re-projects to through the SAME viewProj matrix
    // that was used to unproject it - what the shader writes to
    // gl_FragDepth so the grid is correctly depth-TESTED (never
    // depth-WRITTEN - see PHASE0_MASTER_STRATEGY.md's own "Depth
    // handling" design decision) against real scene geometry. Only
    // meaningful when valid == true.
    float ndcDepth = 1.0f;
};

// Computes GridPlaneHit for the camera ray through NDC screen position
// `ndcXY` (each component in [-1, 1], Vulkan's own raw clip-space
// convention - NOT flipped/adjusted here in any way; see
// PHASE2_GRID_SHADERS.md's own header comment for why no Y-flip is ever
// needed anywhere in this feature). `invViewProj` MUST be the caller's own
// Mat4::TryInverse() result of `viewProj` (the exact SAME matrix,
// inverted) - passing a mismatched pair produces meaningless output with
// no way for this function to detect it.
GridPlaneHit ComputeGridPlaneHit(const Mat4& invViewProj, const Mat4& viewProj, Vec2 ndcXY) noexcept;

// Anti-aliased grid-line coverage in [0, 1] (0 = fully off a line, 1 =
// fully on one) for a square grid of `cellSize` world units, at world-space
// position (worldX, worldZ), given the screen-space derivative of that
// same world position along each axis (derivativeX/derivativeZ - the
// fwidth()-equivalent inputs; a real GPU call site supplies these via
// fwidth(), which has no literal CPU equivalent - see this function's own
// definition comment in SceneGridMath.cpp for why a plain float parameter
// is still exactly the right, fully-testable shape here). Returns 0.0f for
// a non-positive cellSize - never NaN/divide-by-zero garbage.
float ComputeGridLineCoverage(
    float worldX, float worldZ, float cellSize, float derivativeX, float derivativeZ) noexcept;

// Anti-aliased single-axis-line coverage in [0, 1] (0 = fully off the axis
// line, 1 = fully on it) for a Unity-style colored X/Z axis highlight line -
// the pure CPU mirror of SceneGrid.frag's AxisLineCoverage() (Phase 2).
// Unlike ComputeGridLineCoverage() above (whose line spacing comes from a
// cell size), a single axis line has a fixed WORLD-SPACE half-width
// (`halfWidthWorld`) instead: `distanceFromAxis` is the absolute world-space
// distance from the axis itself (e.g. abs(worldPos.z) for the X axis, which
// runs along Z == 0), and `derivative` is the screen-space derivative of
// that same distance (the fwidth()-equivalent input, same shape as
// ComputeGridLineCoverage()'s own derivativeX/derivativeZ parameters).
// Returns 0.0f for a non-positive halfWidthWorld - never NaN/Inf.
float ComputeAxisLineCoverage(float distanceFromAxis, float derivative, float halfWidthWorld) noexcept;

} // namespace gte
