#pragma once

#include "../../Core/Extent.h"
#include "../../Core/FeatureKit/Texture.h"
#include "../../Math/Mat4.h"
#include "../../Math/Vec3.h"

#include <array>
#include <cstdint>
#include <type_traits>

namespace gte {

// Name the feature registers under.
inline constexpr const char* kVolumetricLightFeatureName = "VolumetricLight";

// Froxel grid size. A persistent volume has a fixed size, so the grid is a constant.
inline constexpr std::uint32_t kVolumetricLightGridWidth = 160;
inline constexpr std::uint32_t kVolumetricLightGridHeight = 90;
inline constexpr std::uint32_t kVolumetricLightGridDepth = 64;

// The history atlas tiles the depth slices as 2D tiles of one grid layer each.
inline constexpr std::uint32_t kVolumetricLightAtlasTilesX = 8;
inline constexpr std::uint32_t kVolumetricLightAtlasTilesY = kVolumetricLightGridDepth / kVolumetricLightAtlasTilesX;
inline constexpr std::uint32_t kVolumetricLightAtlasWidth = kVolumetricLightGridWidth * kVolumetricLightAtlasTilesX;
inline constexpr std::uint32_t kVolumetricLightAtlasHeight = kVolumetricLightGridHeight * kVolumetricLightAtlasTilesY;
static_assert(kVolumetricLightGridDepth % kVolumetricLightAtlasTilesX == 0);

inline constexpr std::uint32_t kVolumetricLightHaltonCycle = 16;

// Matches kDirectScale of the material lighting.
inline constexpr float kVolumetricLightSunDirectScale = 0.65f;

// Ambient light left when the sun is below the horizon.
inline constexpr float kVolumetricLightNightAmbientFraction = 0.15f;

// Smallest effective max distance, world units.
inline constexpr float kVolumetricLightMinDistance = 1.0f;

// The CPU wind offset wraps here, world units.
inline constexpr double kVolumetricLightNoiseWrap = 65536.0;

enum class VolumetricLightDebugView : std::uint8_t { Off, InScatterOnly, Transmittance };

enum class VolumetricLightFrameStatus : std::uint8_t {
    NotRendered,
    Ok,
    Disabled,
    NoValidCamera,
    ShadersNotReady,
    TargetFailed,
    PassDisabled,
};

enum class VolumetricLightReceiverState : std::uint8_t { NotChecked, Used, Missing, Rejected };

// Tunables. Stored only through SanitizeVolumetricLightSettings().
struct VolumetricLightSettings {
    bool enabled = true;
    bool applyInSceneView = true;     // false skips the editor Scene view.

    // Medium.
    float density = 0.015f;           // Extinction per world unit, 0..1.
    float albedo = 0.9f;              // Scattering over extinction, 0..1.
    float anisotropyG = 0.7f;         // Henyey-Greenstein g, -0.95..0.95.
    float heightDensity = 0.0f;       // Extra extinction at heightBase, 0..1.
    float heightBase = 0.0f;          // World Y where the height term starts to decay.
    float heightFalloff = 0.1f;       // Per world unit above heightBase, 0..2.
    float noiseAmount = 0.0f;         // 0..1.
    float noiseScale = 0.15f;         // Cycles per world unit, 0.001..4.
    float windX = 0.0f;               // World units per second, -20..20.
    float windZ = 0.0f;

    // Range and grid.
    float maxDistance = 60.0f;        // World units, 1..2000; shadow coverage clamps it.
    float startDistance = 0.0f;       // Fog fades in from here, 0..maxDistance.
    float depthExponent = 2.0f;       // Slice distribution, 1..4.

    // Light.
    float sunIntensity = 1.0f;        // 0..16, on top of illuminance * kVolumetricLightSunDirectScale.
    float ambientR = 0.35f;           // Fog ambient color, 0..4.
    float ambientG = 0.40f;
    float ambientB = 0.50f;
    float ambientIntensity = 0.3f;    // 0..16.
    float ambientShadowedFraction = 1.0f; // 0..1. Ambient kept in sun-shadowed air: 1 ignores shadows, 0 lights only sunlit air.

    // Shadow sampling.
    std::uint32_t shadowSamples = 4;  // 1, 2, 4 or 8.
    float shadowFilterTexels = 1.5f;  // 0..8.
    float shadowBiasTexels = 1.0f;    // 0..16.

    // Temporal.
    bool temporal = true;
    bool jitter = true;
    float historyBlend = 0.9f;        // 0..0.98.

