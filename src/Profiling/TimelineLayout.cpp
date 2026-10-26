#include "TimelineLayout.h"

#include <algorithm>
#include <cmath>
#include <vector>

namespace gte::Profiling {

namespace {

constexpr bool HasTime(SampleStatus status) noexcept
{
    return status == SampleStatus::Present || status == SampleStatus::Partial;
}

} // namespace

TimelineLayout BuildTimelineLayout(const SpikeDetail& frame)
{
    TimelineLayout layout;

    double axis = std::max(frame.frameMs, 0.0);
    for (const SpikeScopeRow& row : frame.scopes) {
        axis = std::max(axis, row.startMs + row.durationMs);
    }
    for (const SpikeWorkerRow& job : frame.workers) {
        axis = std::max(axis, job.startMs + job.durationMs);
    }
    std::array<double, kViewSlotCount> gpuMs{};
    for (std::size_t i = 0; i < kViewSlotCount; ++i) {
        const GpuViewMetric& view = frame.gpu.views[i];
        if (HasTime(view.status) && std::isfinite(view.milliseconds) && view.milliseconds > 0.0f) {
            gpuMs[i] = view.milliseconds;
            axis = std::max(axis, gpuMs[i]);
        }
    }
    layout.axisMs = axis;
    layout.laneRows.fill(1);
    if (!(axis > 0.0)) {
        return layout;
    }

    const auto add = [&](TimelineLane lane, std::uint16_t row, double startMs, double durationMs, TimelineBarKind kind) {
        if (row >= kTimelineMaxRows) {
            ++layout.hiddenBars;
            return;
        }
        TimelineBar bar;
        bar.lane = lane;
        bar.row = row;
        bar.x0 = static_cast<float>(std::clamp(startMs / axis, 0.0, 1.0));
        bar.x1 = static_cast<float>(std::clamp((startMs + std::max(durationMs, 0.0)) / axis, 0.0, 1.0));
        bar.kind = kind;
        layout.bars.push_back(bar);
        std::uint16_t& rows = layout.laneRows[static_cast<std::size_t>(lane)];
        rows = std::max<std::uint16_t>(rows, static_cast<std::uint16_t>(row + 1));
    };

    for (const SpikeScopeRow& row : frame.scopes) {
        switch (row.kind) {
        case ScopeKind::Work: add(TimelineLane::CpuMain, row.depth, row.startMs, row.durationMs, TimelineBarKind::Work); break;
        case ScopeKind::Blocked: add(TimelineLane::Blocked, 0, row.startMs, row.durationMs, TimelineBarKind::Blocked); break;
        case ScopeKind::Marker: add(TimelineLane::CpuMain, 0, row.startMs, 0.0, TimelineBarKind::Marker); break;
        }
    }

    // Workers get one row each in order of first appearance.
    std::vector<std::uint16_t> seen;
    for (const SpikeWorkerRow& job : frame.workers) {
        auto it = std::find(seen.begin(), seen.end(), job.worker);
        if (it == seen.end()) {
            seen.push_back(job.worker);
            it = seen.end() - 1;
        }
        add(TimelineLane::Workers, static_cast<std::uint16_t>(it - seen.begin()), job.startMs, job.durationMs, TimelineBarKind::Job);
    }

    constexpr TimelineBarKind kGpuKinds[kViewSlotCount] = { TimelineBarKind::GpuShared, TimelineBarKind::GpuPrimary,
        TimelineBarKind::GpuSecondary };
    for (std::size_t i = 0; i < kViewSlotCount; ++i) {
        if (gpuMs[i] > 0.0) {
            add(TimelineLane::Gpu, static_cast<std::uint16_t>(i), 0.0, gpuMs[i], kGpuKinds[i]);
        }
    }
    return layout;
}

} // namespace gte::Profiling
