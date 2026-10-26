#include "ScopeTree.h"

#include <algorithm>
#include <array>

namespace gte::Profiling {

ScopeTreeReport ComputeScopeTimes(std::span<const ScopeEvent> events, std::span<ScopeTimes> out) noexcept
{
    ScopeTreeReport report;
    // childSum[d] = summed totals of finished events at depth d that no parent has claimed yet.
    std::array<std::uint64_t, kMaxScopeDepth + 1> childSum{};

    const std::size_t count = std::min(events.size(), out.size());
    for (std::size_t i = 0; i < count; ++i) {
        const ScopeEvent& e = events[i];
        std::size_t depth = e.depth;
        if (depth >= kMaxScopeDepth) {
            depth = kMaxScopeDepth - 1;
            ++report.depthClamped;
        }

        const std::uint64_t total = DurationTicks(e);
        const std::uint64_t children = childSum[depth + 1];
        if (children > total) {
            ++report.childrenExceedParent;
        }

        out[i].totalTicks = total;
        out[i].selfTicks = children > total ? 0 : total - children;

        childSum[depth + 1] = 0;
        childSum[depth] += total;
    }
    return report;
}

} // namespace gte::Profiling
