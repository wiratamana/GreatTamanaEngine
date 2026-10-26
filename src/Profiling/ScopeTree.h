#pragma once

#include "ScopeEvent.h"

#include <cstddef>
#include <cstdint>
#include <span>

namespace gte::Profiling {

inline constexpr std::size_t kMaxScopeDepth = 64;

struct ScopeTimes {
    std::uint64_t totalTicks = 0;
    std::uint64_t selfTicks = 0; // total minus direct children, never negative
};

struct ScopeTreeReport {
    std::uint32_t depthClamped = 0;        // events deeper than kMaxScopeDepth
    std::uint32_t childrenExceedParent = 0; // children summed to more than their parent (clock skew)
};

// One O(n) pass over post-order events (children before their parent). Writes min(events, out) entries.
// Depth is explicit, so recursion is never double counted.
ScopeTreeReport ComputeScopeTimes(std::span<const ScopeEvent> events, std::span<ScopeTimes> out) noexcept;

} // namespace gte::Profiling
