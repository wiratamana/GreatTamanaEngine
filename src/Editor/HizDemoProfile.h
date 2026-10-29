#pragma once

#include <algorithm>
#include <array>
#include <string_view>

namespace gte {

// Fixed-cost feature name prefixes the "geometry_only" profile turns off.
// Scripts read them from GET /hiz_demo/geometry_only_features.
inline constexpr std::array<std::string_view, 6> kGeometryOnlyFeaturePrefixes{
    "Shadow", "VolumetricLight", "Atmosphere", "Ssgi", "Gtao", "ToneMapping" };

// True when `featureName` starts with one of kGeometryOnlyFeaturePrefixes.
inline constexpr bool IsGeometryOnlyFeature(std::string_view featureName) noexcept
{
    return std::any_of(kGeometryOnlyFeaturePrefixes.begin(), kGeometryOnlyFeaturePrefixes.end(),
        [featureName](std::string_view prefix) { return featureName.starts_with(prefix); });
}

} // namespace gte
