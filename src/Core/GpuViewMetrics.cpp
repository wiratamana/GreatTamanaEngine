#include "GpuViewMetrics.h"

#include <cstddef>

namespace gte {

static_assert(rg::kViewScopeCount == Profiling::kViewSlotCount, "view scopes and view slots must match one to one");
static_assert(static_cast<int>(GpuReadState::Inactive) == 5, "a new GpuReadState needs a mapping in ToSampleStatus");

Profiling::SampleStatus ToSampleStatus(GpuReadState state) noexcept
{
    switch (state) {
    case GpuReadState::Present:
        return Profiling::SampleStatus::Present;
    case GpuReadState::Partial:
        return Profiling::SampleStatus::Partial;
    case GpuReadState::Absent:
        return Profiling::SampleStatus::Absent;
    case GpuReadState::Error:
        return Profiling::SampleStatus::Error;
    case GpuReadState::Unsupported:
        return Profiling::SampleStatus::Unsupported;
    case GpuReadState::Inactive:
        return Profiling::SampleStatus::Inactive;
    }
    return Profiling::SampleStatus::Absent;
}

Profiling::ViewSlot ToViewSlot(rg::ViewScope scope) noexcept
{
    switch (scope) {
    case rg::ViewScope::Shared:
        return Profiling::ViewSlot::Shared;
    case rg::ViewScope::Primary:
        return Profiling::ViewSlot::Primary;
    case rg::ViewScope::Secondary:
        return Profiling::ViewSlot::Secondary;
    }
    return Profiling::ViewSlot::Shared;
}

namespace {

using Profiling::SampleStatus;

// What a frame-wide failure says about a number that has no record to speak for it.
SampleStatus FallbackStatus(const GpuFrameReport& report) noexcept
{
    const SampleStatus status = ToSampleStatus(report.state);
    return (status == SampleStatus::Unsupported || status == SampleStatus::Error || status == SampleStatus::Inactive)
        ? status
        : SampleStatus::Absent;
}

struct ViewTally {
    double milliseconds = 0.0;
    std::uint32_t records = 0;
    std::uint32_t measured = 0;
    std::uint32_t folded = 0;
    std::uint32_t errors = 0;
    std::uint32_t unsupported = 0;
    std::uint32_t inactive = 0;
    std::uint32_t indirectDraws = 0;
};

SampleStatus StatusOf(const ViewTally& tally, const GpuFrameReport& report) noexcept
{
    if (tally.records == 0) {
        return FallbackStatus(report);
    }
    if (tally.measured == tally.records && tally.folded == 0) {
        return SampleStatus::Present;
    }
    if (tally.measured != 0) {
        return SampleStatus::Partial;
    }
    if (tally.errors == tally.records) {
        return SampleStatus::Error;
    }
    if (tally.unsupported == tally.records) {
        return SampleStatus::Unsupported;
    }
    if (tally.inactive == tally.records) {
        return SampleStatus::Inactive;
    }
    return SampleStatus::Absent;
}

} // namespace

Profiling::GpuFrameMetrics BuildGpuFrameMetrics(
    std::span<const GpuPassRecord> records, const GpuFrameReport& report, const GpuMetricsInput& input) noexcept
{
    Profiling::GpuFrameMetrics metrics;
    metrics.declaredPasses = input.declaredPasses;
    metrics.executedPasses = input.executedPasses;
    metrics.recordedPasses = static_cast<std::uint32_t>(records.size());
    metrics.disabledByToggle = input.disabledByToggle;
    metrics.droppedFrames = input.droppedFrames;

    ViewTally tallies[Profiling::kViewSlotCount];
    for (const GpuPassRecord& record : records) {
        if (record.view >= Profiling::kViewSlotCount) {
            continue;
        }
        ViewTally& tally = tallies[record.view];
        ++tally.records;
        tally.folded += record.foldedPasses;
        tally.indirectDraws += record.indirectDraws;
        switch (record.state) {
        case GpuReadState::Present:
            ++tally.measured;
            tally.milliseconds += static_cast<double>(record.barrierMs) + static_cast<double>(record.workMs);
            break;
        case GpuReadState::Partial:
            tally.milliseconds += static_cast<double>(record.barrierMs) + static_cast<double>(record.workMs);
            break;
        case GpuReadState::Error:
            ++tally.errors;
            break;
        case GpuReadState::Unsupported:
            ++tally.unsupported;
            break;
        case GpuReadState::Inactive:
            ++tally.inactive;
            break;
        case GpuReadState::Absent:
            break;
        }
    }

    bool anyViewActive = false;
    for (std::size_t slot = 0; slot < Profiling::kViewSlotCount; ++slot) {
        Profiling::GpuViewMetric& metric = metrics.views[slot];
        if (!input.viewActive[slot]) {
            metric.status = SampleStatus::Inactive;
            continue;
        }
        anyViewActive = anyViewActive || slot != static_cast<std::size_t>(Profiling::ViewSlot::Shared);
        metrics.indirectDraws[slot] = tallies[slot].indirectDraws;
        if (!input.captureEnabled) {
            metric.status = SampleStatus::Absent;
            continue;
        }
        metric.status = StatusOf(tallies[slot], report);
        metric.foldedPasses = tallies[slot].folded;
        if (metric.status == SampleStatus::Present || metric.status == SampleStatus::Partial) {
            metric.milliseconds = static_cast<float>(tallies[slot].milliseconds);
        }
    }

    Profiling::GpuViewMetric& span = metrics.span;
    if (!anyViewActive) {
        span.status = SampleStatus::Inactive;
    } else if (!input.captureEnabled) {
        span.status = SampleStatus::Absent;
    } else if (report.spanValid) {
        span.status = SampleStatus::Present;
        span.milliseconds = static_cast<float>(report.spanMs);
        span.foldedPasses = report.foldedPasses;
    } else {
        span.status = FallbackStatus(report);
    }
    return metrics;
}

} // namespace gte
