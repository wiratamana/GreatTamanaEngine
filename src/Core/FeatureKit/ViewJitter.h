#pragma once

#include "../../Math/Mat4.h"
#include "../../Math/Vec2.h"

namespace gte::fx {

// Largest accepted jitter, in NDC (a quarter of the screen width).
inline constexpr float kMaxViewJitterNdc = 0.5f;

// True when both parts are finite and within kMaxViewJitterNdc.
bool IsValidViewJitter(Vec2 jitterNdc) noexcept;

// Clip-space shift: clip.xy += jitterNdc * clip.w. A zero jitter returns `viewProjection` unchanged.
Mat4 ApplyClipJitter(const Mat4& viewProjection, Vec2 jitterNdc) noexcept;

} // namespace gte::fx
