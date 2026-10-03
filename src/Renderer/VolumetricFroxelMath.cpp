#include "VolumetricFroxelMath.h"

#include <algorithm>
#include <cmath>

namespace gte {

float FroxelSliceToViewDepth(float slice, float sliceCount, float maxDistanceKm, float depthExponent) noexcept
{
    const float u = std::clamp(slice / std::max(sliceCount, 1e-6f), 0.0f, 1.0f);
    return maxDistanceKm * std::pow(u, depthExponent);
}

float ViewDepthToFroxelSlice(float viewDepthKm, float sliceCount, float maxDistanceKm, float depthExponent) noexcept
{
    const float u = std::clamp(viewDepthKm / std::max(maxDistanceKm, 1e-6f), 0.0f, 1.0f);
    return std::pow(u, 1.0f / std::max(depthExponent, 1e-6f)) * sliceCount;
}

} // namespace gte
