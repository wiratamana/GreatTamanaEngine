#include "ScopeAggregate.h"

#include "NameTable.h"
#include "ProfilingClock.h"

#include <algorithm>

namespace gte::Profiling {

namespace {

// Index of the row for (lane, name) among rows [begin, size), or kNone.
std::size_t FindRow(const ScopeAggregateBuffer& out, std::size_t begin, ScopeLane lane, NameId name) noexcept
{
    const std::span<const ScopeAggregate> rows = out.Rows();
    for (std::size_t i = begin; i < rows.size(); ++i) {
        if (rows[i].lane == lane && rows[i].name == name) {
            return i;
        }
    }
    return ScopeAggregateBuffer::kNone;
}

bool HasTime(SampleStatus status) noexcept
{
    return status == SampleStatus::Present || status == SampleStatus::Partial;
}

std::uint64_t MillisecondsToTicks(float milliseconds) noexcept
{
    if (!(milliseconds > 0.0f)) {
        return 0;
    }
    return static_cast<std::uint64_t>(static_cast<double>(milliseconds) * kTicksPerMillisecond + 0.5);
}

// Status of a merged row after one more call. Present only while every call is clean.
SampleStatus MergeStatus(SampleStatus current, bool callClean, bool callTimed) noexcept
{
    if (current == SampleStatus::Present) {
        return callClean ? SampleStatus::Present : SampleStatus::Partial;
    }
    if (current == SampleStatus::Partial || callTimed) {
        return SampleStatus::Partial;
    }
    return current;
}

} // namespace

void AppendWorkerAggregates(std::span<const WorkerEvent> events, ScopeAggregateBuffer& out) noexcept
{
    const std::size_t begin = out.Size();
    for (const WorkerEvent& e : events) {
        const std::uint64_t ticks = e.endTicks >= e.startTicks ? e.endTicks - e.startTicks : 0;
        std::size_t index = FindRow(out, begin, ScopeLane::Workers, e.name);
        if (index == ScopeAggregateBuffer::kNone) {
            ScopeAggregate row;
            row.name = e.name;
            row.lane = ScopeLane::Workers;
            index = out.Add(row);
            if (index == ScopeAggregateBuffer::kNone) {
                continue;
            }
        }
        ScopeAggregate& row = out.At(index);
        row.totalTicks += ticks;
        row.selfTicks += ticks;
        row.maxTicks = std::max(row.maxTicks, ticks);
        ++row.calls;
    }
}

void AppendGpuAggregates(std::span<const GpuPassRow> rows, ScopeAggregateBuffer& out) noexcept
{
    static const NameId foldedName = InternName("(folded passes)");

    const std::size_t begin = out.Size();
    for (const GpuPassRow& in : rows) {
        const bool folded = in.foldedPasses > 0;
        const NameId name = folded ? foldedName : in.pass;
        if (name == kInvalidNameId) {
            continue;
        }
        const bool timed = HasTime(in.status);
        const bool clean = in.status == SampleStatus::Present && !folded;
        const std::uint64_t ticks = timed ? MillisecondsToTicks(in.barrierMs + in.workMs) : 0;

        std::size_t index = FindRow(out, begin, ScopeLane::Gpu, name);
        if (index == ScopeAggregateBuffer::kNone) {
            ScopeAggregate row;
            row.name = name;
            row.feature = folded ? kInvalidNameId : in.feature;
            row.lane = ScopeLane::Gpu;
            row.status = clean ? SampleStatus::Present : (timed ? SampleStatus::Partial : in.status);
            index = out.Add(row);
            if (index == ScopeAggregateBuffer::kNone) {
                continue;
            }
        } else {
            ScopeAggregate& row = out.At(index);
            row.status = MergeStatus(row.status, clean, timed);
        }
        ScopeAggregate& row = out.At(index);
        row.totalTicks += ticks;
        row.selfTicks += ticks;
        row.maxTicks = std::max(row.maxTicks, ticks);
        row.calls += folded ? in.foldedPasses : 1u;
    }
}

} // namespace gte::Profiling
