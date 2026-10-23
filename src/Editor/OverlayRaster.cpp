#include "OverlayRaster.h"

#include <algorithm>
#include <cmath>
#include <cstddef>

namespace gte {

namespace {

// Liang-Barsky clip of p0->p1 to [0, w] x [0, h]. False when nothing remains.
bool ClipLine(double& x0, double& y0, double& x1, double& y1, double w, double h) noexcept
{
    const double dx = x1 - x0;
    const double dy = y1 - y0;
    const double p[4] = { -dx, dx, -dy, dy };
    const double q[4] = { x0, w - x0, y0, h - y0 };
    double t0 = 0.0;
    double t1 = 1.0;
    for (int i = 0; i < 4; ++i) {
        if (p[i] == 0.0) {
            if (q[i] < 0.0) {
                return false;
            }
            continue;
        }
        const double t = q[i] / p[i];
        if (p[i] < 0.0) {
            t0 = std::max(t0, t);
        } else {
            t1 = std::min(t1, t);
        }
        if (t0 > t1) {
            return false;
        }
    }
    const double nx0 = x0 + dx * t0;
    const double ny0 = y0 + dy * t0;
    x1 = x0 + dx * t1;
    y1 = y0 + dy * t1;
    x0 = nx0;
    y0 = ny0;
    return true;
}

void BlendPixel(std::uint8_t* dst, const std::array<std::uint8_t, 4>& src) noexcept
{
    const int a = src[3];
    if (a == 0) {
        return;
    }
    const int inv = 255 - a;
    for (int c = 0; c < 3; ++c) {
        dst[c] = static_cast<std::uint8_t>((src[static_cast<std::size_t>(c)] * a + dst[c] * inv + 127) / 255);
    }
    dst[3] = static_cast<std::uint8_t>(std::min(255, a + (dst[3] * inv + 127) / 255));
}

} // namespace

void RasterizeSegments(std::span<std::uint8_t> rgba, int width, int height, std::span<const OverlaySegment2D> segments)
{
    if (width <= 0 || height <= 0) {
        return;
    }
    const std::size_t needed = static_cast<std::size_t>(width) * static_cast<std::size_t>(height) * 4u;
    if (rgba.size() < needed) {
        return;
    }

    for (const OverlaySegment2D& segment : segments) {
        double x0 = segment.a.x;
        double y0 = segment.a.y;
        double x1 = segment.b.x;
        double y1 = segment.b.y;
        if (!std::isfinite(x0) || !std::isfinite(y0) || !std::isfinite(x1) || !std::isfinite(y1)) {
            continue;
        }
        if (!ClipLine(x0, y0, x1, y1, width, height)) {
            continue;
        }

        const double length = std::max(std::fabs(x1 - x0), std::fabs(y1 - y0));
        const int steps = std::max(1, static_cast<int>(std::ceil(length)));
        int lastX = -1;
        int lastY = -1;
        for (int i = 0; i <= steps; ++i) {
            const double t = static_cast<double>(i) / static_cast<double>(steps);
            const int px = std::clamp(static_cast<int>(std::floor(x0 + (x1 - x0) * t)), 0, width - 1);
            const int py = std::clamp(static_cast<int>(std::floor(y0 + (y1 - y0) * t)), 0, height - 1);
            if (px == lastX && py == lastY) {
                continue;
            }
            lastX = px;
            lastY = py;
            BlendPixel(&rgba[(static_cast<std::size_t>(py) * static_cast<std::size_t>(width) + static_cast<std::size_t>(px)) * 4u],
                segment.rgba);
        }
    }
}

} // namespace gte
