#include "WaterMath.h"

#include <algorithm>
#include <cmath>

namespace gte::water {

namespace {

struct V2 {
    double x;
    double z;
};

V2 CatmullRom(const V2& p0, const V2& p1, const V2& p2, const V2& p3, double t) noexcept
{
    const double t2 = t * t;
    const double t3 = t2 * t;
    auto axis = [&](double a, double b, double c, double d) {
        return 0.5 * ((2.0 * b) + (-a + c) * t + (2.0 * a - 5.0 * b + 4.0 * c - d) * t2 + (-a + 3.0 * b - 3.0 * c + d) * t3);
    };
    return { axis(p0.x, p1.x, p2.x, p3.x), axis(p0.z, p1.z, p2.z, p3.z) };
}

double DistanceToSegmentSq(double px, double pz, double ax, double az, double bx, double bz, double* tOut) noexcept
{
    const double dx = bx - ax;
    const double dz = bz - az;
    const double len2 = dx * dx + dz * dz;
    double t = 0.0;
    if (len2 > 1.0e-18) {
        t = std::clamp(((px - ax) * dx + (pz - az) * dz) / len2, 0.0, 1.0);
    }
    if (tOut != nullptr) {
        *tOut = t;
    }
    const double cx = ax + dx * t - px;
    const double cz = az + dz * t - pz;
    return cx * cx + cz * cz;
}

bool SegmentsCross(const Point2& a, const Point2& b, const Point2& c, const Point2& d) noexcept
{
    auto orient = [](const Point2& p, const Point2& q, const Point2& r) {
        return (q.x - p.x) * (r.z - p.z) - (q.z - p.z) * (r.x - p.x);
    };
    const double o1 = orient(a, b, c);
    const double o2 = orient(a, b, d);
    const double o3 = orient(c, d, a);
    const double o4 = orient(c, d, b);
    return ((o1 > 0.0) != (o2 > 0.0)) && ((o3 > 0.0) != (o4 > 0.0)) && o1 != 0.0 && o2 != 0.0 && o3 != 0.0 && o4 != 0.0;
}

} // namespace

bool PointInPolygon(std::span<const Point2> poly, double x, double z) noexcept
{
    bool inside = false;
    const std::size_t n = poly.size();
    for (std::size_t i = 0, j = n - 1; i < n; j = i++) {
        const Point2& a = poly[i];
        const Point2& b = poly[j];
        if (((a.z > z) != (b.z > z)) && (x < (b.x - a.x) * (z - a.z) / (b.z - a.z) + a.x)) {
            inside = !inside;
        }
    }
    return inside;
}

double PolygonSignedDistance(std::span<const Point2> poly, double x, double z) noexcept
{
    const std::size_t n = poly.size();
    if (n < 3) {
        return 1.0e30;
    }
    double best = 1.0e300;
    for (std::size_t i = 0, j = n - 1; i < n; j = i++) {
        best = std::min(best, DistanceToSegmentSq(x, z, poly[j].x, poly[j].z, poly[i].x, poly[i].z, nullptr));
    }
    const double d = std::sqrt(best);
    return PointInPolygon(poly, x, z) ? -d : d;
}

Rect2 PolygonBounds(std::span<const Point2> poly) noexcept
{
    Rect2 r{ 1.0e300, 1.0e300, -1.0e300, -1.0e300 };
    for (const Point2& p : poly) {
        r.minX = std::min(r.minX, p.x);
        r.minZ = std::min(r.minZ, p.z);
        r.maxX = std::max(r.maxX, p.x);
        r.maxZ = std::max(r.maxZ, p.z);
    }
    return r;
}

double PolygonArea(std::span<const Point2> poly) noexcept
{
    double a = 0.0;
    const std::size_t n = poly.size();
    for (std::size_t i = 0, j = n - 1; i < n; j = i++) {
        a += poly[j].x * poly[i].z - poly[i].x * poly[j].z;
    }
    return 0.5 * a;
}

bool PolygonSelfIntersects(std::span<const Point2> poly) noexcept
{
    const std::size_t n = poly.size();
    for (std::size_t i = 0; i < n; ++i) {
        for (std::size_t j = i + 2; j < n; ++j) {
            if (i == 0 && j == n - 1) {
                continue;
            }
            if (SegmentsCross(poly[i], poly[(i + 1) % n], poly[j], poly[(j + 1) % n])) {
                return true;
            }
        }
    }
    return false;
}

std::vector<RiverSegment> FlattenRiver(std::span<const RiverPoint> pts, double maxStep)
{
    std::vector<RiverSegment> out;
    const std::size_t n = pts.size();
    if (n < 2) {
        return out;
    }
    maxStep = std::max(maxStep, 0.5);
    double along = 0.0;
    for (std::size_t i = 0; i + 1 < n; ++i) {
        const V2 p0{ pts[i == 0 ? 0 : i - 1].x, pts[i == 0 ? 0 : i - 1].z };
        const V2 p1{ pts[i].x, pts[i].z };
        const V2 p2{ pts[i + 1].x, pts[i + 1].z };
        const V2 p3{ pts[std::min(i + 2, n - 1)].x, pts[std::min(i + 2, n - 1)].z };
        const double chord = std::hypot(p2.x - p1.x, p2.z - p1.z);
        const int steps = std::clamp(static_cast<int>(std::ceil(chord / maxStep)), 1, 64);
        V2 prev = p1;
        for (int k = 1; k <= steps; ++k) {
            const double t0 = static_cast<double>(k - 1) / steps;
            const double t1 = static_cast<double>(k) / steps;
            const V2 cur = k == steps ? p2 : CatmullRom(p0, p1, p2, p3, t1);
            RiverSegment s;
            s.ax = prev.x; s.az = prev.z; s.bx = cur.x; s.bz = cur.z;
            s.ya = pts[i].surfaceY + (pts[i + 1].surfaceY - pts[i].surfaceY) * static_cast<float>(t0);
            s.yb = pts[i].surfaceY + (pts[i + 1].surfaceY - pts[i].surfaceY) * static_cast<float>(t1);
            s.wa = pts[i].width + (pts[i + 1].width - pts[i].width) * static_cast<float>(t0);
            s.wb = pts[i].width + (pts[i + 1].width - pts[i].width) * static_cast<float>(t1);
            s.sa = static_cast<float>(along);
            along += std::hypot(cur.x - prev.x, cur.z - prev.z);
            s.sb = static_cast<float>(along);
            out.push_back(s);
            prev = cur;
        }
    }
    return out;
}

void EnforceDescent(std::span<RiverPoint> points) noexcept
{
    for (std::size_t i = 1; i < points.size(); ++i) {
        points[i].surfaceY = std::min(points[i].surfaceY, points[i - 1].surfaceY);
    }
}

RiverHit NearestOnRiver(std::span<const RiverSegment> segments, double x, double z) noexcept
{
    RiverHit hit;
    double best = 1.0e300;
    for (std::size_t i = 0; i < segments.size(); ++i) {
        const RiverSegment& s = segments[i];
        double t = 0.0;
        const double d2 = DistanceToSegmentSq(x, z, s.ax, s.az, s.bx, s.bz, &t);
        if (d2 < best) {
            best = d2;
            const float tf = static_cast<float>(t);
            hit.surfaceY = s.ya + (s.yb - s.ya) * tf;
            hit.width = s.wa + (s.wb - s.wa) * tf;
            hit.along = s.sa + (s.sb - s.sa) * tf;
            hit.segment = static_cast<int>(i);
        }
    }
    hit.distance = hit.segment >= 0 ? std::sqrt(best) : 1.0e30;
    return hit;
}

Rect2 RiverBounds(std::span<const RiverSegment> segments) noexcept
{
    Rect2 r{ 1.0e300, 1.0e300, -1.0e300, -1.0e300 };
    for (const RiverSegment& s : segments) {
        const double half = 0.5 * std::max(s.wa, s.wb);
        r.minX = std::min({ r.minX, s.ax - half, s.bx - half });
        r.minZ = std::min({ r.minZ, s.az - half, s.bz - half });
        r.maxX = std::max({ r.maxX, s.ax + half, s.bx + half });
        r.maxZ = std::max({ r.maxZ, s.az + half, s.bz + half });
    }
    return r;
}

} // namespace gte::water
