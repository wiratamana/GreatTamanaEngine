#pragma once

#include "../../Math/Vec3.h"

namespace gte {

// CPU reference for the per-pixel branch and final blend of AtmosphereAerialPerspectiveComposite.comp; the shader
// mirrors it. Pure functions. The volume sampling stays in the shader:
// `sampledAerialRgb`/`sampledAerialA` are the already-sampled, already-faded texel values.

// Depth at or beyond this means nothing was drawn at the pixel. Must equal the literal `0.999999` in the shader
// (GLSL cannot include this header); change both together.
constexpr float kAerialPerspectiveFarPlaneDepthThreshold = 0.999999f;

// True when no opaque geometry was drawn at the pixel (rawDepth at or beyond the threshold above). The sky
// background already produced its final color, so the composite must pass it through unchanged.
bool ShouldBypassAerialPerspectiveComposite(float rawDepth) noexcept;

// The shader's final per-pixel color: sceneColorRgb unchanged when the pixel is bypassed, otherwise
// sceneColorRgb * transmittance + inScattering, with inScattering = sampledAerialRgb * strength and
// transmittance = mix(1.0, sampledAerialA, strength).
Vec3 ComputeAerialPerspectiveCompositeColor(float rawDepth, const Vec3& sceneColorRgb,
    const Vec3& sampledAerialRgb, float sampledAerialA, float strength) noexcept;

} // namespace gte
