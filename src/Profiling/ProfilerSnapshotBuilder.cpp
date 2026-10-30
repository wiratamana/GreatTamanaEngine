#include "ProfilerSnapshotBuilder.h"

#include "FrameChecks.h"
#include "FrameGraphData.h"
#include "NameTable.h"
#include "Overhead.h"
#include "ProfilingClock.h"
#include "ScopeSource.h"
#include "ScopeTable.h"
#include "ScopeTree.h"

#include <algorithm>
#include <array>
#include <charconv>
#include <cstring>
#include <memory>
#include <string_view>
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

// One name at one depth, summed over its calls in the frame.
struct ScopeAccumulator {
    NameId name = kInvalidNameId;
    std::uint16_t depth = 0;
    std::uint64_t totalTicks = 0;
    std::uint64_t selfTicks = 0;
    std::uint32_t calls = 0;
};

// Sums the non-marker scopes at depth 0 (`topLevel`) or deeper. Same name and depth merge; largest total first.
std::vector<ScopeAccumulator> SumScopes(
    std::span<const ScopeEvent> events, std::span<const ScopeTimes> times, bool topLevel)
{
    std::vector<std::uint32_t> picked;
    for (std::size_t i = 0; i < events.size(); ++i) {
        if (events[i].kind != ScopeKind::Marker && (events[i].depth == 0) == topLevel) {
            picked.push_back(static_cast<std::uint32_t>(i));
        }
    }
    std::sort(picked.begin(), picked.end(), [&](std::uint32_t a, std::uint32_t b) {
        return events[a].name != events[b].name ? events[a].name < events[b].name : events[a].depth < events[b].depth;
    });

    std::vector<ScopeAccumulator> sums;
    for (const std::uint32_t index : picked) {
        const ScopeEvent& event = events[index];
        if (sums.empty() || sums.back().name != event.name || sums.back().depth != event.depth) {
            sums.push_back(ScopeAccumulator{ event.name, event.depth, 0, 0, 0 });
        }
        sums.back().totalTicks += times[index].totalTicks;
        sums.back().selfTicks += times[index].selfTicks;
        ++sums.back().calls;
    }
    std::sort(sums.begin(), sums.end(), [](const ScopeAccumulator& a, const ScopeAccumulator& b) {
        if (a.totalTicks != b.totalTicks) {
            return a.totalTicks > b.totalTicks;
        }
        return a.name != b.name ? a.name < b.name : a.depth < b.depth;
    });
    return sums;
}

template <typename Row>
void FillScopeRow(Row& row, const ScopeAccumulator& sum, bool partial)
{
    CopySnapshotText(row.name, ResolveName(sum.name));
    row.total = TicksAsMilliseconds(sum.totalTicks);
    row.self = TicksAsMilliseconds(sum.selfTicks, MetricKind::Derived);
    row.calls = Count(sum.calls);
    if (partial) {
        row.total.status = SampleStatus::Partial;
        row.total.reason = Reason::Dropped;
        row.self.status = SampleStatus::Partial;
        row.self.reason = Reason::Dropped;
    }
}

void FillScopeRows(ProfilerSnapshot& snapshot, const FrameRecord& record)
{
    const std::span<const ScopeEvent> events = record.scopes.Events();
    std::vector<ScopeTimes> times(events.size());
    ComputeScopeTimes(events, times);
    const bool partial = record.scopes.Partial();

    const std::vector<ScopeAccumulator> top = SumScopes(events, times, true);
    const std::size_t topUsed = std::min(top.size(), snapshot.scopes.size());
    for (std::size_t r = 0; r < topUsed; ++r) {
        FillScopeRow(snapshot.scopes[r], top[r], partial);
    }
    snapshot.scopeCount = static_cast<std::uint32_t>(topUsed);
    snapshot.scopeRowsDropped = static_cast<std::uint32_t>(top.size() - topUsed);

    const std::vector<ScopeAccumulator> nested = SumScopes(events, times, false);
    const std::size_t nestedUsed = std::min(nested.size(), snapshot.nested.size());
    for (std::size_t r = 0; r < nestedUsed; ++r) {
        FillScopeRow(snapshot.nested[r], nested[r], partial);
        snapshot.nested[r].depth = nested[r].depth;
    }
    snapshot.nestedCount = static_cast<std::uint32_t>(nestedUsed);
    snapshot.nestedRowsDropped = static_cast<std::uint32_t>(nested.size() - nestedUsed);
}

