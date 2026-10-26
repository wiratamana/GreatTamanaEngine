#pragma once

// Frame totals and self-checks computed from one finished frame. Pure: no clock, no globals except name lookup.

#include "ProfilingTypes.h"
#include "ScopeTree.h"

#include <array>
#include <cstddef>
#include <cstdint>

namespace gte::Profiling {

// Top-level scopes may leave this share of the frame unaccounted before the reconciliation check warns.
inline constexpr double kUntrackedWarnShare = 0.05;

// Top-level scopes may exceed the frame by this share (clock granularity) before the check fails.
inline constexpr double kOverrunFailShare = 0.02;

struct FrameTotals {
    std::uint64_t frameTicks = 0;
    std::uint64_t topLevelTicks = 0;   // summed top-level work and blocked scopes
    std::uint64_t untrackedTicks = 0;  // frame time outside every top-level scope
    std::uint64_t blockedTicks = 0;    // every Blocked scope
    std::uint64_t blockedGpuTicks = 0; // Blocked scopes that are a named GPU wait
    std::uint64_t blockedWorkerTicks = 0;
    std::uint64_t blockedOtherTicks = 0; // Blocked scopes that are neither of the above
    std::uint32_t scopeCount = 0;
    ScopeTreeReport tree;
};

// One O(n) pass over the frame's scope events.
FrameTotals ComputeFrameTotals(const FrameRecord& record) noexcept;

enum class CheckId : std::uint8_t {
    CaptureOn,
    ScopesDropped,
    LateScopes,
    ForeignScopes,
    WorkerEvents,
    Reconciliation,
    ScopeNesting,
};
inline constexpr std::size_t kCheckCount = 7;

enum class CheckStatus : std::uint8_t { Ok, Warn, Fail };

struct FrameCheck {
    CheckId id = CheckId::CaptureOn;
    CheckStatus status = CheckStatus::Ok;
    std::uint64_t value = 0; // the count or per-mille share behind the status
    const char* text = "";   // static text, never owned
};

struct ProfilerChecks {
    std::array<FrameCheck, kCheckCount> items{};

    std::size_t IssueCount() const noexcept
    {
        std::size_t count = 0;
        for (const FrameCheck& check : items) {
            count += check.status != CheckStatus::Ok ? 1 : 0;
        }
        return count;
    }
};

// Always returns every check, in CheckId order.
ProfilerChecks EvaluateChecks(const FrameRecord& record, const FrameTotals& totals, bool captureEnabled) noexcept;

} // namespace gte::Profiling
