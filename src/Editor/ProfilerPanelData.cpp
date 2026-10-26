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
