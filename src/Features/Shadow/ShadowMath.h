#pragma once

#include "ShadowTypes.h"
#include "../../Game/Lighting/SunShadowReceiver.h"
#include "../../Math/Mat4.h"
#include "../../Math/Vec3.h"

#include <algorithm>
#include <cstdint>
#include <optional>

namespace gte {

// World position of one depth sample through a camera's inverse view-projection.
Vec3 ReconstructWorldPositionFromDepth(
    const Mat4& invViewProjection, float ndcX, float ndcY, float depthZeroToOne) noexcept;

// Clamps every field into its valid range. A non-finite float becomes that field's default.
ShadowSettings SanitizeShadowSettings(const ShadowSettings& settings) noexcept;

// Map texels per view pixel at the nearest cascade, and the smallest map size.
inline constexpr std::uint32_t kShadowTexelsPerViewPixel = 2;
inline constexpr std::uint32_t kMinShadowResolution = 512;

// Frames a new target size must stay the target before the maps are resized (a resize is a GPU idle wait).
inline constexpr std::uint32_t kShadowResolutionHoldFrames = 30;

// Map size for a view: texels per view pixel, rounded up to a power of two, clamped to [min, maxResolution].
constexpr std::uint32_t EffectiveShadowResolution(
    std::uint32_t maxResolution, std::uint32_t viewWidth, std::uint32_t viewHeight) noexcept
{
    const std::uint64_t want = std::uint64_t{ std::max(viewWidth, viewHeight) } * kShadowTexelsPerViewPixel;
    std::uint32_t size = kMinShadowResolution;
    while (size < want && size < maxResolution) {
        size <<= 1;
    }
    return std::min(size, maxResolution);
}

// Per-view map size with hysteresis. current == 0 means no maps exist yet.
struct ShadowResolutionState {
    std::uint32_t current = 0;
    std::uint32_t pending = 0;    // Target that differs from `current`.
    std::uint32_t heldFrames = 0; // Consecutive frames `pending` was the target.
};

// Advances one frame and returns the size to request. The first size applies at once.
constexpr std::uint32_t StepShadowResolution(ShadowResolutionState& state, std::uint32_t target) noexcept
{
    if (state.current == 0 || target == state.current) {
        state.current = state.current == 0 ? target : state.current;
        state.pending = 0;
        state.heldFrames = 0;
        return state.current;
    }
    state.heldFrames = target == state.pending ? state.heldFrames + 1 : 1;
    state.pending = target;
    if (state.heldFrames >= kShadowResolutionHoldFrames) {
        state.current = target;
        state.pending = 0;
        state.heldFrames = 0;
    }
    return state.current;
}

// The resize to `refused` failed: go back to `previous` and retry after a full hold period.
constexpr void RefuseShadowResolution(ShadowResolutionState& state, std::uint32_t previous) noexcept
{
    if (previous == 0) {
        state = ShadowResolutionState{};
        return;
    }
    state.pending = state.current;
    state.current = previous;
    state.heldFrames = 0;
}

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

// The cascade data other features sample the sun's shadow with. `safe` must be sanitized.
// Entries past the count stay zero, except splitFar, which repeats the last split.
SunShadowReceiver MakeSunShadowReceiver(const ShadowMapData& map, const ShadowSettings& safe = ShadowSettings{}) noexcept;

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
