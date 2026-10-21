#pragma once

#include "AtmosphereTypes.h"
#include "../../Game/Lighting/SunLightModulation.h"
#include "../../Math/Vec3.h"

namespace gte {

// CPU reference for the atmosphere density, optical depth, transmittance and phase math; the shaders mirror it.
// Pure functions. Every `positionKm`/`direction` uses the planet center as origin:
// Length(positionKm) == planetRadiusKm + heightAboveGroundKm.

// Rayleigh density profile: exponential falloff, 1.0 at height 0.
float RayleighDensityAtHeight(const AtmosphereParametersGpu& params, float heightKm) noexcept;

// Mie density profile: same shape with a much shorter scale height (aerosols stay near the ground).
float MieDensityAtHeight(const AtmosphereParametersGpu& params, float heightKm) noexcept;

// Ozone density profile: a symmetric tent peaking at 1.0 at ozoneTentCenterKm and reaching 0 at
// +/- ozoneTentHalfWidthKm. A non-positive half-width gives 0.0 everywhere.
float OzoneDensityAtHeight(const AtmosphereParametersGpu& params, float heightKm) noexcept;

// Total extinction coefficient (scattering + absorption, per channel) at an altitude: Rayleigh scattering,
// Mie scattering plus absorption, and ozone absorption.
Vec3 ComputeExtinctionCoefficientAtHeight(const AtmosphereParametersGpu& params, float heightKm) noexcept;

// Optical depth from positionKm along `direction` (normalized internally) to the top of the atmosphere, by a
// fixed-step ray march like the transmittance shader. Always integrates to the outer boundary, even through the
// planet; ground occlusion is the caller's concern. Returns Vec3::Zero() for sampleCount <= 0 or a degenerate path.
Vec3 ComputeOpticalDepthToTopOfAtmosphere(
    const AtmosphereParametersGpu& params, Vec3 positionKm, Vec3 direction, int sampleCount) noexcept;

// exp(-opticalDepth) per channel: the fraction of light that survives to the top of the atmosphere.
Vec3 ComputeTransmittanceToTopOfAtmosphere(
    const AtmosphereParametersGpu& params, Vec3 positionKm, Vec3 direction, int sampleCount) noexcept;

// Sun transmittance (RGB) from `heightKm` above the ground to the top of the atmosphere.
// `sunUpDot` is the sun direction along local up. A ray that hits the planet gives zero.
// An invalid input or planet gives one (no air).
Vec3 ComputeSunTransmittance(
    const AtmosphereParametersGpu& params, float heightKm, float sunUpDot, int sampleCount) noexcept;

// Sun light after the atmosphere, relative to an overhead sun at the same height.
// An invalid input or a non-finite result gives the inactive record.
SunLightModulation ComputeAtmosphereSunLight(
    const AtmosphereParametersGpu& params, float heightKm, float sunUpDot) noexcept;

// Rayleigh phase function, normalized over the sphere. `cosTheta` is the cosine of the scattering angle.
float RayleighPhaseFunction(float cosTheta) noexcept;

// Cornette-Shanks Mie phase function, normalized over the sphere; `g` close to 1 is strongly forward-scattering.
float CornetteShanksMiePhaseFunction(float g, float cosTheta) noexcept;

// Decodes a transmittance LUT texel position (u, v) into the (height, upDot) pair the LUT shader used:
// heightKm = mix(0, thickness, clamp(u, 0, 1)), upDot = max(v * 2 - 1, -0.999). Mirrors AtmosphereCommon.glsl.
void TransmittanceLutUvToHeightZenith(
    const AtmosphereParametersGpu& params, float lutGridU, float lutGridV, float& outHeightKm, float& outUpDot) noexcept;

inline constexpr float kMinScaleHeightKm = 0.01f;

// Converts a scale height (km) to AtmosphereParametersGpu's reciprocal form.
// Clamps to kMinScaleHeightKm, so a divide-by-zero is impossible.
float ReciprocalScaleHeightFromKm(float scaleHeightKm) noexcept;

} // namespace gte
