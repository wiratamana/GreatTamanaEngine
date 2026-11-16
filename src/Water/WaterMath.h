#pragma once

// Pure geometry of water bodies: Catmull-Rom river paths, polyline queries, polygon distance.

#include "../Terrain/TerrainCarve.h"
#include "WaterTypes.h"

#include <span>
#include <vector>

namespace gte::water {

using terrain::Rect2;

// ---- Polygon (lake outline) ----

bool PointInPolygon(std::span<const Point2> poly, double x, double z) noexcept;

// Distance to the outline in metres: negative inside, positive outside.
double PolygonSignedDistance(std::span<const Point2> poly, double x, double z) noexcept;

Rect2 PolygonBounds(std::span<const Point2> poly) noexcept;

// Signed area (positive when counter-clockwise in x/z).
double PolygonArea(std::span<const Point2> poly) noexcept;

// True when two non-neighbouring edges cross.
bool PolygonSelfIntersects(std::span<const Point2> poly) noexcept;

// ---- River path ----

// One straight piece of the flattened river.
struct RiverSegment {
    double ax = 0.0;
    double az = 0.0;
    double bx = 0.0;
    double bz = 0.0;
    float ya = 0.0f;
    float yb = 0.0f;
    float wa = 0.0f; // Full width at a.
    float wb = 0.0f;
    float sa = 0.0f; // Distance along the river at a, metres.
    float sb = 0.0f;
};

// Catmull-Rom through the control points, split into pieces of at most `maxStep` metres.
// Height and width are interpolated linearly, so a falling surface never rises between points.
std::vector<RiverSegment> FlattenRiver(std::span<const RiverPoint> points, double maxStep);

// Lowers each point to the one before it so the surface never rises downstream.
void EnforceDescent(std::span<RiverPoint> points) noexcept;

// Closest point of a river polyline to (x, z).
struct RiverHit {
    double distance = 0.0; // Metres to the centre line.
    float surfaceY = 0.0f;
    float width = 0.0f;    // Full width at the hit.
    float along = 0.0f;    // Metres from the river start.
    int segment = -1;      // -1 when the path is empty.
};
RiverHit NearestOnRiver(std::span<const RiverSegment> segments, double x, double z) noexcept;

Rect2 RiverBounds(std::span<const RiverSegment> segments) noexcept;

// Smooth 0..1 ramp between two edges.
inline double SmoothStep(double e0, double e1, double v) noexcept
{
    const double t = e1 == e0 ? (v >= e1 ? 1.0 : 0.0) : (v - e0) / (e1 - e0);
    const double c = t < 0.0 ? 0.0 : (t > 1.0 ? 1.0 : t);
    return c * c * (3.0 - 2.0 * c);
}

} // namespace gte::water
