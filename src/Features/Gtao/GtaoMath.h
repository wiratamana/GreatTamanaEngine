#pragma once

#include "GtaoTypes.h"
#include "../../Core/FeatureKit/ScreenSpaceView.h"
#include "../../Math/Mat4.h"
#include "../../Math/Vec3.h"

#include <cstdint>

namespace gte {

// Clamps every field into its valid range. A non-finite float becomes that field's default.
GtaoSettings SanitizeGtaoSettings(const GtaoSettings& settings) noexcept;

// Occluder weight = clamp(distance * mul + add, 0, 1): 1 up to the fade start, 0 at the radius.
struct GtaoFalloff {
    float mul = 0.0f;
    float add = 0.0f;
};
GtaoFalloff MakeGtaoFalloff(float radius, float falloffFraction) noexcept;

// Sample radius in depth pixels of a surface at `viewDepth`, capped at `maxPixels`.
float GtaoScreenRadiusPixels(float radius, float viewDepth, float pixelScale, float maxPixels) noexcept;

// One slice of a surface point: the normal projected into the slice plane.
// Also the CPU reference of the shader's per-slice math.
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
GtaoMainParamsGpu MakeGtaoMainParams(const fx::CameraBasis& basis, const GtaoSettings& safe,
    const Mat4& invViewProjection, std::uint32_t noiseFrame) noexcept;

GtaoCompositePush MakeGtaoCompositePush(const fx::CameraBasis& basis, const GtaoSettings& safe,
    const Mat4& invViewProjection, std::uint32_t divisor) noexcept;

} // namespace gte
