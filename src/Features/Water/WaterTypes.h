#pragma once

#include "WaterBodyDesc.h"
#include "../../Core/Extent.h"
#include "../../Math/Mat4.h"
#include "../../Math/Vec3.h"

#include <cstdint>
#include <type_traits>

namespace gte {

// Name the feature registers under.
inline constexpr const char* kWaterFeatureName = "Water";

// Most waves the shader sums.
inline constexpr std::uint32_t kWaterMaxWaves = 8;

enum class WaterDebugView : std::uint8_t {
    Off,
    Depth,
    WaterDepth,
    ShoreMask,
    Foam,
    Normal,
    WaveHeight,
    Reflection,
    BodyId,
    WetMask,
};
inline constexpr std::uint8_t kWaterDebugViewCount = 10;

enum class WaterFrameStatus : std::uint8_t {
    NotRendered,
    Ok,
    NoValidCamera,
    ShadersNotReady,
    TargetFailed,
    PassDisabled,
    Disabled,
    CameraUnderwater,
    NoBodies,
};

// Water tunables. Stored only through SanitizeWaterSettings().
struct WaterSettings {
    bool enabled = true;
    float seaLevel = 0.0f;               // -10000..10000, world Y of the ocean.

    // Waves.
    std::uint32_t waveCount = 6;         // 1..8.
    float waveHeight = 1.2f;             // 0..30, crest to trough of the sum, metres.
    float choppiness = 0.6f;             // 0..1, crest sharpness.
    float waveSpeed = 1.0f;              // 0..4, multiplier of the deep-water phase speed.
    float windDirectionDeg = 30.0f;      // Degrees, 0 = +X, 90 = +Z.
    float windSpread = 35.0f;            // 0..90, degrees the waves fan around the wind.
    float baseWavelength = 70.0f;        // 4..400, longest wave in metres.
    float rippleStrength = 0.6f;         // 0..2, small detail normals.
    float rippleScale = 1.0f;            // 0.25..4, ripple size multiplier.
    float waveLodScale = 1.0f;           // 0.25..4, higher drops fine waves earlier with distance.

    // Colour.
    Vec3 shallowColor{ 0.10f, 0.52f, 0.50f };
    Vec3 deepColor{ 0.01f, 0.07f, 0.16f };
    Vec3 foamColor{ 0.95f, 0.97f, 1.0f };
    float clarity = 14.0f;               // 0.5..200, metres of water until the deep colour wins.
    float absorption = 1.0f;             // 0..8, scale of how fast the scene below fades.
    float refractionStrength = 0.05f;    // 0..0.3, screen shift of the seabed by the ripples.

    // Surface lighting.
    float roughness = 0.07f;             // 0.02..0.6, sun glint width.
    float sunSpecular = 1.0f;            // 0..8.
    float reflectionStrength = 1.0f;     // 0..2.

    // Foam.
    float crestFoam = 0.8f;              // 0..2.
    float shoreFoam = 1.0f;              // 0..2.
    float shoreWidth = 2.5f;             // 0.1..30, metres of depth the shore foam reaches.

    // Screen-space reflection.
    bool ssr = false;
    std::uint32_t ssrSteps = 16;         // 4..48.
    float ssrMaxDistance = 400.0f;       // 10..20000, metres.

    // Wet shoreline on the terrain.
    bool wetShore = true;
    float wetBand = 1.5f;                // 0.1..20, metres above the water level.
    float wetStrength = 0.45f;           // 0..1, darkening.
    float wetFadeDistance = 3000.0f;     // 100..50000, metres.

    // Rendering budget.
    float maxDistance = 100000.0f;       // 100..1e6, ocean ray length limit in metres.
    bool halfRes = false;                // Shade at half resolution; the composite keeps the scene depth edges exact.
    bool halfResAuto = true;             // Also use half resolution when the view has 600k pixels or more.

