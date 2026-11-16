#include "WaterMapImage.h"

#include <algorithm>
#include <cmath>

namespace gte::water {

namespace {

struct Rgb {
    float r;
    float g;
    float b;
};

Rgb Mix(const Rgb& a, const Rgb& b, float t) noexcept
{
    return { a.r + (b.r - a.r) * t, a.g + (b.g - a.g) * t, a.b + (b.b - a.b) * t };
}

Rgb LandColor(float h) noexcept
{
    const Rgb sand{ 0.78f, 0.72f, 0.52f };
    const Rgb grass{ 0.30f, 0.50f, 0.22f };
    const Rgb rock{ 0.45f, 0.42f, 0.40f };
    const Rgb snow{ 0.95f, 0.95f, 0.97f };
    if (h < 8.0f) {
        return sand;
    }
    if (h < 500.0f) {
        return Mix(grass, rock, std::clamp((h - 150.0f) / 350.0f, 0.0f, 1.0f));
    }
    return Mix(rock, snow, std::clamp((h - 600.0f) / 500.0f, 0.0f, 1.0f));
}

void Put(WaterMapImage& img, int x, int y, const Rgb& c) noexcept
{
    if (x < 0 || y < 0 || x >= img.size || y >= img.size) {
        return;
    }
    std::uint8_t* p = &img.rgba[(static_cast<std::size_t>(y) * static_cast<std::size_t>(img.size) + static_cast<std::size_t>(x)) * 4];
    p[0] = static_cast<std::uint8_t>(std::clamp(c.r, 0.0f, 1.0f) * 255.0f + 0.5f);
    p[1] = static_cast<std::uint8_t>(std::clamp(c.g, 0.0f, 1.0f) * 255.0f + 0.5f);
    p[2] = static_cast<std::uint8_t>(std::clamp(c.b, 0.0f, 1.0f) * 255.0f + 0.5f);
    p[3] = 255;
}

void Line(WaterMapImage& img, double x0, double y0, double x1, double y1, const Rgb& c, int thickness)
{
    const double len = std::hypot(x1 - x0, y1 - y0);
    const int steps = std::max(1, static_cast<int>(std::ceil(len)));
    const int r = std::max(0, thickness / 2);
    for (int i = 0; i <= steps; ++i) {
        const double t = static_cast<double>(i) / steps;
        const int cx = static_cast<int>(std::lround(x0 + (x1 - x0) * t));
        const int cy = static_cast<int>(std::lround(y0 + (y1 - y0) * t));
        for (int dy = -r; dy <= r; ++dy) {
            for (int dx = -r; dx <= r; ++dx) {
                Put(img, cx + dx, cy + dy, c);
            }
        }
    }
}

void Dot(WaterMapImage& img, double x, double y, const Rgb& c, int radius)
{
    for (int dy = -radius; dy <= radius; ++dy) {
        for (int dx = -radius; dx <= radius; ++dx) {
            if (dx * dx + dy * dy <= radius * radius) {
                Put(img, static_cast<int>(std::lround(x)) + dx, static_cast<int>(std::lround(y)) + dy, c);
            }
        }
    }
}

} // namespace

bool WorldToMapPixel(const WaterMapView& view, double x, double z, double& px, double& py) noexcept
{
    const double scale = static_cast<double>(view.size) / (2.0 * view.halfRange);
    px = (x - (view.centerX - view.halfRange)) * scale;
    py = ((view.centerZ + view.halfRange) - z) * scale;
    return px >= 0.0 && py >= 0.0 && px < view.size && py < view.size;
}

WaterMapImage RenderWaterMap(const WaterBodySet& set, const HeightFn& ground, const WaterMapView& viewIn, const WaterMapOptions& options)
{
    WaterMapView view = viewIn;
    view.size = std::clamp(view.size, 64, 2048);
    view.halfRange = std::clamp(view.halfRange, 8.0, 100000.0);
    WaterMapImage img;
    img.size = view.size;
    img.rgba.assign(static_cast<std::size_t>(view.size) * static_cast<std::size_t>(view.size) * 4, 255);

    const int n = view.size;
    const double metresPerPixel = 2.0 * view.halfRange / n;
    const int stride = n + 2;
    std::vector<float> heights(static_cast<std::size_t>(stride) * static_cast<std::size_t>(stride));
    for (int j = 0; j < stride; ++j) {
        const double z = view.centerZ + view.halfRange - (j - 0.5) * metresPerPixel;
        for (int i = 0; i < stride; ++i) {
            heights[static_cast<std::size_t>(j) * stride + i] = ground(view.centerX - view.halfRange + (i - 0.5) * metresPerPixel, z);
        }
    }

    const float wetBand = set.Settings().wetBand;
    const Rgb shallow{ set.Settings().shallowColor[0], set.Settings().shallowColor[1], set.Settings().shallowColor[2] };
    const Rgb deep{ set.Settings().deepColor[0], set.Settings().deepColor[1], set.Settings().deepColor[2] };
    float oceanLevel = 0.0f;
    bool hasOcean = false;
    for (const WaterBody& b : set.Bodies()) {
        if (b.kind == WaterBodyKind::Ocean) {
            oceanLevel = b.level;
            hasOcean = true;
        }
    }

    for (int py = 0; py < n; ++py) {
        const double z = view.centerZ + view.halfRange - (py + 0.5) * metresPerPixel;
        for (int px = 0; px < n; ++px) {
            const double x = view.centerX - view.halfRange + (px + 0.5) * metresPerPixel;
            const float* c = &heights[static_cast<std::size_t>(py + 1) * stride + (px + 1)];
            const float h = c[0];
            const double gx = (c[1] - c[-1]) / (2.0 * metresPerPixel);
            const double gz = (c[-stride] - c[stride]) / (2.0 * metresPerPixel);
            const double shade = std::clamp(0.75 + 0.9 * (-gx * 0.5 + gz * 0.5) / std::sqrt(1.0 + gx * gx + gz * gz), 0.35, 1.25);

            const WaterSample w = set.SampleAt(x, z);
            const bool wet = w.hasWater && (w.kind != WaterBodyKind::Ocean || h < w.surfaceY);
            Rgb color;
            if (wet) {
                const float depth = std::max(0.0f, w.surfaceY - h);
                color = Mix(shallow, deep, std::clamp(depth / std::max(1.0f, set.Settings().clarity), 0.0f, 1.0f));
                color = { color.r * 0.8f + 0.2f * static_cast<float>(shade), color.g * 0.8f + 0.2f * static_cast<float>(shade), color.b * 0.8f + 0.2f * static_cast<float>(shade) };
                ++img.waterPixels;
            } else {
                const Rgb base = LandColor(h);
                color = { base.r * static_cast<float>(shade), base.g * static_cast<float>(shade), base.b * static_cast<float>(shade) };
                ++img.landPixels;
                if (options.shoreBand && hasOcean && h >= oceanLevel && h < oceanLevel + wetBand) {
                    color = Mix(color, Rgb{ 1.0f, 0.9f, 0.4f }, 0.55f);
                    ++img.shorePixels;
                }
            }
            Put(img, px, py, color);
        }
    }

    if (options.outlines) {
        const Rgb lakeLine{ 1.0f, 1.0f, 1.0f };
        const Rgb riverLine{ 1.0f, 0.95f, 0.2f };
        const Rgb handle{ 1.0f, 0.2f, 0.1f };
        for (std::size_t bi = 0; bi < set.Bodies().size(); ++bi) {
            const WaterBody& b = set.Bodies()[bi];
            if (b.kind == WaterBodyKind::Lake) {
                const std::size_t count = b.polygon.size();
                for (std::size_t i = 0; i < count; ++i) {
                    double ax, ay, bx, by;
                    WorldToMapPixel(view, b.polygon[i].x, b.polygon[i].z, ax, ay);
                    WorldToMapPixel(view, b.polygon[(i + 1) % count].x, b.polygon[(i + 1) % count].z, bx, by);
                    Line(img, ax, ay, bx, by, lakeLine, 1);
                }
                for (const Point2& p : b.polygon) {
                    double ax, ay;
                    WorldToMapPixel(view, p.x, p.z, ax, ay);
                    Dot(img, ax, ay, handle, 3);
                }
            } else if (b.kind == WaterBodyKind::River) {
                const auto segs = set.Segments(bi);
                for (const RiverSegment& s : segs) {
                    double ax, ay, bx, by;
                    WorldToMapPixel(view, s.ax, s.az, ax, ay);
                    WorldToMapPixel(view, s.bx, s.bz, bx, by);
                    Line(img, ax, ay, bx, by, riverLine, 1);
                }
                for (const RiverPoint& p : b.river) {
                    double ax, ay;
                    WorldToMapPixel(view, p.x, p.z, ax, ay);
                    Dot(img, ax, ay, handle, 3);
                }
            }
        }
    }
    return img;
}

} // namespace gte::water
