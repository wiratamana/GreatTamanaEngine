#include "SceneGridMath.h"

#include "../Math/MathTypes.h" // kEpsilon
#include "../Math/Vec4.h"

#include <algorithm>
#include <cmath>

namespace gte {

namespace {

// Unprojects NDC (ndcX, ndcY, ndcZ) through invViewProj INCLUDING the
// perspective divide - deliberately using the raw Vec4 projective multiply
// (Mat4's own `operator*(const Mat4&, const Vec4&)`), NEVER
// Mat4::TransformPoint()/TransformVector() (both of those assume affine
// input/output and never divide by the resulting w - see this module's
// own header comment for why that would silently be wrong here).
Vec3 UnprojectNdc(const Mat4& invViewProj, float ndcX, float ndcY, float ndcZ) noexcept
{
    const Vec4 clip(ndcX, ndcY, ndcZ, 1.0f);
    const Vec4 world = invViewProj * clip;
    if (std::fabs(world.w) < kEpsilon) {
        // Degenerate (a genuinely singular/degenerate invViewProj, or a
        // clip position exactly at the camera's own eye) - the caller
        // (ComputeGridPlaneHit) is expected to still produce a sane
        // "invalid" result from whatever this returns; returning the
        // origin here is an arbitrary-but-safe (never NaN/Inf) fallback.
        return Vec3::Zero();
    }
    const float invW = 1.0f / world.w;
    return Vec3(world.x * invW, world.y * invW, world.z * invW);
}

// x - floor(x): GLSL's fract() semantics exactly (always in [0, 1), even
// for negative x) - deliberately NOT std::fmod(), whose sign convention
// differs for negative inputs and would silently break the grid pattern's
// symmetry across the world origin.
float Frac(float x) noexcept
{
    return x - std::floor(x);
}

} // namespace

GridPlaneHit ComputeGridPlaneHit(const Mat4& invViewProj, const Mat4& viewProj, Vec2 ndcXY) noexcept
{
    // Two points along this pixel's camera ray: one on the near plane
    // (NDC z = 0), one on the far plane (NDC z = 1) - Vulkan's own
    // zero-to-one clip-space depth convention (see Mat4::PerspectiveFovLH_ZO).
    const Vec3 nearPoint = UnprojectNdc(invViewProj, ndcXY.x, ndcXY.y, 0.0f);
    const Vec3 farPoint = UnprojectNdc(invViewProj, ndcXY.x, ndcXY.y, 1.0f);
    const Vec3 rayDir = farPoint - nearPoint;

    // The ray is (numerically) parallel to the Y = 0 plane - e.g. looking
    // exactly level with the horizon. No meaningful single intersection
    // point exists.
    if (std::fabs(rayDir.y) < kEpsilon) {
        return GridPlaneHit{};
    }

    // Solve nearPoint.y + t * rayDir.y == 0 for t. t == 0 is the near
    // plane, t == 1 is the far plane - t outside (0, 1] means the
    // intersection is either behind the camera (t <= 0) or beyond the far
    // clip plane (t > 1), neither of which this pixel should ever draw a
    // grid fragment for.
    const float t = -nearPoint.y / rayDir.y;
    if (t <= 0.0f || t > 1.0f) {
        return GridPlaneHit{};
    }

    const Vec3 worldPos = nearPoint + rayDir * t;

    // Re-project worldPos through the ORIGINAL viewProj (not invViewProj)
    // to get the exact NDC depth a real rasterized quad at this exact
    // world position would have produced - this is what
    // SceneGridRenderer/SceneGrid.frag writes to gl_FragDepth (see
    // PHASE0_MASTER_STRATEGY.md's "Depth handling" decision).
    const Vec4 clipHit = viewProj * Vec4(worldPos.x, worldPos.y, worldPos.z, 1.0f);
    if (clipHit.w <= kEpsilon) {
        return GridPlaneHit{}; // Behind the camera in projective terms - reject.
    }
    const float ndcDepth = clipHit.z / clipHit.w;
    if (ndcDepth < 0.0f || ndcDepth > 1.0f) {
        // Should be structurally impossible given the t-range check above
        // (both derivations describe the same physical constraint), but
        // guarded explicitly anyway - never write an out-of-range
        // gl_FragDepth (undefined per the Vulkan spec without
        // VK_EXT_depth_range_unrestricted, which this engine does not use).
        return GridPlaneHit{};
    }

    GridPlaneHit result;
    result.valid = true;
    result.worldPosition = worldPos;
    result.ndcDepth = ndcDepth;
    return result;
}

float ComputeGridLineCoverage(float worldX, float worldZ, float cellSize, float derivativeX, float derivativeZ) noexcept
{
    if (cellSize <= kEpsilon) {
        return 0.0f;
    }

    const float coordX = worldX / cellSize;
    const float coordZ = worldZ / cellSize;

    // Never let a zero/near-zero derivative (e.g. a synthetic test value)
    // produce a divide-by-near-zero explosion - clamps the effective line
    // width to at most one full cell, which is a harmless, sane fallback
    // rather than Inf/NaN.
    const float dX = std::max(std::fabs(derivativeX / cellSize), kEpsilon);
    const float dZ = std::max(std::fabs(derivativeZ / cellSize), kEpsilon);

    // Classic "pristine grid" line-coverage formula: distance (in
    // fractional-cell units) from the NEAREST cell boundary, divided by
    // the pixel's own on-screen footprint in that same unit - <= 1 means
    // this pixel's footprint already spans the line, i.e. full coverage.
    const float gx = std::fabs(Frac(coordX - 0.5f) - 0.5f) / dX;
    const float gz = std::fabs(Frac(coordZ - 0.5f) - 0.5f) / dZ;
    const float lineFactor = std::min(gx, gz);

    return 1.0f - std::clamp(lineFactor, 0.0f, 1.0f);
}

float ComputeAxisLineCoverage(float distanceFromAxis, float derivative, float halfWidthWorld) noexcept
{
    if (halfWidthWorld <= 0.0f) {
        return 0.0f;
    }

    // Same "never let a near-zero derivative explode the divide" clamp
    // ComputeGridLineCoverage() above already applies - see its own
    // comment. std::fabs() here (rather than a bare max(derivative,
    // kEpsilon) the way SceneGrid.frag's own GLSL mirror can get away with)
    // is what keeps this CPU function well-defined even for a synthetic
    // test value that happens to pass a negative derivative - a real
    // fwidth() call site (see PHASE2_GRID_SHADERS.md) only ever produces a
    // non-negative value, so this never changes real, on-GPU behavior.
    const float d = std::max(std::fabs(derivative), kEpsilon);
    const float factor = (distanceFromAxis - halfWidthWorld) / d;
    return 1.0f - std::clamp(factor, 0.0f, 1.0f);
}

} // namespace gte