// "File.cpp:123" of the site that first wrote `name`; empty when unknown.
void FormatSourceText(std::span<char> out, NameId name) noexcept
{
    out[0] = '\0';
    const ScopeSource source = SourceOf(name);
    if (source.fileName == kInvalidNameId) {
        return;
    }
    std::array<char, 16> digits{};
    const auto converted = std::to_chars(digits.data(), digits.data() + digits.size(), source.line);
    const std::size_t used = CopySnapshotText(out, ResolveName(source.fileName));
    if (used + 1 < out.size()) {
        out[used] = ':';
        CopySnapshotText(out.subspan(used + 1), std::string_view(digits.data(), static_cast<std::size_t>(converted.ptr - digits.data())));
    }
}

void FillTableRow(SnapshotTableRow& out, const ScopeTableRow& in, double frameMs, double gpuMs)
{
    CopySnapshotText(out.name, ResolveName(in.name));
    CopySnapshotText(out.feature, in.feature == kInvalidNameId ? std::string_view() : std::string_view(ResolveName(in.feature)));
    FormatSourceText(out.source, in.name);
    out.lane = static_cast<std::uint8_t>(in.lane);
    out.origin = static_cast<std::uint8_t>(SourceOf(in.name).origin);
    out.kind = in.kind;
    out.status = in.status;
    out.hasChildren = in.hasChildren;
    out.depth = in.depth;
    out.calls = in.calls;
    out.totalMs = TicksToMilliseconds(in.totalTicks);
    out.selfMs = TicksToMilliseconds(in.selfTicks);
    out.maxMs = TicksToMilliseconds(in.maxTicks);

    const bool gpu = in.lane == ScopeLane::Gpu;
    const double base = gpu ? gpuMs : frameMs;
    const bool measured = !gpu || in.status == SampleStatus::Present || in.status == SampleStatus::Partial;
    out.percentKnown = measured && base > 0.0;
    out.percent = out.percentKnown ? out.totalMs / base * 100.0 : 0.0;
}

void CopyTable(std::array<SnapshotTableRow, kSnapshotTableRows>& rows, std::uint32_t& count, std::uint32_t& dropped,
    const ScopeTable& table, double frameMs, double gpuMs)
{
    const std::size_t used = std::min(table.rows.size(), rows.size());
    for (std::size_t i = 0; i < used; ++i) {
        FillTableRow(rows[i], table.rows[i], frameMs, gpuMs);
    }
    count = static_cast<std::uint32_t>(used);
    dropped = table.rowsDropped + static_cast<std::uint32_t>(table.rows.size() - used);
}

void FillTables(ProfilerSnapshot& snapshot, const FrameRecord& record)
{
    auto tables = std::make_shared<SnapshotTables>();
    const double frameMs = record.summary.cpuMilliseconds;
    const double gpuMs = record.summary.gpu.span.status == SampleStatus::Present
        ? static_cast<double>(record.summary.gpu.span.milliseconds)
        : 0.0;

    const ScopeTable tree = BuildScopeTree(record, ScopeSort::CallOrder, kSnapshotTableRows);
    CopyTable(tables->tree, tables->treeCount, tables->treeDropped, tree, frameMs, gpuMs);
    tables->treeTruncated = tree.treeTruncated;

    const ScopeTable flat = BuildScopeFlat(record, ScopeSort::TotalDesc, kSnapshotTableRows);
    CopyTable(tables->flat, tables->flatCount, tables->flatDropped, flat, frameMs, gpuMs);
    snapshot.tables = std::move(tables);
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
        row.cpuBusy = Milliseconds(CpuBusyMilliseconds(summary), MetricKind::Derived);
        row.cpuBusy.ageFrames = age;
        row.gpuSpan = MetricFromGpuView(summary.gpu.span);
        row.gpuPrimary = MetricFromGpuView(summary.gpu.views[static_cast<std::size_t>(ViewSlot::Primary)]);
        row.gpuSecondary = MetricFromGpuView(summary.gpu.views[static_cast<std::size_t>(ViewSlot::Secondary)]);
        row.gpuSpan.ageFrames = age;
        row.gpuPrimary.ageFrames = age;
        row.gpuSecondary.ageFrames = age;
    }
    snapshot.frameCount = static_cast<std::uint32_t>(count);
}

