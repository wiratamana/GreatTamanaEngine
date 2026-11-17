#include "WaterEdit.h"

#include "WaterMath.h"

#include <algorithm>
#include <cmath>

namespace gte::water {

namespace {

bool FiniteCoord(double v) noexcept { return std::isfinite(v) && std::abs(v) <= kMaxWorldCoord; }

std::vector<Point2> OutlineSamples(const std::vector<Point2>& poly, int count)
{
    std::vector<Point2> out;
    const std::size_t n = poly.size();
    if (n < 3 || count <= 0) {
        return out;
    }
    double perimeter = 0.0;
    for (std::size_t i = 0; i < n; ++i) {
        perimeter += std::hypot(poly[(i + 1) % n].x - poly[i].x, poly[(i + 1) % n].z - poly[i].z);
    }
    const double step = perimeter / count;
    double next = 0.0;
    double accumulated = 0.0;
    for (std::size_t i = 0; i < n; ++i) {
        const Point2& a = poly[i];
        const Point2& b = poly[(i + 1) % n];
        const double len = std::hypot(b.x - a.x, b.z - a.z);
        while (next < accumulated + len && step > 0.0) {
            const double t = len > 0.0 ? (next - accumulated) / len : 0.0;
            out.push_back({ a.x + (b.x - a.x) * t, a.z + (b.z - a.z) * t });
            next += step;
        }
        accumulated += len;
    }
    return out;
}

} // namespace

WaterBody MakeBoxLake(const std::string& name, double cx, double cz, double sx, double sz, float level)
{
    WaterBody b;
    b.name = name;
    b.kind = WaterBodyKind::Lake;
    b.level = level;
    b.polygon = MakeBoxPolygon(cx, cz, sx, sz);
    b.look.waveScale = 0.35f;
    return b;
}

WaterBody MakeRiverBody(const std::string& name, std::vector<RiverPoint> points)
{
    WaterBody b;
    b.name = name;
    b.kind = WaterBodyKind::River;
    b.depth = 4.0f;
    b.river = std::move(points);
    b.look.waveScale = 0.2f;
    return b;
}

WaterBody MakeOceanBody(float level)
{
    WaterBody b;
    b.name = "Ocean";
    b.kind = WaterBodyKind::Ocean;
    b.level = level;
    b.carveTerrain = false;
    return b;
}

bool EditMovePoint(WaterBody& body, int index, double x, double z, std::optional<float> y, std::optional<float> w, std::string& error)
{
    if (!FiniteCoord(x) || !FiniteCoord(z)) {
        error = "position is not a finite world coordinate";
        return false;
    }
    if (body.kind == WaterBodyKind::Lake) {
        if (index < 0 || index >= static_cast<int>(body.polygon.size())) {
            error = "lake corner index out of range";
            return false;
        }
        body.polygon[static_cast<std::size_t>(index)] = { x, z };
        return true;
    }
    if (body.kind == WaterBodyKind::River) {
        if (index < 0 || index >= static_cast<int>(body.river.size())) {
            error = "river point index out of range";
            return false;
        }
        RiverPoint& p = body.river[static_cast<std::size_t>(index)];
        p.x = x;
        p.z = z;
        if (y && std::isfinite(*y)) {
            p.surfaceY = *y;
        }
        if (w && std::isfinite(*w)) {
            p.width = std::clamp(*w, 1.0f, 500.0f);
        }
        return true;
    }
    error = "an ocean has no points";
    return false;
}

bool EditInsertPoint(WaterBody& body, int index, double x, double z, std::string& error)
{
    if (!FiniteCoord(x) || !FiniteCoord(z)) {
        error = "position is not a finite world coordinate";
        return false;
    }
    if (body.kind == WaterBodyKind::Lake) {
        if (static_cast<int>(body.polygon.size()) >= kMaxLakeVertices) {
            error = "a lake has at most 32 corners";
            return false;
        }
        const int at = index < 0 ? static_cast<int>(body.polygon.size()) : std::min(index, static_cast<int>(body.polygon.size()));
        body.polygon.insert(body.polygon.begin() + at, Point2{ x, z });
        return true;
    }
    if (body.kind == WaterBodyKind::River) {
        if (static_cast<int>(body.river.size()) >= kMaxRiverPoints) {
            error = "a river has at most 64 points";
            return false;
        }
        const int n = static_cast<int>(body.river.size());
        const int at = index < 0 ? n : std::min(index, n);
        RiverPoint p{ x, z, 0.0f, 12.0f };
        if (n > 0) {
            const RiverPoint& before = body.river[static_cast<std::size_t>(std::max(0, at - 1))];
            const RiverPoint& after = body.river[static_cast<std::size_t>(std::min(n - 1, at))];
            p.surfaceY = at >= n ? before.surfaceY : 0.5f * (before.surfaceY + after.surfaceY);
            p.width = at >= n ? before.width : 0.5f * (before.width + after.width);
        }
        body.river.insert(body.river.begin() + at, p);
        return true;
    }
    error = "an ocean has no points";
    return false;
}

bool EditRemovePoint(WaterBody& body, int index, std::string& error)
{
    if (body.kind == WaterBodyKind::Lake) {
        if (static_cast<int>(body.polygon.size()) <= kMinLakeVertices) {
            error = "a lake needs at least 3 corners";
            return false;
        }
        if (index < 0 || index >= static_cast<int>(body.polygon.size())) {
            error = "lake corner index out of range";
            return false;
        }
        body.polygon.erase(body.polygon.begin() + index);
        return true;
    }
    if (body.kind == WaterBodyKind::River) {
        if (static_cast<int>(body.river.size()) <= kMinRiverPoints) {
            error = "a river needs at least 2 points";
            return false;
        }
        if (index < 0 || index >= static_cast<int>(body.river.size())) {
            error = "river point index out of range";
            return false;
        }
        body.river.erase(body.river.begin() + index);
        return true;
    }
    error = "an ocean has no points";
    return false;
}

void BodyCenter(const WaterBody& body, double& x, double& z, double& radius) noexcept
{
    Rect2 r{ 0.0, 0.0, 0.0, 0.0 };
    if (body.kind == WaterBodyKind::Lake && !body.polygon.empty()) {
        r = PolygonBounds(body.polygon);
    } else if (body.kind == WaterBodyKind::River && !body.river.empty()) {
        r = { 1.0e300, 1.0e300, -1.0e300, -1.0e300 };
        for (const RiverPoint& p : body.river) {
            r.minX = std::min(r.minX, p.x);
            r.minZ = std::min(r.minZ, p.z);
            r.maxX = std::max(r.maxX, p.x);
            r.maxZ = std::max(r.maxZ, p.z);
        }
    }
    x = 0.5 * (r.minX + r.maxX);
    z = 0.5 * (r.minZ + r.maxZ);
    radius = 0.5 * std::max(r.maxX - r.minX, r.maxZ - r.minZ);
}

void EditTranslate(WaterBody& body, double dx, double dz) noexcept
{
    for (Point2& p : body.polygon) {
        p.x += dx;
        p.z += dz;
    }
    for (RiverPoint& p : body.river) {
        p.x += dx;
        p.z += dz;
    }
}

LakeLeakReport CheckLakeLeaks(const WaterBody& lake, const HeightFn& ground)
{
    LakeLeakReport report;
    report.lowest = 1.0e30f;
    report.highest = -1.0e30f;
    for (const Point2& p : OutlineSamples(lake.polygon, 96)) {
        const float h = ground(p.x, p.z);
        ++report.samples;
        report.below += h < lake.level ? 1 : 0;
        report.lowest = std::min(report.lowest, h);
        report.highest = std::max(report.highest, h);
    }
    if (report.samples == 0) {
        report.lowest = report.highest = 0.0f;
    }
    return report;
}

float SuggestLakeLevel(const WaterBody& lake, const HeightFn& ground, float percentile)
{
    std::vector<float> heights;
    for (const Point2& p : OutlineSamples(lake.polygon, 96)) {
        heights.push_back(ground(p.x, p.z));
    }
    if (heights.empty()) {
        return lake.level;
    }
    std::sort(heights.begin(), heights.end());
    const std::size_t i = static_cast<std::size_t>(std::clamp(percentile, 0.0f, 1.0f) * static_cast<float>(heights.size() - 1));
    return heights[i];
}

void AutoRiverLevels(WaterBody& river, const HeightFn& ground, float freeboard)
{
    for (RiverPoint& p : river.river) {
        p.surfaceY = ground(p.x, p.z) - freeboard;
    }
    EnforceDescent(river.river);
}

} // namespace gte::water
