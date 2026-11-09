#pragma once

#include "VolumetricCloudTypes.h"
#include "../../Math/Mat4.h"
#include "../../Math/Vec3.h"

#include <cstdint>

namespace gte {

// CPU twins of the pure functions in VolumetricCloud.comp; the tests pin both sides to the same numbers.

// Linear remap of `v` from [lo0, hi0] to [lo1, hi1], not clamped. A zero-width source range maps like a step.
float CloudRemap(float v, float lo0, float hi0, float lo1, float hi1) noexcept;

// Height above sea level (m) of a point `horizontalSquared` (m^2) away from the planet axis at world height `y`.
// Exact where `y` and the offset are small against the radius, unlike the length of a point on a 6371 km sphere.
float CloudAltitudeM(float horizontalSquared, float y, float radiusM) noexcept;

// Position inside the layer: 0 at the base, 1 at the top.
float CloudHeightFraction(float altitudeM, float baseM, float topM) noexcept;

// Rounds the bottom and the top of a cloud whose peak sits at `peak` (0..1 of the layer).
float CloudShapeGradient(float heightFraction, float peak, float baseRoundness, float topRoundness) noexcept;

// Density ramp over the layer: thin at the base, thin again under the top.
float CloudDensityGradient(float heightFraction, float ramp, float topFade) noexcept;

// Part of one view ray that lies inside the layer shell, as distances along the ray.
struct CloudShellSegment {
    bool hit = false;
    float tStart = 0.0f;
    float tEnd = 0.0f;
};

// `cameraY` is the eye height above sea level and `dirY` the Y of the unit ray direction; the shell is centred
// below the eye, so nothing else matters. A ray that reaches the ground before the layer misses.
CloudShellSegment ComputeCloudShellSegment(
    float cameraY, float dirY, float radiusM, float baseM, float topM, float maxDistanceM) noexcept;

// Wind drift of the noise lookups: advances `offset` (x, z in metres) by `dt` seconds.
void AdvanceCloudWind(float (&offset)[2], float speed, float directionDegrees, float dt) noexcept;

// Phase functions, normalised so they integrate to 1 over the sphere. `cosTheta` is the cosine between the
// view ray and the direction toward the sun, so +1 looks into the sun.
float CloudHenyeyGreenstein(float cosTheta, float g) noexcept;
float CloudDraine(float cosTheta, float g, float alpha) noexcept;

// Draine constants for 10 micrometre droplets, mixed with a very forward Henyey-Greenstein lobe.
inline constexpr float kCloudDraineHgG = 0.9881f;
inline constexpr float kCloudDraineG = 0.5567f;
inline constexpr float kCloudDraineAlpha = 21.9955f;
inline constexpr float kCloudDraineWeight = 0.4824f;

// Phase of one scattering octave; `eccentricity` scales every g (1 = the full model, 0 = isotropic).
float CloudPhase(const VolumetricCloudSettings& safe, float cosTheta, float eccentricity) noexcept;

// Beer-Lambert transmittance for an optical depth.
float CloudBeer(float opticalDepth) noexcept;

// Sun light reaching a sample: the sum over octaves of b^i * phase_i * exp(-tau * a^i), plus the silver lobe.
float CloudSunScatter(const VolumetricCloudSettings& safe, float cosTheta, float opticalDepth) noexcept;

// One energy-conserving march step: the fraction of the light behind that the step absorbs.
float CloudStepOpacity(float extinction, float stepLength) noexcept;

// Adds one step to a front-to-back accumulation. `source` is the radiance per unit of absorbed light.
void CloudAccumulateStep(float& light, float& transmittance, float source, float opacity) noexcept;

// What the per-frame parameter block needs besides the settings.
struct VolumetricCloudFrameFacts {
    Mat4 invViewProjection = Mat4::Identity();
    Vec3 eye;
    Vec3 sunDirection = Vec3::Up();
    Vec3 sunIlluminance = Vec3::One();
    Vec3 sunTint = Vec3::One();
    float ambientScale = 1.0f;
    float windOffset[2] = {};
    std::uint64_t frameIndex = 0;
};

VolumetricCloudParamsGpu MakeVolumetricCloudParams(
    const VolumetricCloudSettings& safe, const VolumetricCloudFrameFacts& facts) noexcept;

} // namespace gte
