#pragma once

#include "../../Math/Vec3.h"

namespace gte {

// CPU reference for the per-pixel branch and final blend of AtmosphereAerialPerspectiveComposite.comp; the shader
// mirrors it. Pure functions; texture sampling stays in the shader and filtered samples are passed in.

// Depth at or beyond this means nothing was drawn at the pixel. Must equal kClearDepthThreshold in
// src/Shaders/SceneDepthConstants.glsl.
constexpr float kAerialPerspectiveFarPlaneDepthThreshold = 0.999999f;

// Largest finite 16-bit float; the composite target is RGBA16F.
// Must equal kMaxOutputLuminance in AtmosphereAerialPerspectiveCompositeBody.glsl.
constexpr float kAerialPerspectiveMaxOutput = 65504.0f;

// True when no opaque geometry was drawn at the pixel.
bool IsClearDepth(float rawDepth) noexcept;

// Final per-pixel color. Sky adds shaftDeltaRgb * strength * skyIntensity (clamped >= 0). Geometry adds in-scatter
// scaled by mix(1, skyIntensity, aerialSkyMatch), clamped so far terrain approaches without exceeding horizonSkyRgb.
Vec3 ComputeAerialPerspectiveCompositeColor(float rawDepth, const Vec3& sceneColorRgb, const Vec3& sampledAerialRgb,
    float sampledAerialA, const Vec3& shaftDeltaRgb, float strength, float skyIntensity, float aerialSkyMatch = 1.0f,
    const Vec3& horizonSkyRgb = Vec3(
        kAerialPerspectiveMaxOutput, kAerialPerspectiveMaxOutput, kAerialPerspectiveMaxOutput)) noexcept;

} // namespace gte
