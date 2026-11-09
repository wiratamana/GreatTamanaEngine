#pragma once

#include "../../Game/Lighting/AtmosphereSkyLuts.h"
#include "../../Math/Mat4.h"
#include "../../Math/Vec3.h"

#include <cstdint>
#include <type_traits>

namespace gte {

// AtmosphereParametersGpu and AtmosphereFrameUniforms (the GPU layouts the sky shaders read) live in the shared
// header AtmosphereSkyLuts.h, so other features can read the sky LUT channels.

// Tunable atmosphere knobs, edited live in the Editor's "Atmosphere" panel. The physical fields mirror
// AtmosphereParametersGpu and are re-read every frame by AtmosphereFeature. One instance per session.
struct AtmosphereSettings {
    Vec3 rayleighScattering = Vec3(0.0058f, 0.0135f, 0.0331f);
    float rayleighScaleHeightKm = 8.0f; // Scale height (km), not the GPU struct's reciprocal.
    Vec3 mieScattering = Vec3(0.006f, 0.006f, 0.006f);
    float miePhaseG = 0.76f;
    Vec3 mieAbsorption = Vec3(0.00066f, 0.00066f, 0.00066f);
    float mieScaleHeightKm = 1.2f; // Scale height (km).
    Vec3 ozoneAbsorption = Vec3(0.00065f, 0.00188f, 0.00008f);
    float ozoneTentCenterKm = 25.0f;
    Vec3 groundAlbedo = Vec3(0.3f, 0.3f, 0.3f); // Fraction of light the ground reflects back.
    float ozoneTentHalfWidthKm = 15.0f;
    float planetRadiusKm = 6371.0f;
    float atmosphereThicknessKm = 100.0f;
    float multiScatteringStrength = 1.0f;

    // Strength of the aerial perspective composite: 1.0 is the physical result, 0.0 passes the scene color through.
    float aerialPerspectiveStrength = 1.0f;

    // Linear multiplier on the sky background radiance.
    float skyIntensity = 6.0f;

    // Aerial perspective ray-march tunables. With autoScale on, the max distance and the exaggeration are derived
    // from the visible depth range each frame and these two values apply only as fallbacks.
    float aerialPerspectiveMaxDistanceKm = 0.5f;
    float aerialPerspectiveDepthExponent = 2.0f;
    int aerialPerspectiveSamplesPerSlice = 8;
    float aerialPerspectiveScatteringExaggeration = 8.0f;
    bool autoScale = true;

    // Sun and sky light of materials and light shafts follow the atmosphere.
    bool sceneLightFollowsAtmosphere = true;
};

// Sun-visibility taps per aerial perspective march sample (rotated per sample), and the tap filter and bias in texels.
inline constexpr std::uint32_t kAerialShadowTaps = 2;
inline constexpr float kAerialShadowFilterTexels = 1.5f;
inline constexpr float kAerialShadowBiasTexels = 1.0f;

// Sun cascade data read by AtmosphereAerialPerspectiveVolume.comp (block "shadow", std430).
struct AerialShadowParamsGpu {
    Mat4 cascadeViewProjection[4];
    float cascadeSplitFar[4] = {};
    float cascadeBlendWidth[4] = {};
    float cascadeTexelWorld[4] = {};
    float cascadeDepthRange[4] = {};
    float eyeAndUnitsPerKm[4] = {};  // xyz world eye, w world units per km.
    float forwardAndBias[4] = {};    // xyz camera forward, w bias in texels.
    float shadowParams[4] = {};      // x filter texels, y 1 / map size, z shadow blend.
    std::uint32_t counts[4] = {};    // x cascade count (0 = no shadow taps), y taps per sample.
};
static_assert(sizeof(AerialShadowParamsGpu) == 384, "AerialShadowParamsGpu must match the shader block");
static_assert(std::is_trivially_copyable_v<AerialShadowParamsGpu>);

// Why a view's aerial volume did or did not use the sun cascades.
enum class AerialReceiverState : std::uint8_t { NotChecked, Used, Missing, Rejected, Mismatch };

// What the HTTP endpoint reads for one view.
struct AtmosphereAerialSnapshot {
    float maxDistanceKm = 0.0f;
    float exaggeration = 0.0f;
    float shadowBlend = 0.0f;
    std::uint32_t cascadeCount = 0;
    AerialReceiverState receiver = AerialReceiverState::NotChecked;
    bool scaleHeld = false;
    std::uint64_t frameIndex = 0;
};

} // namespace gte