    WaterDebugView debugView = WaterDebugView::Off;
};

// What the endpoint and the panel read, one per view scope.
struct WaterViewSnapshot {
    WaterFrameStatus status = WaterFrameStatus::NotRendered;
    std::uint64_t frameIndex = 0;
    Extent2D extent{};
    std::uint32_t bodyCount = 0;
    std::uint32_t waveCount = 0;
    std::uint64_t textureBytes = 0;
    bool skyValid = false;
};

// One wave of the shade table; two vec4 so the std430 array stride is 32 bytes.
struct WaterWaveGpu {
    float dirAndK[4] = {};   // xy unit direction on XZ, z wave number (rad/m), w crest exponent.
    float ampPhase[4] = {};  // x amplitude (m), y frequency (rad/s), z phase offset (rad), w mean of the profile.
};
static_assert(sizeof(WaterWaveGpu) == 32);
static_assert(std::is_trivially_copyable_v<WaterWaveGpu>);

// One ripple layer: scale, wrapped offset and the lattice period (in layer units) that keeps the hash periodic.
struct WaterRippleLayerGpu {
    float scaleOffsetPeriod[4] = {}; // x scale (1/m), yz wrapped offset (layer units), w period.
};
static_assert(sizeof(WaterRippleLayerGpu) == 16);

// std430 block of WaterShade.comp; member order matches the shader.
struct WaterShadeParamsGpu {
    Mat4 invViewProjection;           // Clip to (world - eye).
    Mat4 viewProjection;              // (world - eye) to clip, unjittered.
    float eyeTime[4] = {};            // xyz eye world position, w time in seconds.
    float viewInfo[4] = {};           // x pixel scale, yz jitter NDC, w sea level.
    float sunDirection[4] = {};       // xyz toward the sun, w sun specular strength.
    float sunRadiance[4] = {};        // rgb sun illuminance after the air tint, w sky intensity.
    float skyInfo[4] = {};            // x eye height km, y planet radius km, z sky valid, w ambient scale.
    float shallowColor[4] = {};       // rgb, w clarity.
    float deepColor[4] = {};          // rgb, w refraction strength.
    float foamColor[4] = {};          // rgb, w crest foam.
    float surfaceInfo[4] = {};        // x shore foam, y shore width, z roughness, w reflection strength.
    float rippleInfo[4] = {};         // x ripple strength, y wave lod scale, z absorption, w max distance.
    float wetInfo[4] = {};            // x wet band, y wet strength, z wet fade distance, w ssr max distance.
    std::uint32_t counts[4] = {};     // x wave count, y debug view, z ssr steps, w flags (1 ssr, 2 wet, 4 sky).
    WaterRippleLayerGpu ripple[2];
    WaterWaveGpu waves[kWaterMaxWaves];
};
static_assert(sizeof(WaterShadeParamsGpu) == 128 + 16 * 11 + 16 + 32 + 32 * kWaterMaxWaves);
static_assert(std::is_trivially_copyable_v<WaterShadeParamsGpu>);

// One body of the shade table; four vec4 so the std430 array stride is 64 bytes.
struct WaterBodyHeaderGpu {
    float info[4] = {};             // x kind, y level, z wave scale, w body id.
    float bounds[4] = {};           // xz min, xz max of the body in world metres.
    std::uint32_t range[4] = {};    // x first point, y point count.
    float shape[4] = {};            // x flow speed, y lowest surface level, z highest surface level.
};
static_assert(sizeof(WaterBodyHeaderGpu) == 64);

// std430 block "bodies" of WaterShade.comp.
struct WaterBodiesGpu {
    std::uint32_t counts[4] = {};   // x body count.
    WaterBodyHeaderGpu headers[kWaterMaxBodies];
    float points[kWaterMaxPoints][4] = {}; // xz world position, z river level, w river half width.
};
static_assert(sizeof(WaterBodiesGpu) == 16 + 64 * kWaterMaxBodies + 16 * kWaterMaxPoints);
static_assert(std::is_trivially_copyable_v<WaterBodiesGpu>);

inline constexpr std::uint32_t kWaterFlagSsr = 1u;
inline constexpr std::uint32_t kWaterFlagWet = 2u;
inline constexpr std::uint32_t kWaterFlagSky = 4u;

} // namespace gte
