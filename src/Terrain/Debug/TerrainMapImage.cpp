#include "TerrainMapImage.h"

#include <algorithm>
#include <array>
#include <cmath>

namespace gte::terrain {

namespace {

constexpr double kPi = 3.14159265358979323846;
constexpr int kMinSize = 64;
constexpr int kMaxSize = 2048;

using Rgb = std::array<std::uint8_t, 3>;

constexpr std::array<Rgb, 12> kLodColors = { {
    { 70, 200, 90 }, { 150, 210, 70 }, { 230, 210, 60 }, { 240, 150, 50 }, { 235, 90, 60 }, { 220, 70, 150 },
    { 160, 90, 210 }, { 90, 110, 230 }, { 60, 170, 230 }, { 70, 215, 205 }, { 200, 200, 200 }, { 120, 120, 120 },
} };

struct Canvas {
    int width = 0;
    int height = 0;
    std::vector<std::uint8_t>* rgba = nullptr;

    void Blend(int x, int y, Rgb color, int alpha) const noexcept
    {
        if (x < 0 || y < 0 || x >= width || y >= height) {
            return;
        }
        std::uint8_t* p = rgba->data() + (static_cast<std::size_t>(y) * width + x) * 4;
        for (int c = 0; c < 3; ++c) {
            p[c] = static_cast<std::uint8_t>((p[c] * (255 - alpha) + color[c] * alpha) / 255);
        }
        p[3] = 255;
    }

    void Line(double x0, double y0, double x1, double y1, Rgb color) const noexcept
    {
        const double length = std::max(std::abs(x1 - x0), std::abs(y1 - y0));
        const int steps = static_cast<int>(std::min(length, 8192.0)) + 1;
        for (int i = 0; i <= steps; ++i) {
            const double t = static_cast<double>(i) / steps;
            Blend(static_cast<int>(std::lround(x0 + (x1 - x0) * t)), static_cast<int>(std::lround(y0 + (y1 - y0) * t)), color, 255);
        }
    }

