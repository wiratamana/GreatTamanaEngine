#pragma once

// Places the scopes, waits, jobs and GPU times of one frame on a shared time axis. Pure data in, bars out.

#include "SpikeDetail.h"

#include <array>
#include <cstdint>
#include <vector>

namespace gte::Profiling {

enum class TimelineLane : std::uint8_t { CpuMain, Blocked, Workers, Gpu };
inline constexpr std::size_t kTimelineLaneCount = 4;

enum class TimelineBarKind : std::uint8_t { Work, Blocked, Marker, Job, GpuShared, GpuPrimary, GpuSecondary };

inline constexpr std::uint16_t kTimelineMaxRows = 8;

// A bar in axis fractions: x0 and x1 lie in [0, 1], x1 >= x0.
struct TimelineBar {
    TimelineLane lane = TimelineLane::CpuMain;
    std::uint16_t row = 0;
    float x0 = 0.0f;
    float x1 = 0.0f;
    TimelineBarKind kind = TimelineBarKind::Work;
};

struct TimelineLayout {
    double axisMs = 0.0;                                      // time the full axis stands for
    std::array<std::uint16_t, kTimelineLaneCount> laneRows{}; // rows used per lane, at least 1
    std::uint32_t hiddenBars = 0;                             // bars that did not fit the row limit
    std::vector<TimelineBar> bars;
};

// GPU bars start at the axis origin: a frame carries no GPU start time, so its lane is not aligned to the CPU lanes.
TimelineLayout BuildTimelineLayout(const SpikeDetail& frame);

} // namespace gte::Profiling
