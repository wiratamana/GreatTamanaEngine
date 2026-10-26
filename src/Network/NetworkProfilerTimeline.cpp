#include "NetworkProfilerTimeline.h"

#include "../Editor/OverlayRaster.h"
#include "../Encoding/PngEncoder.h"

#include <algorithm>
#include <array>
#include <charconv>
#include <system_error>

namespace gte::Network {

namespace {

using Profiling::TimelineBarKind;
using Profiling::TimelineLane;

constexpr int kMargin = 6;
constexpr int kRowHeight = 14;
constexpr int kRowGap = 2;
constexpr int kLaneGap = 8;
constexpr std::array<std::uint8_t, 4> kBackground{ 28, 28, 32, 255 };
constexpr std::array<std::uint8_t, 4> kLaneColor{ 44, 44, 52, 255 };
constexpr std::array<std::uint8_t, 4> kUnalignedLaneColor{ 62, 44, 44, 255 };

std::array<std::uint8_t, 4> BarColor(TimelineBarKind kind) noexcept
{
    switch (kind) {
    case TimelineBarKind::Work: return { 90, 160, 235, 255 };
    case TimelineBarKind::Blocked: return { 235, 120, 70, 255 };
    case TimelineBarKind::Marker: return { 250, 230, 90, 255 };
    case TimelineBarKind::Job: return { 120, 200, 130, 255 };
    case TimelineBarKind::GpuShared: return { 170, 170, 190, 255 };
    case TimelineBarKind::GpuPrimary: return { 190, 120, 230, 255 };
    case TimelineBarKind::GpuSecondary: return { 230, 120, 170, 255 };
    }
    return { 255, 255, 255, 255 };
}

OverlayQuad2D Rect(float x0, float y0, float x1, float y1, std::array<std::uint8_t, 4> rgba)
{
    OverlayQuad2D quad;
    quad.p[0] = Vec2(x0, y0);
    quad.p[1] = Vec2(x1, y0);
    quad.p[2] = Vec2(x1, y1);
    quad.p[3] = Vec2(x0, y1);
    quad.rgba = rgba;
    return quad;
}

int LaneTop(const Profiling::TimelineLayout& layout, std::size_t lane) noexcept
{
    int top = kMargin;
    for (std::size_t i = 0; i < lane; ++i) {
        top += layout.laneRows[i] * (kRowHeight + kRowGap) + kLaneGap;
    }
    return top;
}

} // namespace

std::optional<int> ParseTimelineWidth(const std::string& text)
{
    if (text.empty()) {
        return kTimelineWidthDefault;
    }
    int value = 0;
    const char* const begin = text.data();
    const char* const end = begin + text.size();
    const std::from_chars_result result = std::from_chars(begin, end, value);
    if (result.ec != std::errc{} || result.ptr != end || value < kTimelineWidthMin || value > kTimelineWidthMax) {
        return std::nullopt;
    }
    return value;
}

TimelinePixelPlan PlanTimelinePixels(const Profiling::TimelineLayout& layout, int width)
{
    TimelinePixelPlan plan;
    plan.width = std::clamp(width, kTimelineWidthMin, kTimelineWidthMax);
    plan.height = LaneTop(layout, Profiling::kTimelineLaneCount) + kMargin - kLaneGap;

    const float axisLeft = static_cast<float>(kMargin);
    const float axisWidth = static_cast<float>(plan.width - 2 * kMargin);
    for (std::size_t lane = 0; lane < Profiling::kTimelineLaneCount; ++lane) {
        const float top = static_cast<float>(LaneTop(layout, lane));
        const float bottom = top + static_cast<float>(layout.laneRows[lane] * (kRowHeight + kRowGap));
        const bool gpu = lane == static_cast<std::size_t>(TimelineLane::Gpu);
        plan.quads.push_back(Rect(axisLeft, top, axisLeft + axisWidth, bottom, gpu ? kUnalignedLaneColor : kLaneColor));
    }

    for (const Profiling::TimelineBar& bar : layout.bars) {
        const float top = static_cast<float>(LaneTop(layout, static_cast<std::size_t>(bar.lane)) + bar.row * (kRowHeight + kRowGap));
        const float x0 = axisLeft + bar.x0 * axisWidth;
        const float x1 = std::max(axisLeft + bar.x1 * axisWidth, x0 + 1.0f);
        plan.quads.push_back(Rect(x0, top, x1, top + static_cast<float>(kRowHeight), BarColor(bar.kind)));
    }
    return plan;
}

std::vector<std::uint8_t> RenderTimelinePng(const TimelinePixelPlan& plan)
{
    const std::size_t pixels = static_cast<std::size_t>(plan.width) * static_cast<std::size_t>(plan.height);
    std::vector<std::uint8_t> rgba(pixels * 4);
    for (std::size_t i = 0; i < pixels; ++i) {
        std::copy(kBackground.begin(), kBackground.end(), rgba.begin() + static_cast<std::ptrdiff_t>(i * 4));
    }
    RasterizeQuads(rgba, plan.width, plan.height, plan.quads);
    return Encoding::EncodeRgba8ToPng(rgba.data(), plan.width, plan.height);
}

} // namespace gte::Network
