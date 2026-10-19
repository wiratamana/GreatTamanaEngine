#pragma once

#include "ShadowTypes.h"
#include "../../Game/Lighting/SunShadowReceiver.h"
#include "../../Math/Mat4.h"
#include "../../Math/Vec3.h"

#include <cstdint>
#include <optional>

namespace gte {

// World position of one depth sample through a camera's inverse view-projection.
Vec3 ReconstructWorldPositionFromDepth(
    const Mat4& invViewProjection, float ndcX, float ndcY, float depthZeroToOne) noexcept;

// Clamps every field into its valid range. A non-finite float becomes that field's default.
ShadowSettings SanitizeShadowSettings(const ShadowSettings& settings) noexcept;

// Derives eye, forward, near/far view depth and the 4 corner rays from the camera's inverse view-projection.
ShadowViewFrustum ExtractViewFrustum(const Vec3& eye, const Mat4& invViewProjection) noexcept;

// Fits up to 4 stable cascades to the camera. `safe` must be sanitized.
// With a depth range the splits cover that range, otherwise [view near, shadow distance].
ShadowCascadeSet BuildShadowCascades(const ShadowViewFrustum& frustum, const Vec3& directionTowardSun,
    const ShadowSettings& safe, std::uint32_t resolution,
    std::optional<ShadowDepthRange> sdsmRange = std::nullopt) noexcept;

// GPU parameter block of the mask pass. `safe` must be sanitized.
ShadowMaskParamsGpu MakeShadowMaskParams(
    const ShadowMapData& map, const ShadowSettings& safe, const Mat4& invViewProjection) noexcept;

// The cascade data other features sample the sun's shadow with.
// Entries past the count stay zero, except splitFar, which repeats the last split.
SunShadowReceiver MakeSunShadowReceiver(const ShadowMapData& map) noexcept;

// Sun elevation above the horizon in degrees; a non-finite direction counts as straight up.
float SunElevationDegrees(const Vec3& directionTowardSun) noexcept;

// Decodes the reduction result. Nothing when only sky was visible or the bits are not a valid range.
std::optional<ShadowDepthRange> DecodeDepthRange(const ShadowDepthRangeGpu& gpu) noexcept;

// 1 - exp(-dt / tau). tau <= 0 gives 1; otherwise a non-positive or non-finite input gives 0.
float ExponentialBlendFactor(float deltaSeconds, float timeConstantSeconds) noexcept;

// Pads, smooths (instant growth, slow shrink) and quantizes a depth-range sample.
// No sample holds the previous state; a range outside the shadow distance invalidates it.
ShadowSdsmState UpdateSdsmState(const ShadowSdsmState& prev, std::optional<ShadowDepthSample> sample,
    const ShadowViewFrustum& frustum, const ShadowSettings& safe, std::uint64_t frameIndex,
    float deltaSeconds) noexcept;

} // namespace gte
