#include "FixedTimestepAccumulator.h"

#include <algorithm>

namespace gte {

int ComputeFixedStepCount(float& accumulatedSeconds, float frameDeltaSeconds, float fixedTimestep, int maxStepsPerFrame) noexcept
{
    if (fixedTimestep <= 0.0f) {
        return 0;
    }
    accumulatedSeconds += frameDeltaSeconds;

    int steps = 0;
    while (accumulatedSeconds >= fixedTimestep && steps < maxStepsPerFrame) {
        accumulatedSeconds -= fixedTimestep;
        ++steps;
    }
    // Spiral-of-death guard: never let unboundedly-large leftover time keep
    // demanding more steps next frame either.
    if (steps == maxStepsPerFrame) {
        accumulatedSeconds = std::min(accumulatedSeconds, fixedTimestep);
    }
    return steps;
}

} // namespace gte
