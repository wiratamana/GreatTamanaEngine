#include "AtmosphereAerialPerspectiveMath.h"

#include "AtmosphereParameters.h"

#include <algorithm>
#include <cmath>

namespace gte {

namespace {

constexpr float kMinDistanceKm = 0.5f;
constexpr float kMaxDistanceKm = 30.0f;
constexpr float kVisibleMargin = 1.25f;
constexpr float kGridStepsPerOctave = 4.0f;
constexpr float kGridEpsilon = 1e-4f;      // Keeps a value on a grid point from flipping on float error.
constexpr float kSunMatchMinDot = 0.9999f; // Receiver sun and this frame's sun must agree.
constexpr float kShadowBlendStartKm = 0.5f;
constexpr float kShadowBlendEndKm = 4.0f;
constexpr float kExaggerationBudgetKm = 4.0f;

float SmoothStep(float edge0, float edge1, float x) noexcept
{
    const float t = std::clamp((x - edge0) / (edge1 - edge0), 0.0f, 1.0f);
    return t * t * (3.0f - 2.0f * t);
}

AerialScale SettingsScale(const AtmosphereSettings& safe, float shadowBlend) noexcept
{
    return AerialScale{ safe.aerialPerspectiveMaxDistanceKm, safe.aerialPerspectiveScatteringExaggeration, shadowBlend };
}

} // namespace

AerialScale ComputeAerialScale(float visibleFarKm) noexcept
{
    if (!std::isfinite(visibleFarKm) || !(visibleFarKm > 0.0f)) {
        return AerialScale{};
    }
    const float x = visibleFarKm * kVisibleMargin;
    const float steps = std::ceil(std::log2(x / kMinDistanceKm) * kGridStepsPerOctave - kGridEpsilon);
    AerialScale scale;
    scale.maxDistanceKm =
        std::clamp(kMinDistanceKm * std::exp2(steps / kGridStepsPerOctave), kMinDistanceKm, kMaxDistanceKm);
    scale.exaggeration = std::clamp(kExaggerationBudgetKm / scale.maxDistanceKm, 1.0f, 8.0f);
    scale.shadowBlend = SmoothStep(kShadowBlendStartKm, kShadowBlendEndKm, scale.maxDistanceKm);
    return scale;
}

AerialScaleResult ResolveAerialScale(AerialScaleState& state, const SunShadowReceiver* receiver,
    const AtmosphereSettings& safe, std::uint64_t frameIndex) noexcept
{
    if (!safe.autoScale) {
        return AerialScaleResult{ SettingsScale(safe, 1.0f), false };
    }

    if (receiver != nullptr && receiver->depthFitted && receiver->cascadeCount > 0) {
        const std::uint32_t last = std::min(receiver->cascadeCount, kSunShadowReceiverMaxCascades) - 1;
        state.scale = ComputeAerialScale(receiver->splitFar[last] / kWorldUnitsPerKilometer);
        state.lastFittedFrame = frameIndex;
        state.valid = true;
        return AerialScaleResult{ state.scale, false };
    }

    // A restarted frame counter drops the state like an expired one.
    const bool fresh = state.valid && frameIndex >= state.lastFittedFrame
        && frameIndex - state.lastFittedFrame <= kAerialScaleHoldFrames;
    if (fresh) {
        return AerialScaleResult{ state.scale, true };
    }
    state = AerialScaleState{};
    return AerialScaleResult{ SettingsScale(safe, 0.0f), false };
}

AerialReceiverState ClassifyAerialReceiver(const SunShadowReceiver* published, const Vec3& sunDirection) noexcept
{
    if (published == nullptr) {
        return AerialReceiverState::Missing;
    }
    if (!SunShadowReceiverUsable(*published)) {
        return AerialReceiverState::Rejected;
    }
    return Dot(published->directionTowardSun, sunDirection) >= kSunMatchMinDot ? AerialReceiverState::Used
                                                                                : AerialReceiverState::Mismatch;
}

AerialShadowParamsGpu MakeAerialShadowParams(const SunShadowReceiver* receiver, float shadowBlend) noexcept
{
    AerialShadowParamsGpu p;
    p.counts[1] = kAerialShadowTaps;
    p.eyeAndUnitsPerKm[3] = kWorldUnitsPerKilometer;
    p.forwardAndBias[3] = kAerialShadowBiasTexels;
    p.shadowParams[0] = kAerialShadowFilterTexels;
    if (receiver == nullptr || !(shadowBlend > 0.0f)) {
        return p;
    }

    const std::uint32_t count = std::min(receiver->cascadeCount, kSunShadowReceiverMaxCascades);
    for (std::uint32_t c = 0; c < count; ++c) {
        p.cascadeViewProjection[c] = receiver->viewProjection[c];
        p.cascadeSplitFar[c] = receiver->splitFar[c];
        p.cascadeBlendWidth[c] = receiver->blendWidth[c];
        p.cascadeTexelWorld[c] = receiver->texelWorld[c];
        p.cascadeDepthRange[c] = receiver->depthRange[c];
    }
    for (std::uint32_t c = count; c < kSunShadowReceiverMaxCascades && count > 0; ++c) {
        p.cascadeSplitFar[c] = receiver->splitFar[count - 1];
    }
    p.eyeAndUnitsPerKm[0] = receiver->cameraPosition.x;
    p.eyeAndUnitsPerKm[1] = receiver->cameraPosition.y;
    p.eyeAndUnitsPerKm[2] = receiver->cameraPosition.z;
    p.forwardAndBias[0] = receiver->cameraForward.x;
    p.forwardAndBias[1] = receiver->cameraForward.y;
    p.forwardAndBias[2] = receiver->cameraForward.z;
    p.shadowParams[1] = receiver->resolution > 0 ? 1.0f / static_cast<float>(receiver->resolution) : 0.0f;
    p.shadowParams[2] = std::min(shadowBlend, 1.0f);
    p.counts[0] = count;
    return p;
}

} // namespace gte