    // Composite.
    float strength = 1.0f;            // 0..1.
    bool blendInLinearSpace = true;
    float dither = 1.0f;              // In 8-bit steps, 0..2.
    VolumetricLightDebugView debugView = VolumetricLightDebugView::Off;
};

// Previous-frame record that makes history reprojectable. Valid only if the temporal pass wrote it.
struct VolumetricLightHistory {
    bool valid = false;
    std::uint64_t frame = 0;
    std::uint32_t stableFrames = 0; // Frames in a row the history was continuous.
    Mat4 viewProjection = Mat4::Identity();
    Vec3 eye;
    Vec3 forward;
    Vec3 sunDirection = Vec3::Up();
    float maxDistance = 0.0f;
    float depthExponent = 0.0f;
    std::uint64_t lightingKey = 0;  // Hash of every setting that changes the lit result.
};

// Per-view, per-frame facts the parameter blocks are built from.
struct VolumetricLightFrameFacts {
    Mat4 viewProjection = Mat4::Identity();
    Mat4 invViewProjection = Mat4::Identity();
    Vec3 eye;
    Vec3 forward;
    Vec3 sunDirection = Vec3::Up();
    Vec3 sunIlluminance = Vec3::One();
    Vec3 jitter = Vec3{ 0.5f, 0.5f, 0.5f };
    std::array<double, 3> windOffset{}; // World units, wrapped.
    float maxDistance = 0.0f;           // Effective max distance of this frame.
    std::uint64_t frameIndex = 0;
};

// Channel "Result": volume pass to composite pass.
struct VolumetricLightResult {
    fx::Texture integrated; // rgb in-scatter, a transmittance, at each slice far boundary.
    float maxDistance = 0.0f;
    float depthExponent = 0.0f;
};

// What the endpoint reads, one per view scope.
struct VolumetricLightViewSnapshot {
    VolumetricLightFrameStatus status = VolumetricLightFrameStatus::NotRendered;
    std::uint64_t frameIndex = 0;
    VolumetricLightReceiverState receiver = VolumetricLightReceiverState::NotChecked;
    std::uint32_t cascadeCount = 0;
    float effectiveMaxDistance = 0.0f;
    bool historyUsed = false;
    std::uint32_t jitterIndex = 0;
    std::uint64_t textureBytes = 0;
};

// std430 block of VolumetricLightInject.comp; member order matches the shader.
struct VolumetricLightInjectParamsGpu {
    Mat4 invViewProjection;
    Mat4 cascadeViewProjection[4] = {};
    float cascadeSplitFar[4] = {};
    float cascadeBlendWidth[4] = {};
    float cascadeTexelWorld[4] = {};
    float cascadeDepthRange[4] = {};
    float eyeAndMaxDistance[4] = {};    // xyz eye, w effective max distance.
    float forwardAndExponent[4] = {};   // xyz camera forward, w depth exponent.
    float sunDirectionAndG[4] = {};     // xyz toward sun, w anisotropy g.
    float sunRadiance[4] = {};          // rgb.
    float ambientRadiance[4] = {};      // rgb.
    float densityParams[4] = {};        // x density, y albedo, z start distance, w noise amount.
    float heightParams[4] = {};         // x height density, y base, z falloff, w noise scale.
    float noiseOffset[4] = {};          // xyz wind offset, world units.
    float jitter[4] = {};               // xyz in [0,1).
    float shadowParams[4] = {};         // x bias texels, y filter texels, z 1 / map size, w ambient shadowed fraction.
    std::uint32_t counts[4] = {};       // x cascade count, y shadow samples, z frame index.
};
static_assert(sizeof(VolumetricLightInjectParamsGpu) == 560);
static_assert(std::is_trivially_copyable_v<VolumetricLightInjectParamsGpu>);

// std430 block of VolumetricLightTemporal.comp; member order matches the shader.
struct VolumetricLightTemporalParamsGpu {
    Mat4 invViewProjection;
    Mat4 prevViewProjection;
    float eyeAndMaxDistance[4] = {};     // Current frame.
    float prevEyeAndMaxDistance[4] = {}; // Previous frame.
    float exponentsAndBlend[4] = {};     // x current exponent, y previous exponent, z history weight, w valid.
    std::uint32_t atlas[4] = {};         // x tiles across, y tiles down.
};
static_assert(sizeof(VolumetricLightTemporalParamsGpu) == 192);
static_assert(std::is_trivially_copyable_v<VolumetricLightTemporalParamsGpu>);

// Push block of VolumetricLightIntegrate.comp.
struct VolumetricLightIntegratePush {
    float maxDistance = 0.0f;
    float depthExponent = 0.0f;
};
static_assert(sizeof(VolumetricLightIntegratePush) == 8);
static_assert(std::is_trivially_copyable_v<VolumetricLightIntegratePush>);

// Push block of VolumetricLightComposite.comp.
struct VolumetricLightCompositePush {
    Mat4 invViewProjection;
    float eyeAndMaxDistance[4] = {};
    float strengthExponentLinearDither[4] = {}; // x strength, y exponent, z linear flag, w dither.
    std::uint32_t frameAndDebug[4] = {};        // x frame index, y debug view.
};
static_assert(sizeof(VolumetricLightCompositePush) == 112);
static_assert(std::is_trivially_copyable_v<VolumetricLightCompositePush>);

} // namespace gte
