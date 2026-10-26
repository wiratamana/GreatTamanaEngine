#include "FrameChecks.h"

#include "NameTable.h"

#include <cstring>

namespace gte::Profiling {

namespace {

constexpr const char* kGpuWaitPrefix = "GpuWait(";
constexpr const char* kWorkerWaitName = "Wait.Workers";

enum class BlockedClass : std::uint8_t { Gpu, Worker, Other };

BlockedClass Classify(NameId name) noexcept
{
    const char* text = ResolveName(name);
    if (std::strncmp(text, kGpuWaitPrefix, std::strlen(kGpuWaitPrefix)) == 0) {
        return BlockedClass::Gpu;
    }
    if (std::strcmp(text, kWorkerWaitName) == 0) {
        return BlockedClass::Worker;
    }
    return BlockedClass::Other;
}

FrameCheck Make(CheckId id, CheckStatus status, std::uint64_t value, const char* text) noexcept
{
    return FrameCheck{ id, status, value, text };
}

} // namespace

FrameTotals ComputeFrameTotals(const FrameRecord& record) noexcept
{
    FrameTotals totals;
    totals.frameTicks = record.summary.endTicks >= record.summary.startTicks
        ? record.summary.endTicks - record.summary.startTicks
        : 0;

    const std::span<const ScopeEvent> events = record.scopes.Events();
    totals.scopeCount = static_cast<std::uint32_t>(events.size());

    for (const ScopeEvent& e : events) {
        if (e.kind == ScopeKind::Marker) {
            continue;
        }
        const std::uint64_t ticks = DurationTicks(e);
        if (e.depth == 0) {
            totals.topLevelTicks += ticks;
        }
        if (e.kind == ScopeKind::Blocked) {
            totals.blockedTicks += ticks;
            switch (Classify(e.name)) {
            case BlockedClass::Gpu: totals.blockedGpuTicks += ticks; break;
            case BlockedClass::Worker: totals.blockedWorkerTicks += ticks; break;
            case BlockedClass::Other: totals.blockedOtherTicks += ticks; break;
            }
        }
    }
    totals.untrackedTicks = totals.frameTicks > totals.topLevelTicks ? totals.frameTicks - totals.topLevelTicks : 0;

    // Only the report is wanted; a short scratch array is enough for the nesting counters.
    std::array<ScopeTimes, kMaxScopeEventsPerFrame> scratch{};
    totals.tree = ComputeScopeTimes(events, scratch);
    return totals;
}

ProfilerChecks EvaluateChecks(const FrameRecord& record, const FrameTotals& totals, bool captureEnabled) noexcept
{
    ProfilerChecks checks;
    const FrameSummary& summary = record.summary;

    checks.items[static_cast<std::size_t>(CheckId::CaptureOn)] = captureEnabled
        ? Make(CheckId::CaptureOn, CheckStatus::Ok, 0, "capture on")
        : Make(CheckId::CaptureOn, CheckStatus::Warn, 0, "capture is off, no CPU scopes are recorded");

    const std::uint32_t dropped = record.scopes.Dropped();
    checks.items[static_cast<std::size_t>(CheckId::ScopesDropped)] = dropped == 0
        ? Make(CheckId::ScopesDropped, CheckStatus::Ok, 0, "no scope dropped")
        : Make(CheckId::ScopesDropped, CheckStatus::Fail, dropped, "scope buffer full, totals are lower bounds");

    checks.items[static_cast<std::size_t>(CheckId::LateScopes)] = summary.lateScopes == 0
        ? Make(CheckId::LateScopes, CheckStatus::Ok, 0, "no late scope")
        : Make(CheckId::LateScopes, CheckStatus::Warn, summary.lateScopes, "scopes ended after their frame closed");

    checks.items[static_cast<std::size_t>(CheckId::ForeignScopes)] = summary.foreignScopes == 0
        ? Make(CheckId::ForeignScopes, CheckStatus::Ok, 0, "no off-thread scope")
        : Make(CheckId::ForeignScopes, CheckStatus::Warn, summary.foreignScopes,
              "scopes finished off the main thread were dropped");

    const std::uint64_t workerLoss = static_cast<std::uint64_t>(summary.lostWorkerEvents) + record.workers.Dropped();
    checks.items[static_cast<std::size_t>(CheckId::WorkerEvents)] = workerLoss == 0
        ? Make(CheckId::WorkerEvents, CheckStatus::Ok, 0, "no worker event lost")
        : Make(CheckId::WorkerEvents, CheckStatus::Warn, workerLoss, "worker events were lost, lanes are incomplete");

    FrameCheck reconciliation = Make(CheckId::Reconciliation, CheckStatus::Ok, 0, "scopes account for the frame");
    if (totals.frameTicks > 0) {
        const double frame = static_cast<double>(totals.frameTicks);
        const double over = static_cast<double>(totals.topLevelTicks) - frame;
        const double untrackedShare = static_cast<double>(totals.untrackedTicks) / frame;
        if (over > frame * kOverrunFailShare) {
            reconciliation = Make(CheckId::Reconciliation, CheckStatus::Fail,
                static_cast<std::uint64_t>(over / frame * 1000.0), "top-level scopes exceed the frame time");
        } else if (untrackedShare > kUntrackedWarnShare) {
            reconciliation = Make(CheckId::Reconciliation, CheckStatus::Warn,
                static_cast<std::uint64_t>(untrackedShare * 1000.0), "part of the frame is outside every scope");
        }
    }
    checks.items[static_cast<std::size_t>(CheckId::Reconciliation)] = reconciliation;

    const std::uint64_t nesting = static_cast<std::uint64_t>(totals.tree.childrenExceedParent) + totals.tree.depthClamped;
    checks.items[static_cast<std::size_t>(CheckId::ScopeNesting)] = nesting == 0
        ? Make(CheckId::ScopeNesting, CheckStatus::Ok, 0, "scope nesting is consistent")
        : Make(CheckId::ScopeNesting, CheckStatus::Warn, nesting, "children exceed their parent or nesting is too deep");

    return checks;
}

} // namespace gte::Profiling