// The overrun rule of the reconciliation check, applied to a stored summary.
bool SummaryChecksFail(const FrameSummary& summary) noexcept
{
    const std::uint64_t frame = summary.endTicks >= summary.startTicks ? summary.endTicks - summary.startTicks : 0;
    return frame > 0 && static_cast<double>(summary.topLevelTicks) > static_cast<double>(frame) * (1.0 + kOverrunFailShare);
}

const char* NameText(NameId id) noexcept
{
    return id == kInvalidNameId ? "" : ResolveName(id);
}

// Verdict over the newest frames. `newestChecksFail` carries the full self-check of the newest frame.
void FillVerdict(ProfilerSnapshot& snapshot, const FrameProfiler& profiler, bool capture, bool newestChecksFail)
{
    const Verdict verdict = VerdictFromHistory(profiler, capture, newestChecksFail);
    FormatVerdict(verdict, snapshot.verdict);

    SnapshotVerdictDetail& detail = snapshot.verdictDetail;
    detail.kind = verdict.kind;
    detail.frames = Count(verdict.frames);
    if (verdict.kind == VerdictKind::Unknown) {
        const Reason why = capture ? Reason::NotReady : Reason::CaptureOff;
        const Metric none = MissingMetric(Unit::Milliseconds, SampleStatus::Absent, why);
        detail.frame = none;
        detail.cpuWork = none;
        detail.gpuWait = none;
        detail.presentWait = none;
        detail.otherWait = none;
        detail.untracked = none;
        detail.stallShare = MissingMetric(Unit::Percent, SampleStatus::Absent, why);
        return;
    }
    const VerdictFrame& m = verdict.median;
    detail.frame = Milliseconds(m.frameMs, MetricKind::Derived);
    detail.cpuWork = Milliseconds(m.cpuWorkMs, MetricKind::Derived);
    detail.gpuWait = Milliseconds(m.blockedGpuMs, MetricKind::Derived);
    detail.presentWait = Milliseconds(m.blockedPresentMs, MetricKind::Derived);
    detail.otherWait = Milliseconds(m.blockedOtherMs, MetricKind::Derived);
    detail.untracked = Milliseconds(m.untrackedMs, MetricKind::Derived);
    detail.stallShare = PresentMetric(verdict.stallShare * 100.0, Unit::Percent, MetricKind::Derived);
    CopySnapshotText(detail.stallSite, NameText(verdict.stallSite));
}

static_assert(kSnapshotSpikeRows == SpikeStore::kSlots, "snapshot rows must hold every frozen spike");

