#include "WindField.h"
#include "../Math/MathTypes.h" // kTwoPi
#include <cmath>

namespace gte {

Vec3 ComputeWindAcceleration(const WindSettings& settings, const Vec3& worldPosition, float timeSeconds) noexcept
{
    const float spatialHash = Dot(worldPosition, Vec3(12.9898f, 78.233f, 37.719f)) * 0.001f;
    const float phase = timeSeconds * settings.gustFrequency * kTwoPi + settings.seedOffset + spatialHash;
    const float gust = std::sin(phase) * settings.gustStrength;

    return Normalize(settings.direction) * (settings.baseStrength + gust);
}

} // namespace gte
