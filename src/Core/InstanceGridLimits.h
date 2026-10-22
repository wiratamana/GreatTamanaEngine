#pragma once

#include <cstdint>

namespace gte {

// Bounds of the instance-grid spawn tool (UI menu and HTTP route share them).
inline constexpr std::uint32_t kMaxSpawnGridInstances = 100000;
inline constexpr std::uint32_t kDefaultSpawnGridInstances = 6;
inline constexpr float kMinSpawnGridSpacing = 0.1f;
inline constexpr float kMaxSpawnGridSpacing = 1000.0f;
inline constexpr float kDefaultSpawnGridSpacing = 1.5f;

} // namespace gte
