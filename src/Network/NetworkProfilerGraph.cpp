#include "NetworkProfilerGraph.h"

#include "../Editor/OverlayRaster.h"
#include "../Encoding/PngEncoder.h"
#include "../Profiling/FrameGraphData.h"

#include <algorithm>
#include <array>
#include <charconv>
#include <cmath>
#include <system_error>

namespace gte::Network {

namespace {

using Profiling::Metric;
using Profiling::SnapshotFrameRow;

using Rgba = std::array<std::uint8_t, 4>;

constexpr float kMargin = 4.0f;
constexpr float kLineHalfWidth = 1.0f;
constexpr Rgba kBackground{ 28, 28, 32, 255 };
constexpr Rgba kGuideColor{ 62, 62, 70, 255 };

struct Series {
    Metric SnapshotFrameRow::* metric;
    Rgba color;
};

constexpr std::array<Series, 3> kSeries{ {
    { &SnapshotFrameRow::cpu, { 179, 179, 179, 255 } },
    { &SnapshotFrameRow::cpuBusy, { 77, 166, 255, 255 } },
    { &SnapshotFrameRow::gpuSpan, { 255, 158, 51, 255 } },
} };

bool Usable(const Metric& metric) noexcept
{
    return metric.status == Profiling::SampleStatus::Present && std::isfinite(metric.value) && metric.value >= 0.0;
}

OverlayQuad2D Rect(float x0, float y0, float x1, float y1, const Rgba& rgba, float width, float height)
{
    x0 = std::clamp(x0, 0.0f, width);
    x1 = std::clamp(x1, 0.0f, width);
    y0 = std::clamp(y0, 0.0f, height);
    y1 = std::clamp(y1, 0.0f, height);
    OverlayQuad2D quad;
    quad.p[0] = Vec2(x0, y0);
    quad.p[1] = Vec2(x1, y0);
    quad.p[2] = Vec2(x1, y1);
    quad.p[3] = Vec2(x0, y1);
    quad.rgba = rgba;
    return quad;
}

} // namespace

std::optional<int> ParseGraphParam(const std::string& text, int defaultValue, int minValue, int maxValue)
{
    if (text.empty()) {
        return defaultValue;
    }
    int value = 0;
    const char* const begin = text.data();
    const char* const end = begin + text.size();
    const std::from_chars_result result = std::from_chars(begin, end, value);
    if (result.ec != std::errc{} || result.ptr != end || value < minValue || value > maxValue) {
        return std::nullopt;
    }
    return value;
}

GraphPixelPlan PlanGraphPixels(std::span<const SnapshotFrameRow> frames, int width, int height)
{
    GraphPixelPlan plan;
    plan.width = std::clamp(width, kGraphWidthMin, kGraphWidthMax);
    plan.height = std::clamp(height, kGraphHeightMin, kGraphHeightMax);
    if (frames.size() > static_cast<std::size_t>(kGraphFramesMax)) {
        frames = frames.last(static_cast<std::size_t>(kGraphFramesMax));
    }

    double largest = 0.0;
    for (const SnapshotFrameRow& row : frames) {
        for (const Series& series : kSeries) {
            if (Usable(row.*series.metric)) {
                largest = std::max(largest, (row.*series.metric).value);
            }
        }
    }
    const double axisMax = Profiling::GraphAxisMaxMilliseconds(largest);

    const float w = static_cast<float>(plan.width);
    const float h = static_cast<float>(plan.height);
    const float left = kMargin;
    const float right = w - kMargin;
    const float top = kMargin;
    const float bottom = h - kMargin;
    const auto yOf = [&](double ms) { return bottom - static_cast<float>(ms / axisMax) * (bottom - top); };

    for (const double guide : { Profiling::kGraphGuideLowMs, Profiling::kGraphGuideHighMs }) {
        if (guide < axisMax) {
            const float y = yOf(guide);
            plan.quads.push_back(Rect(left, y - 0.5f, right, y + 0.5f, kGuideColor, w, h));
        }
    }

    const std::size_t count = frames.size();
    const auto xOf = [&](std::size_t index) {
        return count == 1 ? (left + right) * 0.5f
                          : left + static_cast<float>(index) * (right - left) / static_cast<float>(count - 1);
    };

    // Step line: a flat run per sample and a vertical riser to the next sample.
    for (const Series& series : kSeries) {
        if (count == 1 && Usable(frames[0].*series.metric)) {
            const float x = xOf(0);
            const float y = yOf((frames[0].*series.metric).value);
            plan.quads.push_back(Rect(x - 1.5f, y - 1.5f, x + 1.5f, y + 1.5f, series.color, w, h));
            continue;
        }
        for (std::size_t i = 0; i + 1 < count; ++i) {
            const Metric& a = frames[i].*series.metric;
            const Metric& b = frames[i + 1].*series.metric;
            if (!Usable(a) || !Usable(b)) {
                continue;
            }
            const float x0 = xOf(i);
            const float x1 = xOf(i + 1);
            const float ya = yOf(a.value);
            const float yb = yOf(b.value);
            plan.quads.push_back(Rect(x0, ya - kLineHalfWidth, x1, ya + kLineHalfWidth, series.color, w, h));
            plan.quads.push_back(
                Rect(x1 - kLineHalfWidth, std::min(ya, yb) - kLineHalfWidth, x1 + kLineHalfWidth,
                    std::max(ya, yb) + kLineHalfWidth, series.color, w, h));
        }
    }
    return plan;
}

std::vector<std::uint8_t> RenderGraphPng(const GraphPixelPlan& plan)
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
