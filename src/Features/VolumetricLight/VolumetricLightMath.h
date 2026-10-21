#pragma once

#include "VolumetricLightTypes.h"
#include "../../Core/Extent.h"
#include "../../Game/Lighting/SunShadowReceiver.h"
#include "../../Math/Mat4.h"
#include "../../Math/Vec3.h"

#include <array>
#include <cstdint>

namespace gte {

// CPU reference of the shaders. If a shader and this file disagree, the shader is wrong.
// Every function returns a finite value for NaN, infinite, zero or negative input.

// Clamps every field into its valid range. A non-finite float becomes that field's default.
VolumetricLightSettings SanitizeVolumetricLightSettings(const VolumetricLightSettings& settings) noexcept;

// Hash of the settings and the light mode that change the lit result. `safe` must be sanitized.
std::uint64_t VolumetricLightLightingKey(const VolumetricLightSettings& safe, bool atmosphereLight) noexcept;

// Slice boundary (0..sliceCount) to ray distance: maxDistance * (slice / sliceCount) ^ exponent.
float VolumetricLightSliceToDistance(float slice, float sliceCount, float maxDistance, float exponent) noexcept;

// Inverse of VolumetricLightSliceToDistance().
float VolumetricLightDistanceToSlice(float distance, float sliceCount, float maxDistance, float exponent) noexcept;

// The shadow coverage ends the fog; without a receiver the setting decides. `safe` must be sanitized.
float VolumetricLightEffectiveMaxDistance(
    const VolumetricLightSettings& safe, bool hasReceiver, float lastSplitFar) noexcept;

struct VolumetricLightCell {
    std::uint32_t x = 0;
    std::uint32_t y = 0;
    std::uint32_t z = 0;
};

// Unit world ray through screen uv (0..1).
Vec3 VolumetricLightRayDirection(float u, float v, const Mat4& invViewProjection) noexcept;

// Optical axis: the ray through the screen center.
Vec3 DeriveVolumetricLightForward(const Mat4& invViewProjection) noexcept;

// World position of a cell's sample point. `jitter` in [0,1) offsets it inside the cell.
Vec3 VolumetricLightCellToWorld(const VolumetricLightCell& cell, const Vec3& jitter, const Vec3& eye,
    const Mat4& invViewProjection, float maxDistance, float exponent) noexcept;

// Screen uv and slice boundary (0..depth) of a world point. Invalid behind the camera, off screen or past maxDistance.
struct VolumetricLightCoord {
    bool valid = false;
    float u = 0.0f;
    float v = 0.0f;
    float slice = 0.0f;
};
VolumetricLightCoord VolumetricLightWorldToCoord(const Vec3& world, const Vec3& eye, const Mat4& viewProjection,
    float maxDistance, float exponent) noexcept;

// History atlas layout.
Extent2D VolumetricLightAtlasExtent() noexcept;

struct VolumetricLightAtlasTexel {
    std::uint32_t x = 0;
    std::uint32_t y = 0;
};
VolumetricLightAtlasTexel VolumetricLightAtlasTexelOf(const VolumetricLightCell& cell) noexcept;

// Henyey-Greenstein phase: (1 - g^2) / (4 pi (1 + g^2 - 2 g cos) ^ 1.5).
float VolumetricLightPhase(float cosTheta, float g) noexcept;

// Extinction at a point: uniform plus height term, faded in from startDistance. `safe` must be sanitized.
float VolumetricLightExtinction(const VolumetricLightSettings& safe, float worldY, float distance) noexcept;

// One slice, analytic: in-scatter and transmittance of a medium with constant scattering and extinction.
struct VolumetricLightSliceIntegral {
    Vec3 inScatter;
    float transmittance = 1.0f;
};
VolumetricLightSliceIntegral VolumetricLightIntegrateSlice(
    const Vec3& scattering, float extinction, float thickness) noexcept;

// smoothstep(-2 deg, 5 deg, sun elevation). A non-finite direction counts as straight up.
float VolumetricLightSunFade(const Vec3& directionTowardSun) noexcept;

// Whether the receiver can be sampled: counts, maps and every used cascade are valid and finite.
bool VolumetricLightReceiverUsable(const SunShadowReceiver& receiver) noexcept;

// Cascade a planar view depth falls in; `blendToNext` 0 means only `index`.
struct VolumetricLightCascadePick {
    std::uint32_t index = 0;
    float blendToNext = 0.0f;
};
VolumetricLightCascadePick VolumetricLightPickCascade(float planarDepth, const SunShadowReceiver& receiver) noexcept;

// World point to cascade uv and depth. False outside the cascade or behind its plane.
bool VolumetricLightProjectToCascade(
    const Mat4& viewProjection, const Vec3& world, float& u, float& v, float& z) noexcept;

// Integrates the wind into a noise offset in world units, wrapped. Y stays 0.
std::array<double, 3> VolumetricLightAdvanceWind(
    const std::array<double, 3>& offset, float windX, float windZ, float deltaSeconds) noexcept;

// Halton (2,3,5) sample in [0,1)^3; the sequence repeats every kVolumetricLightHaltonCycle frames.
Vec3 VolumetricLightHaltonJitter(std::uint32_t frame) noexcept;

// Whether last frame's volume may be blended in.
bool VolumetricLightHistoryUsable(const VolumetricLightHistory& previous, const Vec3& eye, const Vec3& forward,
    std::uint64_t frame, std::uint64_t lightingKey) noexcept;

// History weight: `blend`, faded to 0 as the sun direction moves away from the previous frame's.
float VolumetricLightHistoryWeight(const VolumetricLightHistory& previous, const Vec3& sunDirection, float blend) noexcept;

// The record of a frame whose temporal pass ran.
VolumetricLightHistory MakeVolumetricLightHistory(const VolumetricLightFrameFacts& facts, float depthExponent,
    std::uint64_t lightingKey, const VolumetricLightHistory& previous) noexcept;

// `receiver` is null when no usable cascade data exists: the shader then sees every point as lit.
VolumetricLightInjectParamsGpu MakeVolumetricLightInjectParams(const VolumetricLightSettings& safe,
    const SunShadowReceiver* receiver, const VolumetricLightFrameFacts& facts) noexcept;

// `usable` false makes the shader ignore the history.
VolumetricLightTemporalParamsGpu MakeVolumetricLightTemporalParams(const VolumetricLightSettings& safe,
    const VolumetricLightFrameFacts& facts, const VolumetricLightHistory& previous, float weight, bool usable) noexcept;

VolumetricLightCompositePush MakeVolumetricLightCompositePush(const VolumetricLightSettings& safe,
    const Mat4& invViewProjection, const Vec3& eye, const VolumetricLightResult& result,
    std::uint64_t frameIndex) noexcept;

} // namespace gte