    void Disc(double cx, double cy, double radius, Rgb color) const noexcept
    {
        for (int y = static_cast<int>(cy - radius); y <= static_cast<int>(cy + radius); ++y) {
            for (int x = static_cast<int>(cx - radius); x <= static_cast<int>(cx + radius); ++x) {
                const double dx = x - cx;
                const double dy = y - cy;
                if (dx * dx + dy * dy <= radius * radius) {
                    Blend(x, y, color, 255);
                }
            }
        }
    }
};

Rgb Scale(Rgb color, double factor) noexcept
{
    for (std::uint8_t& c : color) {
        c = static_cast<std::uint8_t>(std::clamp(c * factor, 0.0, 255.0));
    }
    return color;
}

Rgb NodeColor(const TerrainNodeInfo& node, MapColorMode mode) noexcept
{
    if (mode == MapColorMode::State) {
        if (!node.enabled) {
            return { 90, 90, 100 };
        }
        return node.inView ? Rgb{ 70, 200, 90 } : Rgb{ 235, 170, 50 };
    }
    return MapLodColor(node.key.level);
}

} // namespace

std::array<std::uint8_t, 3> MapLodColor(int level) noexcept
{
    return kLodColors[static_cast<std::size_t>(std::clamp(level, 0, static_cast<int>(kLodColors.size()) - 1))];
}

MapImage RenderTerrainMap(std::span<const TerrainNodeInfo> nodes, const MapCamera& camera, const MapOptions& options)
{
    MapImage image;
    const int size = std::clamp(options.size, kMinSize, kMaxSize);
    image.width = size;
    image.height = size;
    image.rgba.assign(static_cast<std::size_t>(size) * size * 4, 0);
    for (std::size_t i = 0; i < image.rgba.size(); i += 4) {
        image.rgba[i] = 18;
        image.rgba[i + 1] = 22;
        image.rgba[i + 2] = 32;
        image.rgba[i + 3] = 255;
    }

    const double range = options.range > 1.0 ? options.range : std::max(camera.viewDistance, 100.0) * 1.15;
    const double mpp = 2.0 * range / size;
    image.metresPerPixel = mpp;
    image.minX = camera.x - range;
    image.maxX = camera.x + range;
    image.minZ = camera.z - range;
    image.maxZ = camera.z + range;

    Canvas canvas{ size, size, &image.rgba };
    auto toPixelX = [&](double x) { return (x - image.minX) / mpp; };
    auto toPixelY = [&](double z) { return (image.maxZ - z) / mpp; };

    // Coarse nodes first: a finer node drawn later sits on top of its parent's area.
    std::vector<const TerrainNodeInfo*> order;
    order.reserve(nodes.size());
    for (const TerrainNodeInfo& node : nodes) {
        order.push_back(&node);
    }
    std::stable_sort(order.begin(), order.end(), [](const TerrainNodeInfo* a, const TerrainNodeInfo* b) { return a->key.level > b->key.level; });

    const double heightSpan = std::max(options.heightMax - options.heightMin, 1.0);
    for (const TerrainNodeInfo* node : order) {
        const double x0 = toPixelX(node->minX);
        const double x1 = toPixelX(node->maxX);
        const double y0 = toPixelY(node->maxZ);
        const double y1 = toPixelY(node->minZ);
        if (x1 < 0.0 || y1 < 0.0 || x0 >= size || y0 >= size) {
            continue;
        }
        const double mid = 0.5 * (node->minY + node->maxY);
        const double shade = 0.55 + 0.45 * std::clamp((mid - options.heightMin) / heightSpan, 0.0, 1.0);
        const Rgb fill = Scale(NodeColor(*node, options.mode), shade);
        const Rgb edge = Scale(fill, 0.45);
        const int px0 = std::max(0, static_cast<int>(std::floor(x0)));
        const int px1 = std::min(size - 1, static_cast<int>(std::ceil(x1)) - 1);
        const int py0 = std::max(0, static_cast<int>(std::floor(y0)));
        const int py1 = std::min(size - 1, static_cast<int>(std::ceil(y1)) - 1);
        for (int y = py0; y <= py1; ++y) {
            for (int x = px0; x <= px1; ++x) {
                const bool border = x == static_cast<int>(std::floor(x0)) || y == static_cast<int>(std::floor(y0))
                    || x == static_cast<int>(std::ceil(x1)) - 1 || y == static_cast<int>(std::ceil(y1)) - 1;
                canvas.Blend(x, y, border ? edge : fill, 255);
            }
        }
    }

    // Camera marker and view wedge.
    const double cx = toPixelX(camera.x);
    const double cy = toPixelY(camera.z);
    if (options.drawFrustum) {
        const double halfV = std::clamp(camera.fovYDegrees, 5.0, 170.0) * kPi / 360.0;
        const double halfH = std::atan(std::tan(halfV) * std::max(camera.aspect, 0.1));
        const double length = std::min(camera.viewDistance, range * 1.4) / mpp;
        for (const double side : { -1.0, 1.0 }) {
            const double angle = camera.yawRadians + side * halfH;
            canvas.Line(cx, cy, cx + std::sin(angle) * length, cy - std::cos(angle) * length, { 255, 255, 255 });
        }
        canvas.Line(cx, cy, cx + std::sin(camera.yawRadians) * length * 0.25, cy - std::cos(camera.yawRadians) * length * 0.25, { 255, 255, 255 });
    }
    canvas.Disc(cx, cy, 4.0, { 255, 40, 40 });

    // Legend: one swatch per LOD level (top-left) and a 1 km scale bar (bottom-left).
    for (int level = 0; level < static_cast<int>(kLodColors.size()); ++level) {
        for (int y = 6; y < 16; ++y) {
            for (int x = 6 + level * 14; x < 6 + level * 14 + 11; ++x) {
                canvas.Blend(x, y, MapLodColor(level), 255);
            }
        }
    }
    const double barPixels = 1000.0 / mpp;
    canvas.Line(6.0, size - 8.0, 6.0 + barPixels, size - 8.0, { 255, 255, 255 });
    canvas.Line(6.0, size - 12.0, 6.0, size - 4.0, { 255, 255, 255 });
    canvas.Line(6.0 + barPixels, size - 12.0, 6.0 + barPixels, size - 4.0, { 255, 255, 255 });
    return image;
}

} // namespace gte::terrain
