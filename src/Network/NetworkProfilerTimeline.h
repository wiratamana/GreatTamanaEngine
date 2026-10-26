#pragma once

// Timeline picture of one profiler frame, drawn on the CPU from frame data. It never reads GPU pixels.
// Lanes from the top: CPU main (one row per nesting depth), waits, worker jobs, GPU per view (unaligned).

#include "../Editor/ViewportProjection.h"
#include "../Profiling/TimelineLayout.h"

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace gte::Network {

inline constexpr int kTimelineWidthDefault = 1200;
inline constexpr int kTimelineWidthMin = 200;
inline constexpr int kTimelineWidthMax = 2400;

// Empty text means the default; anything else must be a whole number within the limits.
std::optional<int> ParseTimelineWidth(const std::string& text);

// Pixel geometry of a layout: image size and the filled rectangles, back to front.
struct TimelinePixelPlan {
    int width = 0;
    int height = 0;
    std::vector<OverlayQuad2D> quads;
};

TimelinePixelPlan PlanTimelinePixels(const Profiling::TimelineLayout& layout, int width);

// PNG bytes of the plan on an opaque background.
std::vector<std::uint8_t> RenderTimelinePng(const TimelinePixelPlan& plan);

} // namespace gte::Network
