#pragma once

#include "../../Math/Vec3.h"

namespace gte {

// CPU reference for the per-pixel branch and final blend of AtmosphereAerialPerspectiveComposite.comp; the shader
// mirrors it. Pure functions. The volume and shaft delta sampling stay in the shader: the sampled values passed in
// are already filtered.

// Depth at or beyond this means nothing was drawn at the pixel. Must equal kClearDepthThreshold in
// src/Shaders/SceneDepthConstants.glsl.
constexpr float kAerialPerspectiveFarPlaneDepthThreshold = 0.999999f;

// True when no opaque geometry was drawn at the pixel.
bool IsClearDepth(float rawDepth) noexcept;

// Final per-pixel color. Sky: sceneColor plus shaftDeltaRgb * strength * skyIntensity, never negative, exact
// sceneColor for a zero delta, volume ignored. Geometry: sceneColor * mix(1, a, strength) + rgb * strength.
Vec3 ComputeAerialPerspectiveCompositeColor(float rawDepth, const Vec3& sceneColorRgb, const Vec3& sampledAerialRgb,
    float sampledAerialA, const Vec3& shaftDeltaRgb, float strength, float skyIntensity) noexcept;

} // namespace gte
