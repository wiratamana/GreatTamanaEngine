#include "ProfilerPanelData.h"

#include "../Profiling/NameTable.h"
#include "../Profiling/ProfilingClock.h"
#include "../Profiling/ScopeTree.h"

#include <algorithm>
#include <cstdio>

namespace gte {

std::vector<CpuScopeRow> BuildSortedCpuScopeRows(const Profiling::FrameRecord& frame)
{
    const auto events = frame.scopes.Events();

    std::vector<Profiling::ScopeTimes> times(events.size());
    Profiling::ComputeScopeTimes(events, times);

    std::vector<CpuScopeRow> rows;
    std::vector<Profiling::NameId> ids;
    for (std::size_t i = 0; i < events.size(); ++i) {
        const Profiling::ScopeEvent& event = events[i];
        if (event.kind == Profiling::ScopeKind::Marker) {
            continue;
        }

        const auto found = std::find(ids.begin(), ids.end(), event.name);
        const std::size_t row = static_cast<std::size_t>(found - ids.begin());
        if (found == ids.end()) {
            ids.push_back(event.name);
            CpuScopeRow fresh;
            fresh.name = Profiling::ResolveName(event.name);
            fresh.kind = event.kind;
            rows.push_back(fresh);
        }

        rows[row].totalMilliseconds += Profiling::TicksToMilliseconds(times[i].totalTicks);
        rows[row].selfMilliseconds += Profiling::TicksToMilliseconds(times[i].selfTicks);
        ++rows[row].callCount;
    }

    std::stable_sort(rows.begin(), rows.end(),
        [](const CpuScopeRow& a, const CpuScopeRow& b) { return a.totalMilliseconds > b.totalMilliseconds; });
    return rows;
}

std::vector<CpuTreeRow> BuildCpuTreeRows(const Profiling::FrameRecord& frame, const Profiling::FrameTotals& totals)
{
    const auto events = frame.scopes.Events();

    std::vector<Profiling::ScopeTimes> times(events.size());
    Profiling::ComputeScopeTimes(events, times);

    // Events arrive children first. A parent starts no later than its children, so sorting by start time and then
    // by depth restores tree order.
    std::vector<std::size_t> order;
    order.reserve(events.size());
    for (std::size_t i = 0; i < events.size(); ++i) {
        if (events[i].kind != Profiling::ScopeKind::Marker) {
            order.push_back(i);
        }
    }
    std::stable_sort(order.begin(), order.end(), [&events](std::size_t a, std::size_t b) {
        if (events[a].startTicks != events[b].startTicks) {
            return events[a].startTicks < events[b].startTicks;
        }
        return events[a].depth < events[b].depth;
    });

    const double frameMilliseconds = Profiling::TicksToMilliseconds(totals.frameTicks);
    const auto percentOf = [frameMilliseconds](double milliseconds) {
        return frameMilliseconds > 0.0 ? milliseconds / frameMilliseconds * 100.0 : 0.0;
    };

    std::vector<CpuTreeRow> rows;
    rows.reserve(order.size() + 1);
    for (const std::size_t i : order) {
        CpuTreeRow row;
        row.name = Profiling::ResolveName(events[i].name);
        row.kind = events[i].kind;
        row.depth = events[i].depth;
        row.totalMilliseconds = Profiling::TicksToMilliseconds(times[i].totalTicks);
        row.selfMilliseconds = Profiling::TicksToMilliseconds(times[i].selfTicks);
        row.percentOfFrame = percentOf(row.totalMilliseconds);
        rows.push_back(row);
    }

    if (totals.frameTicks > 0) {
        CpuTreeRow untracked;
        untracked.name = "(untracked)";
        untracked.untracked = true;
        untracked.totalMilliseconds = Profiling::TicksToMilliseconds(totals.untrackedTicks);
        untracked.selfMilliseconds = untracked.totalMilliseconds;
        untracked.percentOfFrame = percentOf(untracked.totalMilliseconds);
        rows.push_back(untracked);
    }
    return rows;
}

ProfilerFrameView BuildProfilerFrameView(const Profiling::FrameRecord& frame, bool captureEnabled)
{
    ProfilerFrameView view;
    view.summary = frame.summary;
    view.totals = Profiling::ComputeFrameTotals(frame);
    view.checks = Profiling::EvaluateChecks(frame, view.totals, captureEnabled);
    view.rows = BuildSortedCpuScopeRows(frame);
    view.tree = BuildCpuTreeRows(frame, view.totals);
    return view;
}

std::string FormatCheckLine(const Profiling::FrameCheck& check)
{
    const char* tag = check.status == Profiling::CheckStatus::Ok ? "OK"
        : check.status == Profiling::CheckStatus::Warn          ? "WARN"
                                                                : "FAIL";
    char buffer[192];
    if (check.value != 0) {
        std::snprintf(buffer, sizeof(buffer), "[%s] %s (%llu)", tag, check.text,
            static_cast<unsigned long long>(check.value));
    } else {
        std::snprintf(buffer, sizeof(buffer), "[%s] %s", tag, check.text);
    }
    return std::string(buffer);
}

std::string FormatBlockedLine(const Profiling::FrameTotals& totals)
{
    char buffer[160];
    std::snprintf(buffer, sizeof(buffer), "Blocked %.2f ms (GPU %.2f, workers %.2f, other %.2f)",
        Profiling::TicksToMilliseconds(totals.blockedTicks), Profiling::TicksToMilliseconds(totals.blockedGpuTicks),
        Profiling::TicksToMilliseconds(totals.blockedWorkerTicks),
        Profiling::TicksToMilliseconds(totals.blockedOtherTicks));
    return std::string(buffer);
}

std::string FormatDuration(double milliseconds)
{
    char buffer[64];
    std::snprintf(buffer, sizeof(buffer), "%.2f ms", milliseconds);
    return std::string(buffer);
}

std::string FormatFrameTimeSummary(double cpuMilliseconds)
{
    if (cpuMilliseconds <= 0.0) {
        return "N/A";
    }

    const double fps = 1000.0 / cpuMilliseconds;
    const std::string duration = FormatDuration(cpuMilliseconds);

    char buffer[96];
    std::snprintf(buffer, sizeof(buffer), "%s / %.0f FPS", duration.c_str(), fps);
    return std::string(buffer);
}

std::string FormatCount(std::uint64_t value)
{
    const std::string digits = std::to_string(value);

    std::string result;
    result.reserve(digits.size() + digits.size() / 3);

    int sinceLastSeparator = 0;
    for (auto it = digits.rbegin(); it != digits.rend(); ++it) {
        if (sinceLastSeparator != 0 && sinceLastSeparator % 3 == 0) {
            result.push_back(',');
        }
        result.push_back(*it);
        ++sinceLastSeparator;
    }

    std::reverse(result.begin(), result.end());
    return result;
}

std::string FormatGpuTimingLine(Profiling::SampleStatus status, double milliseconds)
{
    switch (status) {
    case Profiling::SampleStatus::Present:
        return FormatDuration(milliseconds);
    case Profiling::SampleStatus::Stale:
        return "stale";
    case Profiling::SampleStatus::Unsupported:
        return "unsupported";
    case Profiling::SampleStatus::Partial:
        return "partial";
    case Profiling::SampleStatus::Error:
        return "error";
    case Profiling::SampleStatus::Inactive:
        return "inactive";
    case Profiling::SampleStatus::Absent:
        break;
    }
    return "not measured";
}

const char* CpuScopeTableEmptyMessage()
{
    if (!kCpuScopeInstrumentationCompiledIn) {
        return "CPU scope instrumentation is compiled out (GTE_ENABLE_PROFILER=OFF) - "
               "no scope data will ever appear here in this build.";
    }
    return "No CPU scopes recorded yet.";
}

} // namespace gte
