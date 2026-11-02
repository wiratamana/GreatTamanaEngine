#pragma once

#include <algorithm>
#include <cmath>

namespace gte {

// The step length that damping and stiffness numbers are written for.
inline constexpr float kReferenceStepSeconds = 1.0f / 60.0f;

// Converts a fraction per reference step into the fraction for one step of `stepSeconds`.
// multiplier > 1 acts stronger, 0 switches it off. NaN and negative input give 0.
inline float ScaleStepFraction(float fractionPerReferenceStep, float stepSeconds, float multiplier = 1.0f) noexcept
{
    if (!(fractionPerReferenceStep > 0.0f)) {
        return 0.0f;
    }
    const float f = std::min(fractionPerReferenceStep, 1.0f);
    if (f >= 1.0f) {
        return 1.0f;
    }
    const float exponent = (stepSeconds / kReferenceStepSeconds) * std::max(multiplier, 0.0f);
    if (exponent == 1.0f) {
        return f; // exact at the reference step.
    }
    if (!(exponent > 0.0f)) {
        return 0.0f; // zero, negative or NaN step.
    }
    return 1.0f - std::pow(1.0f - f, exponent);
}

// PMX linear damping (0..1) to a fraction per reference step, for a rate of `ratePerSecond` * damping per second.
inline float PmxDampingToStepFraction(float pmxDamping, float ratePerSecond) noexcept
{
    if (!(pmxDamping > 0.0f) || !(ratePerSecond > 0.0f)) {
        return 0.0f;
    }
    return 1.0f - std::exp(-pmxDamping * ratePerSecond * kReferenceStepSeconds);
}

} // namespace gte
