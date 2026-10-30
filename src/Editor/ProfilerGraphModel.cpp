#include "ProfilerGraphModel.h"

#include "../Application/ProfilerScopeCatalog.h"
#include "../Profiling/NameTable.h"
#include "../Profiling/ScopeSource.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <utility>

namespace gte {
namespace {

constexpr float kGap = std::numeric_limits<float>::quiet_NaN();
constexpr std::size_t kBaseSeriesCount = 3; // Frame, CPU busy, GPU

// Rewrites a base line in place and keeps the capacity of its value buffer.
void ResetBase(GraphSeries& s, const char* label, GraphColor color, bool visible, std::size_t count)
{
    s.label = label;
    s.color = color;
    s.visible = visible;
    s.isBase = true;
    s.key = TrackedKey{};
    s.values.clear();
    s.values.reserve(count);
}

void TrackLargest(const GraphSeries& s, float& largest) noexcept
{
    if (!s.visible) {
        return;
    }
    for (const float v : s.values) {
        if (!std::isnan(v)) {
            largest = std::max(largest, v);
        }
    }
}

} // namespace

bool AddTracked(std::vector<TrackedKey>& list, TrackedKey key)
{
    if (list.size() >= kMaxTrackedRows || IsTracked(list, key)) {
        return false;
    }
    list.push_back(key);
    return true;
}

bool RemoveTracked(std::vector<TrackedKey>& list, TrackedKey key)
{
    const auto it = std::find(list.begin(), list.end(), key);
    if (it == list.end()) {
        return false;
    }
    list.erase(it);
    return true;
}

bool IsTracked(std::span<const TrackedKey> list, TrackedKey key) noexcept
{
    return std::find(list.begin(), list.end(), key) != list.end();
}

void BuildGraphModel(std::span<const Profiling::FrameGraphPoint> points, const Profiling::ScopeHistory& history,
    std::span<const TrackedKey> tracked, Profiling::ScopeSeries series, const GraphVisibility& visibility,
    GraphModel& model)
{
    const std::span<const Profiling::FrameGraphPoint> window =
        points.size() > kGraphWindowFrames ? points.last(kGraphWindowFrames) : points;
    const std::size_t count = window.size();

    if (model.series.size() < kBaseSeriesCount) {
        model.series.resize(kBaseSeriesCount);
    }
    GraphSeries& frame = model.series[0];
    GraphSeries& busy = model.series[1];
    GraphSeries& gpu = model.series[2];
    ResetBase(frame, "Frame time", kGraphFrameColor, visibility.frame, count);
    ResetBase(busy, "CPU busy", kGraphCpuBusyColor, visibility.cpuBusy, count);
    ResetBase(gpu, "GPU", kGraphGpuColor, visibility.gpu, count);

    model.frameIndices.clear();
    model.frameIndices.reserve(count);
    for (const Profiling::FrameGraphPoint& p : window) {
        model.frameIndices.push_back(p.frameIndex);
        frame.values.push_back(static_cast<float>(p.cpuMilliseconds));
        busy.values.push_back(static_cast<float>(p.cpuBusyMilliseconds));
        gpu.values.push_back(static_cast<float>(Profiling::GpuSpanMilliseconds(p.gpuSpan)));
    }

    // Tracked rows reuse the slots after the base lines; a row without history leaves no slot.
    std::size_t used = kBaseSeriesCount;
    const std::size_t trackedCount = std::min(tracked.size(), kMaxTrackedRows);
    for (std::size_t i = 0; i < trackedCount && count > 0; ++i) {
        if (model.series.size() <= used) {
            model.series.emplace_back();
        }
        GraphSeries& s = model.series[used];
        s.values.assign(count, kGap);
        if (!history.CopySeries(tracked[i].lane, tracked[i].name, series, s.values)) {
            continue;
        }
        if (s.isBase || s.label.empty() || !(s.key == tracked[i])) {
            const char* raw = Profiling::ResolveName(tracked[i].name);
            s.label = DescribeScope(raw, tracked[i].lane, Profiling::SourceOf(tracked[i].name).origin).title;
        }
        s.isBase = false;
        s.visible = true;
        s.color = kGraphTrackedColors[i];
        s.key = tracked[i];
        ++used;
    }
    model.series.resize(used);

    model.historyOutOfSync = trackedCount > 0 && count > 0 && history.NewestFrameIndex() != window.back().frameIndex;

    float largest = 0.0f;
    for (const GraphSeries& s : model.series) {
        TrackLargest(s, largest);
    }
    const double axisMax = Profiling::GraphAxisMaxMilliseconds(largest);
    model.axisMaxMs = static_cast<float>(axisMax);
    model.guideLow = Profiling::kGraphGuideLowMs < axisMax;
    model.guideHigh = Profiling::kGraphGuideHighMs < axisMax;
}

} // namespace gte
