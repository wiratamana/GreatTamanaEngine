#include "ProfilerSnapshotBuilder.h"

#include "FrameChecks.h"
#include "NameTable.h"
#include "ProfilingClock.h"
#include "ScopeTree.h"

#include <algorithm>
#include <cstring>
#include <vector>

namespace gte::Profiling {

namespace {

Metric Milliseconds(double value, MetricKind kind = MetricKind::Measured) noexcept
{
    return PresentMetric(value, Unit::Milliseconds, kind);
}

Metric TicksAsMilliseconds(std::uint64_t ticks, MetricKind kind = MetricKind::Measured) noexcept
{
    return Milliseconds(TicksToMilliseconds(ticks), kind);
}

Metric Count(std::uint64_t value, MetricKind kind = MetricKind::Measured) noexcept
{
    return PresentMetric(static_cast<double>(value), Unit::Count, kind);
}

Metric CountOf(std::uint64_t value, SampleStatus status, Reason reason = Reason::None) noexcept
{
    Metric metric = Count(value);
    metric.status = status;
    metric.reason = reason;
    return metric;
}

bool IsAbsentGpu(const GpuViewMetric& metric) noexcept
{
    return metric.status == SampleStatus::Absent;
}

// Accumulates one name's top-level scopes.
struct ScopeAccumulator {
    NameId name = kInvalidNameId;
    std::uint64_t totalTicks = 0;
    std::uint64_t selfTicks = 0;
    std::uint32_t calls = 0;
};

void FillScopeRows(ProfilerSnapshot& snapshot, const FrameRecord& record)
{
    const std::span<const ScopeEvent> events = record.scopes.Events();
    std::vector<ScopeTimes> times(events.size());
    ComputeScopeTimes(events, times);

    std::array<ScopeAccumulator, kSnapshotScopeRows> accumulators{};
    std::size_t used = 0;
    std::uint32_t dropped = 0;

    for (std::size_t i = 0; i < events.size(); ++i) {
        const ScopeEvent& event = events[i];
        if (event.depth != 0 || event.kind == ScopeKind::Marker) {
            continue;
        }
        ScopeAccumulator* row = nullptr;
        for (std::size_t r = 0; r < used; ++r) {
            if (accumulators[r].name == event.name) {
                row = &accumulators[r];
                break;
            }
        }
        if (row == nullptr) {
            if (used == accumulators.size()) {
                ++dropped;
                continue;
            }
            row = &accumulators[used++];
            row->name = event.name;
        }
        row->totalTicks += times[i].totalTicks;
        row->selfTicks += times[i].selfTicks;
        ++row->calls;
    }

    std::sort(accumulators.begin(), accumulators.begin() + static_cast<std::ptrdiff_t>(used),
        [](const ScopeAccumulator& a, const ScopeAccumulator& b) { return a.totalTicks > b.totalTicks; });

    const bool partial = record.scopes.Partial();
    for (std::size_t r = 0; r < used; ++r) {
        SnapshotScopeRow& row = snapshot.scopes[r];
        CopySnapshotText(row.name, ResolveName(accumulators[r].name));
        row.total = TicksAsMilliseconds(accumulators[r].totalTicks);
        row.self = TicksAsMilliseconds(accumulators[r].selfTicks, MetricKind::Derived);
        row.calls = Count(accumulators[r].calls);
        if (partial) {
            row.total.status = SampleStatus::Partial;
            row.total.reason = Reason::Dropped;
            row.self.status = SampleStatus::Partial;
            row.self.reason = Reason::Dropped;
        }
    }
    snapshot.scopeCount = static_cast<std::uint32_t>(used);
    snapshot.scopeRowsDropped = dropped;
}

void FillFrameRows(ProfilerSnapshot& snapshot, const FrameProfiler& profiler)
{
    const std::size_t total = profiler.HistoryCount();
    if (total == 0) {
        return;
    }
    const std::size_t count = std::min(total, kSnapshotFrameRows);
    const std::uint64_t newest = profiler.HistoryAt(total - 1).frameIndex;

    for (std::size_t i = 0; i < count; ++i) {
        const FrameSummary& summary = profiler.HistoryAt(total - count + i);
        SnapshotFrameRow& row = snapshot.frames[i];
        const std::uint32_t age = static_cast<std::uint32_t>(newest - summary.frameIndex);
        row.frameIndex = summary.frameIndex;
        row.cpu = Milliseconds(summary.cpuMilliseconds);
        row.cpu.ageFrames = age;
        row.gpuSpan = MetricFromGpuView(summary.gpu.span);
        row.gpuPrimary = MetricFromGpuView(summary.gpu.views[static_cast<std::size_t>(ViewSlot::Primary)]);
        row.gpuSecondary = MetricFromGpuView(summary.gpu.views[static_cast<std::size_t>(ViewSlot::Secondary)]);
        row.gpuSpan.ageFrames = age;
        row.gpuPrimary.ageFrames = age;
        row.gpuSecondary.ageFrames = age;
    }
    snapshot.frameCount = static_cast<std::uint32_t>(count);
}

} // namespace

std::size_t CopySnapshotText(std::span<char> out, std::string_view text) noexcept
{
    if (out.empty()) {
        return 0;
    }
    const std::size_t length = std::min(text.size(), out.size() - 1);
    if (length > 0) {
        std::memcpy(out.data(), text.data(), length);
    }
    out[length] = '\0';
    return length;
}

std::string_view SnapshotText(std::span<const char> text) noexcept
{
    std::size_t length = 0;
    while (length < text.size() && text[length] != '\0') {
        ++length;
    }
    return std::string_view(text.data(), length);
}

void FillLiveBlocks(ProfilerSnapshot& snapshot, const LiveBlockTable& table, std::uint64_t nowMicroseconds) noexcept
{
    std::array<LiveBlockEntry, kMaxLiveBlockThreads> entries{};
    const std::size_t count = table.Read(nowMicroseconds, entries);
    for (std::size_t i = 0; i < count; ++i) {
        SnapshotLiveBlock& block = snapshot.liveBlocks[i];
        block.threadSlot = entries[i].threadSlot;
        CopySnapshotText(block.site, entries[i].site);
        CopySnapshotText(block.detail, entries[i].detail);
        block.elapsed = Milliseconds(static_cast<double>(entries[i].elapsedMicroseconds) / 1000.0);
    }
    snapshot.liveBlockCount = static_cast<std::uint32_t>(count);
}

ProfilerSnapshot BuildProfilerSnapshot(const FrameProfiler& profiler, const SnapshotInputs& inputs)
{
    ProfilerSnapshot snapshot;
    const bool capture = profiler.IsCaptureEnabled();
    const bool haveFrame = profiler.HistoryCount() > 0;
    snapshot.captureOn = capture;
    snapshot.displayPaused = profiler.IsDisplayPaused();
    snapshot.hasFrame = haveFrame;
    CopySnapshotText(snapshot.verdict, "unavailable");

    snapshot.device.deviceLost = inputs.deviceLost;
    snapshot.device.consecutiveFrameTimeouts = Count(inputs.consecutiveFrameTimeouts);
    snapshot.device.abandonedObjects = Count(inputs.abandonedObjects);

    const FrameRecord& record = profiler.LastCompletedFrame();
    const FrameTotals totals = ComputeFrameTotals(record);
    const ProfilerChecks checks = EvaluateChecks(record, totals, capture);
    for (std::size_t i = 0; i < kCheckCount; ++i) {
        const FrameCheck& check = checks.items[i];
        snapshot.checks[i] = SnapshotCheck{ check.id, check.status, Count(check.value), check.text };
    }

    FillFrameRows(snapshot, profiler);

    // Without a live frame every measured number says why it is missing.
    const Reason missing = capture ? Reason::NotReady : Reason::CaptureOff;
    const auto absentMs = MissingMetric(Unit::Milliseconds, SampleStatus::Absent, missing);
    const auto absentCount = MissingMetric(Unit::Count, SampleStatus::Absent, missing);
    snapshot.cpuFrame = absentMs;
    snapshot.untracked = absentMs;
    snapshot.blockedGpu = absentMs;
    snapshot.blockedWorkers = absentMs;
    snapshot.blockedOther = absentMs;
    snapshot.gpuSpan = absentMs;
    snapshot.gpuView.fill(absentMs);
    snapshot.indirectDraws.fill(absentCount);
    snapshot.gpuCoverage = SnapshotGpuCoverage{ absentCount, absentCount, absentCount, absentCount };
    snapshot.memoryTotal = MissingMetric(Unit::Bytes, SampleStatus::Absent, missing);
    snapshot.drops = SnapshotDrops{ absentCount, absentCount, absentCount, absentCount, absentCount };

    if (!haveFrame || !capture) {
        return snapshot;
    }

    const FrameSummary& summary = record.summary;
    snapshot.frameIndex = summary.frameIndex;
    snapshot.cpuFrame = Milliseconds(summary.cpuMilliseconds);
    snapshot.untracked = TicksAsMilliseconds(totals.untrackedTicks, MetricKind::Derived);
    snapshot.blockedGpu = TicksAsMilliseconds(totals.blockedGpuTicks);
    snapshot.blockedWorkers = TicksAsMilliseconds(totals.blockedWorkerTicks);
    snapshot.blockedOther = TicksAsMilliseconds(totals.blockedOtherTicks);

    const GpuFrameMetrics& gpu = summary.gpu;
    bool gpuKnown = !IsAbsentGpu(gpu.span);
    snapshot.gpuSpan = MetricFromGpuView(gpu.span);
    for (std::size_t i = 0; i < kViewSlotCount; ++i) {
        snapshot.gpuView[i] = MetricFromGpuView(gpu.views[i]);
        gpuKnown = gpuKnown || !IsAbsentGpu(gpu.views[i]);

        const SampleStatus viewStatus = gpu.views[i].status;
        const bool counted = viewStatus == SampleStatus::Present || viewStatus == SampleStatus::Partial;
        snapshot.indirectDraws[i] = CountOf(gpu.indirectDraws[i], counted ? SampleStatus::Present : viewStatus);
    }

    const SampleStatus coverageStatus = gpuKnown ? SampleStatus::Present : SampleStatus::Absent;
    snapshot.gpuCoverage.declared = CountOf(gpu.declaredPasses, coverageStatus);
    snapshot.gpuCoverage.executed = CountOf(gpu.executedPasses, coverageStatus);
    snapshot.gpuCoverage.recorded = CountOf(gpu.recordedPasses, coverageStatus);
    snapshot.gpuCoverage.disabledByToggle = CountOf(gpu.disabledByToggle, coverageStatus);

    snapshot.memoryTotal = PresentMetric(static_cast<double>(summary.memory.totalBytes), Unit::Bytes);
    snapshot.memoryTotal.status = summary.memory.status;

    snapshot.drops.lateScopes = Count(summary.lateScopes);
    snapshot.drops.foreignScopes = Count(summary.foreignScopes);
    snapshot.drops.lostWorkerEvents = Count(summary.lostWorkerEvents);
    snapshot.drops.droppedScopes = Count(record.scopes.Dropped());
    snapshot.drops.droppedGpuFrames = CountOf(gpu.droppedFrames, coverageStatus);

    FillScopeRows(snapshot, record);
    return snapshot;
}

} // namespace gte::Profiling