void FillSpikes(ProfilerSnapshot& snapshot, const FrameProfiler& profiler)
{
    snapshot.spikesAvailable = true;
    std::array<SpikeStore::Row, kSnapshotSpikeRows> rows{};
    const std::size_t count = profiler.Spikes().Rows(rows);
    for (std::size_t i = 0; i < count; ++i) {
        const SpikeStore::Row& source = rows[i];
        SnapshotSpikeRow& row = snapshot.spikes[i];
        row.frameIndex = source.frameIndex;
        row.cpu = Milliseconds(source.frameMs);
        row.median = Milliseconds(source.medianMs, MetricKind::Derived);
        row.excess = Milliseconds(std::max(0.0, source.frameMs - source.medianMs), MetricKind::Derived);
        row.dominantSelf = Milliseconds(source.classification.dominantSelfMs, MetricKind::Derived);
        row.gpuSpan = MetricFromGpuView(source.gpu.span);
        CopySnapshotText(row.classification, ToString(source.classification.kind));
        CopySnapshotText(row.cause, NameText(source.classification.dominant));
        row.markerPresent = source.classification.markerPresent;
        row.pinned = source.pinned;
        row.gpuAttached = source.gpuAttached;
    }
    snapshot.spikeCount = static_cast<std::uint32_t>(count);
    snapshot.spikePinned = static_cast<std::uint32_t>(profiler.Spikes().PinnedCount());
    snapshot.spikeTotal = profiler.Spikes().TotalFrozen();
}

} // namespace

Verdict VerdictFromHistory(const FrameProfiler& profiler, bool capture, bool newestChecksFail)
{
    std::array<VerdictFrame, VerdictConstants::kWindowFrames> window{};
    std::size_t count = 0;
    if (capture) {
        const std::size_t total = profiler.HistoryCount();
        count = std::min(total, window.size());
        for (std::size_t i = 0; i < count; ++i) {
            const FrameSummary& summary = profiler.HistoryAt(total - count + i);
            window[i] = MakeVerdictFrame(summary, SummaryChecksFail(summary) || (i + 1 == count && newestChecksFail));
        }
    }
    return ComputeVerdict(std::span<const VerdictFrame>(window.data(), count));
}

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
    FillSpikes(snapshot, profiler);
    snapshot.overhead = SnapshotOverhead{ MissingMetric(Unit::Count, SampleStatus::Absent, Reason::NotReady),
        MissingMetric(Unit::Milliseconds, SampleStatus::Absent, Reason::NotReady),
        MissingMetric(Unit::Percent, SampleStatus::Absent, Reason::NotReady), false };

    snapshot.device.deviceLost = inputs.deviceLost;
    snapshot.device.presentUncapped = inputs.presentUncapped;
    snapshot.device.consecutiveFrameTimeouts = Count(inputs.consecutiveFrameTimeouts);
    snapshot.device.abandonedObjects = Count(inputs.abandonedObjects);
    snapshot.timeline.aligned = inputs.gpuClockAligned;
    CopySnapshotText(snapshot.timeline.reason, inputs.gpuClockAligned ? std::string_view("aligned") : inputs.timelineReason);

    const FrameRecord& record = profiler.LastCompletedFrame();
    const FrameTotals totals = ComputeFrameTotals(record);
    const ProfilerChecks checks = EvaluateChecks(record, totals, capture);
    bool newestChecksFail = false;
    for (std::size_t i = 0; i < kCheckCount; ++i) {
        const FrameCheck& check = checks.items[i];
        snapshot.checks[i] = SnapshotCheck{ check.id, check.status, Count(check.value), check.text };
        newestChecksFail = newestChecksFail || check.status == CheckStatus::Fail;
    }
    FillVerdict(snapshot, profiler, capture, newestChecksFail);

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
    FillTables(snapshot, record);

    // An estimate, not a reading: events recorded times the cost of one event measured at startup.
    const OverheadEstimate overhead = EstimateOverhead(profiler.EventCostNs(), record.scopes.Size(), summary.cpuMilliseconds);
    if (overhead.known) {
        snapshot.overhead.costPerEvent = PresentMetric(overhead.costPerEventNs, Unit::Count, MetricKind::Estimated);
        snapshot.overhead.estimated = PresentMetric(overhead.estimatedMs, Unit::Milliseconds, MetricKind::Estimated);
        snapshot.overhead.share = PresentMetric(overhead.frameShare * 100.0, Unit::Percent, MetricKind::Estimated);
        snapshot.overhead.warn = overhead.warn;
    }
    return snapshot;
}

} // namespace gte::Profiling
