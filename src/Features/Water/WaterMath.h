#pragma once

#include "WaterTypes.h"
#include "../../Math/Mat4.h"
#include "../../Math/Vec2.h"
#include "../../Math/Vec3.h"

#include <array>
#include <cstdint>
#include <span>
#include <vector>

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


// CPU twin of the shader's point-in-polygon test (even-odd rule). False for fewer than 3 points.
bool WaterPointInPolygon(std::span<const WaterBodyPoint> polygon, Vec2 p) noexcept;

// Nearest point of a river polyline to `p`: returns distance minus the half width there (negative = inside).
// `level` and `halfWidth` are interpolated at that point and `tangent` is the unit direction of the segment.
float WaterRiverQuery(std::span<const WaterBodyPoint> line, Vec2 p, float& level, float& halfWidth, Vec2& tangent) noexcept;

// Catmull-Rom samples through `control` (2 or more points), `perSegment` samples per span, ends included.
// Level and half width are interpolated with the same weights. The result is capped at kWaterMaxRiverPoints.
std::vector<WaterBodyPoint> WaterSampleSpline(std::span<const WaterBodyPoint> control, std::uint32_t perSegment);

// Packs the bodies for the shader: drops invalid ones, caps counts, computes bounds. An empty list gives
// one implicit ocean at `seaLevel`. `packedBodies` receives the number of bodies written.
WaterBodiesGpu PackWaterBodies(std::span<const WaterBodyDesc> bodies, float seaLevel, std::uint32_t& packedBodies) noexcept;

// What the CPU says about one world point: the visible body, its level and the wave height without distance fade.
struct WaterProbe {
    bool inWater = false;
    int body = -1; // Index into the list; -1 when no body covers the point.
    float level = 0.0f;
    float waveScale = 1.0f;
    float waveHeight = 0.0f;
    float surfaceY = 0.0f;
};

// Highest body covering `xz` wins. An empty list means one ocean at `safe.seaLevel`.
WaterProbe WaterProbePoint(std::span<const WaterBodyDesc> bodies, const WaterSettings& safe, double timeSeconds, Vec2 xz) noexcept;

// True when at least one packed body has its surface below `eyeY`.
bool WaterAnyBodyBelowEye(const WaterBodiesGpu& packed, float eyeY) noexcept;

} // namespace gte
