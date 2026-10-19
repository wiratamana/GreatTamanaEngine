#pragma once

#include "GtaoTypes.h"
#include "../../Math/Mat4.h"
#include "../../Math/Vec3.h"

#include <cstdint>

namespace gte {

// World position of one depth sample through a camera's inverse view-projection.
Vec3 UnprojectGtaoDepth(const Mat4& invViewProjection, float ndcX, float ndcY, float depthZeroToOne) noexcept;

// Clamps every field into its valid range. A non-finite float becomes that field's default.
GtaoSettings SanitizeGtaoSettings(const GtaoSettings& settings) noexcept;

// AO texture size for a view: ceil(size / divisor), at least 1 per axis.
Extent2D AoExtentFor(Extent2D viewExtent, std::uint32_t divisor) noexcept;

// Camera axes and pixel scale from the inverse view-projection alone. Invalid for non-perspective or skewed cameras.
GtaoCameraBasis DeriveGtaoCameraBasis(const Vec3& eye, const Mat4& invViewProjection, Extent2D extent) noexcept;

// Occluder weight = clamp(distance * mul + add, 0, 1): 1 up to the fade start, 0 at the radius.
struct GtaoFalloff {
    float mul = 0.0f;
    float add = 0.0f;
};
GtaoFalloff MakeGtaoFalloff(float radius, float falloffFraction) noexcept;

// Sample radius in depth pixels of a surface at `viewDepth`, capped at `maxPixels`.
float GtaoScreenRadiusPixels(float radius, float viewDepth, float pixelScale, float maxPixels) noexcept;

// CPU reference of the shader's per-slice math, used to check the shader against brute force.
// One slice of a surface point: the normal projected into the slice plane.
struct GtaoSliceFrame {
    Vec3 orthoDir;       // Unit, in the slice plane, perpendicular to the view vector. Side 0 points along it.
    float projLen = 0.0f;
    float cosNorm = 1.0f; // Cosine of the projected normal's angle to the view vector.
    float n = 0.0f;       // Signed angle of the projected normal; positive leans toward side 0.
};
GtaoSliceFrame MakeGtaoSliceFrame(const Vec3& viewVec, const Vec3& normal, const Vec3& sliceDir) noexcept;

// Integral of one half-arc from the view vector to horizon angle `h`.
float GtaoArcIntegral(float n, float cosNorm, float h) noexcept;

// Weighted visibility of one slice from the horizon cosines of both sides (cosine of the angle to the view vector).
float GtaoSliceVisibility(const GtaoSliceFrame& slice, float horizonCos0, float horizonCos1) noexcept;

// GPU parameter blocks. `safe` must be sanitized.
GtaoMainParamsGpu MakeGtaoMainParams(
    const GtaoCameraBasis& basis, const GtaoSettings& safe, const Mat4& invViewProjection, std::uint32_t noiseFrame) noexcept;

GtaoResolveParamsGpu MakeGtaoResolveParams(const GtaoCameraBasis& basis, const GtaoHistory& previous,
    const GtaoSettings& safe, Extent2D viewExtent, const Mat4& invViewProjection, bool historyValid) noexcept;

GtaoCompositePush MakeGtaoCompositePush(const GtaoCameraBasis& basis, const GtaoSettings& safe,
    const Mat4& invViewProjection, std::uint32_t divisor) noexcept;

// True when `previous` may be reprojected into this frame: written last frame, same AO size and divisor,
// stable for two frames, and no camera cut.
bool HistoryUsable(const GtaoHistory& previous, const GtaoCameraBasis& basis, std::uint64_t frame, Extent2D aoExtent,
    std::uint32_t divisor) noexcept;

// History record of a frame whose resolve pass ran.
GtaoHistory MakeHistory(const GtaoCameraBasis& basis, const Mat4& viewProjection, std::uint64_t frame,
    Extent2D aoExtent, std::uint32_t divisor, const GtaoHistory& previous) noexcept;

} // namespace gte
