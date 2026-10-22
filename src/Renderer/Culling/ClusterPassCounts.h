#pragma once

// Pass and slot counts of the cluster path. Dependency-free: sizes the render graph timing table.

#include <cstdint>

namespace gte {

inline constexpr std::uint32_t kMainViewLanes = 1;
inline constexpr std::uint32_t kMainViewPartSlots = 8;
inline constexpr std::uint32_t kDepthOnlyLanes = 4;
inline constexpr std::uint32_t kDepthOnlyPartSlots = 4;

// Pass names per part: reset, cull, draw, stats, stats clear, stats copy. Depth-only chains have no stats.
inline constexpr std::uint32_t kMainViewPassNamesPerPart = 6;
inline constexpr std::uint32_t kDepthOnlyPassNamesPerPart = 3;
inline constexpr std::uint32_t kClusterPassNameCount = kMainViewLanes * kMainViewPartSlots * kMainViewPassNamesPerPart
    + kDepthOnlyLanes * kDepthOnlyPartSlots * kDepthOnlyPassNamesPerPart;

// Hi-Z pyramid: levels 0..kHizMaxLevel, one pass each.
inline constexpr std::uint32_t kHizMaxLevel = 5;
inline constexpr std::uint32_t kHizLevelCount = kHizMaxLevel + 1;

} // namespace gte
