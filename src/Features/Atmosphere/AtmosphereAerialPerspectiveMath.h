#pragma once

#include "AtmosphereTypes.h"
#include "../../Game/Lighting/SunShadowReceiver.h"
#include "../../Math/Vec3.h"

#include <cstdint>

namespace gte {

// Aerial perspective numbers that follow the scene scale.
struct AerialScale {
    float maxDistanceKm = 0.5f;
    float exaggeration = 8.0f;
    float shadowBlend = 0.0f; // 0 = no air shadows, 1 = fully shadowed air.
};

// Scale for a visible depth range ending at `visibleFarKm`: 1.25x margin on a grid of 4 steps per octave from
// 0.5 km, so values inside one grid cell never change the result. Non-finite or non-positive input gives the
// small-scene values (0.5 km, 8x, no air shadows).
AerialScale ComputeAerialScale(float visibleFarKm) noexcept;

// Frames the last fitted scale stays in use while no depth-fitted cascades exist.
inline constexpr std::uint64_t kAerialScaleHoldFrames = 120;

// Per-view memory of the last scale derived from fitted cascades.
struct AerialScaleState {
    bool valid = false;
    AerialScale scale;
    std::uint64_t lastFittedFrame = 0;
};

struct AerialScaleResult {
    AerialScale scale;
    bool held = false; // The scale comes from an earlier frame.
};

// Picks this frame's scale. `receiver` is null or an already checked receiver. `safe` must be sanitized.
// Manual mode (autoScale off) uses the settings and full air shadows. Otherwise fitted cascades update `state`;
// without them the state is held for kAerialScaleHoldFrames, then the settings apply with no air shadows.
AerialScaleResult ResolveAerialScale(AerialScaleState& state, const SunShadowReceiver* receiver,
    const AtmosphereSettings& safe, std::uint64_t frameIndex) noexcept;

// Missing, Rejected or Mismatch (published sun differs from `sunDirection`), otherwise Used.
AerialReceiverState ClassifyAerialReceiver(const SunShadowReceiver* published, const Vec3& sunDirection) noexcept;

// Shadow block of the aerial volume pass. The cascade count is 0 (no shadow taps) when `receiver` is null or
// `shadowBlend` is not positive.
AerialShadowParamsGpu MakeAerialShadowParams(const SunShadowReceiver* receiver, float shadowBlend) noexcept;

} // namespace gte
