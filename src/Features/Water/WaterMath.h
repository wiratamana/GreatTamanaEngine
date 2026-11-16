#pragma once

#include "WaterTypes.h"
#include "../../Math/Mat4.h"
#include "../../Math/Vec2.h"
#include "../../Math/Vec3.h"

#include <array>
#include <cstdint>

namespace gte {

// Clamps every field into its valid range. A non-finite float becomes that field's default.
WaterSettings SanitizeWaterSettings(const WaterSettings& settings) noexcept;

// Everything the shade pass needs from the frame, already resolved by the feature.
struct WaterFrameInputs {
    Mat4 invViewProjection = Mat4::Identity(); // Clip to (world - eye).
    Mat4 viewProjection = Mat4::Identity();    // (world - eye) to clip.
    Vec3 eye;
    Vec2 jitterNdc;
    float pixelScale = 0.0f; // World size of one pixel at view depth 1.
    double timeSeconds = 0.0;
    Vec3 sunDirection = Vec3::Up();
    Vec3 sunRadiance = Vec3::One();
    float skyIntensity = 1.0f;
    float eyeHeightKm = 6371.0f;
    float planetRadiusKm = 6371.0f;
    float ambientScale = 1.0f;
    bool skyValid = false;
};

using WaterWaveTable = std::array<WaterWaveGpu, kWaterMaxWaves>;

// Wave table for `safe`: phases are folded in double so the shader adds only eye-relative terms.
WaterWaveTable BuildWaterWaves(const WaterSettings& safe, double eyeX, double eyeZ, double timeSeconds) noexcept;

// GPU block of the shade pass. `safe` must be sanitized.
WaterShadeParamsGpu MakeWaterShadeParams(const WaterSettings& safe, const WaterFrameInputs& in) noexcept;

// CPU reference of the shader's wave sum: height above the water level at eye-relative (x, z), no distance fade.
float WaterWaveHeight(const WaterWaveTable& waves, std::uint32_t count, float relX, float relZ) noexcept;

} // namespace gte
