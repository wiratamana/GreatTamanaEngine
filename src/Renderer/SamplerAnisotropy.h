#pragma once

#include <algorithm>

namespace gte {

// Anisotropy level to sample material textures with: 1 (off) when the feature is unsupported,
// else the device limit capped to `cap`.
inline float ChooseMaterialAnisotropy(bool featureSupported, float deviceLimit, float cap = 8.0f) noexcept
{
    return featureSupported ? std::max(1.0f, std::min(deviceLimit, cap)) : 1.0f;
}

} // namespace gte
