#pragma once

#include "SsgiTypes.h"
#include "../../Core/FeatureKit/ScreenSpaceView.h"
#include "../../Math/Mat4.h"
#include "../../Math/Vec3.h"

#include <cstdint>
#include <vector>

namespace gte {

// Clamps every field into its valid range. A non-finite float becomes that field's default.
SsgiSettings SanitizeSsgiSettings(const SsgiSettings& settings) noexcept;

// Camera-relative world point of full-resolution pixel (px, py) at view depth `viewZ`.
Vec3 SsgiEyeRelativePoint(float px, float py, float viewZ, Extent2D view, const fx::CameraBasis& basis) noexcept;

struct SsgiProjection {
    bool valid = false;
    float x = 0.0f; // Full-resolution pixel position; pixel centers sit on integers.
    float y = 0.0f;
    float viewZ = 0.0f;
};

// Inverse of SsgiEyeRelativePoint. Invalid for points at or behind the camera plane.
SsgiProjection SsgiProjectToPixel(const Vec3& eyeRelative, Extent2D view, const fx::CameraBasis& basis) noexcept;

// Cosine-weighted direction in the hemisphere around the unit normal `n`; u0 and u1 are in [0, 1).
Vec3 SsgiCosineHemisphereDirection(float u0, float u1, const Vec3& n) noexcept;

float SsgiInterleavedGradientNoise(float x, float y) noexcept;

// Per-ray random numbers: (u0, u1) pick the hemisphere direction, `jitter` offsets the march steps.
struct SsgiRayNoise {
    float u0 = 0.0f;
    float u1 = 0.0f;
    float jitter = 0.0f;
};
SsgiRayNoise SsgiRayNoiseFor(int px, int py, std::uint32_t noiseFrame, std::uint32_t rayCount,
    std::uint32_t rayIndex) noexcept;

// Ray origin offset along the normal; also the hit margin. Scales with the GI texel footprint.
float SsgiRayOriginOffset(float depthBias, float viewZ, float pixelScale, std::uint32_t divisor) noexcept;

// One GI texel as the trace shader sees it. viewZ 0 means no surface.
struct SsgiGridTexel {
    float viewZ = 0.0f;
    Vec3 normal;
    Vec3 radiance;
};

struct SsgiGrid {
    Extent2D size{};
    std::vector<SsgiGridTexel> texels; // size.width * size.height, row major.
};

struct SsgiMarchResult {
    bool hit = false;
    bool blocked = false; // A back face stopped the ray.
    int texelX = -1;
    int texelY = -1;
    float distance = 0.0f; // Ray distance of the hit.
    Vec3 radiance;         // Hit radiance after the end fade.
};

// CPU reference of the trace shader's ray march. `safe` must be sanitized; `bias` is SsgiRayOriginOffset().
SsgiMarchResult SsgiMarchReference(const SsgiGrid& grid, const fx::CameraBasis& basis, Extent2D viewExtent,
    const Vec3& origin, const Vec3& dir, float jitter, const SsgiSettings& safe, float bias) noexcept;

// Receiver albedo from the scene color: hue from the color, brightness from `luma`.
Vec3 SsgiAlbedoGuess(const Vec3& sceneColor, float luma, float saturation) noexcept;

// GPU parameter blocks. `safe` must be sanitized.
SsgiPrepareParamsGpu MakeSsgiPrepareParams(
    const fx::CameraBasis& basis, const SsgiSettings& safe, const Mat4& invViewProjection) noexcept;

SsgiTraceParamsGpu MakeSsgiTraceParams(const fx::CameraBasis& basis, const SsgiSettings& safe, Extent2D viewExtent,
    std::uint32_t noiseFrame) noexcept;

SsgiCompositePush MakeSsgiCompositePush(const fx::CameraBasis& basis, const SsgiSettings& safe,
    const Mat4& invViewProjection, std::uint32_t divisor) noexcept;

} // namespace gte
