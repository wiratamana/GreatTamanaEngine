#pragma once

// Pure model of the profiler frame graph: plain series, plain colors, no UI types.

#include "../Profiling/FrameGraphData.h"
#include "../Profiling/ScopeAggregate.h"
#include "../Profiling/ScopeEvent.h"
#include "../Profiling/ScopeHistory.h"

#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <vector>

namespace gte {

struct GraphColor {
    float r = 1.0f;
    float g = 1.0f;
    float b = 1.0f;
};

inline constexpr GraphColor kGraphFrameColor{ 0.70f, 0.70f, 0.70f };
inline constexpr GraphColor kGraphCpuBusyColor{ 0.30f, 0.65f, 1.00f };
inline constexpr GraphColor kGraphGpuColor{ 1.00f, 0.62f, 0.20f };

inline constexpr std::size_t kMaxTrackedRows = 8;
inline constexpr GraphColor kGraphTrackedColors[kMaxTrackedRows] = {
    { 0.55f, 0.90f, 0.40f }, { 0.95f, 0.45f, 0.75f }, { 0.80f, 0.80f, 0.25f }, { 0.60f, 0.50f, 1.00f },
    { 0.30f, 0.90f, 0.85f }, { 1.00f, 0.45f, 0.35f }, { 0.75f, 0.60f, 0.40f }, { 0.85f, 0.85f, 0.95f },
};

// Newest frames drawn by the graph.
inline constexpr std::size_t kGraphWindowFrames = 240;

// A table row ticked to be drawn as its own line.
struct TrackedKey {
    Profiling::ScopeLane lane = Profiling::ScopeLane::Main;
    Profiling::NameId name = Profiling::kInvalidNameId;
    bool operator==(const TrackedKey&) const = default;
};

// False and unchanged when the key is already tracked or the list holds kMaxTrackedRows.
bool AddTracked(std::vector<TrackedKey>& list, TrackedKey key);
// False when the key was not tracked.
bool RemoveTracked(std::vector<TrackedKey>& list, TrackedKey key);
bool IsTracked(std::span<const TrackedKey> list, TrackedKey key) noexcept;

struct GraphVisibility {
    bool frame = true;
    bool cpuBusy = true;
    bool gpu = true;
};

struct GraphSeries {
    std::string label;
    GraphColor color;
    std::vector<float> values; // one per frame of the window, NaN = gap
    bool isBase = true;        // Frame, CPU busy or GPU
    bool visible = true;       // hidden base lines keep their values for the legend
    TrackedKey key;            // tracked series only
};

struct GraphModel {
    std::vector<GraphSeries> series; // Frame, CPU busy, GPU, then tracked rows in tick order
    std::vector<std::uint64_t> frameIndices;
    float axisMaxMs = static_cast<float>(Profiling::kGraphGuideLowMs);
    bool guideLow = false;  // 16.7 ms line fits under the axis
    bool guideHigh = false; // 33.3 ms line fits under the axis
    bool historyOutOfSync = false; // tracked rows exist but their history ends on another frame
};

// Tracked rows without a history track are left out of the model.
GraphModel BuildGraphModel(std::span<const Profiling::FrameGraphPoint> points, const Profiling::ScopeHistory& history,
    std::span<const TrackedKey> tracked, Profiling::ScopeSeries series, const GraphVisibility& visibility);

} // namespace gte
