#include "WaterGizmoMath.h"

#include "../ViewportProjection.h"

#include <algorithm>
#include <cmath>

namespace gte {

namespace {

constexpr float kHandleLift = 0.4f; // Handles float a little above the water so the surface does not hide them.

bool Unproject(const Mat4& inverse, float ndcX, float ndcY, float ndcZ, double out[3])
{
    const Vec4 p = inverse * Vec4(ndcX, ndcY, ndcZ, 1.0f);
    if (!(std::abs(p.w) > 1.0e-12f) || !std::isfinite(p.w)) {
        return false;
    }
    out[0] = static_cast<double>(p.x) / p.w;
    out[1] = static_cast<double>(p.y) / p.w;
    out[2] = static_cast<double>(p.z) / p.w;
    return std::isfinite(out[0]) && std::isfinite(out[1]) && std::isfinite(out[2]);
}

Vec3 HandleWorld(const water::WaterBody& body, int index, std::size_t count)
{
    static_cast<void>(count);
    if (body.kind == water::WaterBodyKind::Lake) {
        const water::Point2& p = body.polygon[static_cast<std::size_t>(index)];
        return Vec3(static_cast<float>(p.x), body.level + kHandleLift, static_cast<float>(p.z));
    }
    const water::RiverPoint& p = body.river[static_cast<std::size_t>(index)];
    return Vec3(static_cast<float>(p.x), p.surfaceY + kHandleLift, static_cast<float>(p.z));
}

std::size_t PointCount(const water::WaterBody& body) noexcept
{
    return body.kind == water::WaterBodyKind::Lake ? body.polygon.size() : (body.kind == water::WaterBodyKind::River ? body.river.size() : 0);
}

} // namespace

bool ScreenToRay(const Mat4& viewProjection, Vec2 rectMin, Vec2 rectMax, Vec2 pixel, WaterRay& out)
{
    const float width = rectMax.x - rectMin.x;
    const float height = rectMax.y - rectMin.y;
    if (!(width > 0.0f) || !(height > 0.0f)) {
        return false;
    }
    Mat4 inverse;
    if (!viewProjection.TryInverse(inverse)) {
        return false;
    }
    // The engine matrix is flipY, so the top pixel row is ndc.y = -1 and no flip is needed here.
    const float ndcX = 2.0f * (pixel.x - rectMin.x) / width - 1.0f;
    const float ndcY = 2.0f * (pixel.y - rectMin.y) / height - 1.0f;
    double nearPoint[3];
    double farPoint[3];
    if (!Unproject(inverse, ndcX, ndcY, 0.0f, nearPoint) || !Unproject(inverse, ndcX, ndcY, 1.0f, farPoint)) {
        return false;
    }
    double d[3] = { farPoint[0] - nearPoint[0], farPoint[1] - nearPoint[1], farPoint[2] - nearPoint[2] };
    const double length = std::sqrt(d[0] * d[0] + d[1] * d[1] + d[2] * d[2]);
    if (!(length > 1.0e-9)) {
        return false;
    }
    for (int i = 0; i < 3; ++i) {
        out.origin[i] = nearPoint[i];
        out.direction[i] = d[i] / length;
    }
    return true;
}

bool RayHitPlaneY(const WaterRay& ray, double y, double& x, double& z)
{
    if (std::abs(ray.direction[1]) < 1.0e-9) {
        return false;
    }
    const double t = (y - ray.origin[1]) / ray.direction[1];
    if (!(t >= 0.0) || !std::isfinite(t)) {
        return false;
    }
    x = ray.origin[0] + ray.direction[0] * t;
    z = ray.origin[2] + ray.direction[2] * t;
    return true;
}

bool RayHitGround(const WaterRay& ray, const water::HeightFn& ground, double maxDistance, double& x, double& y, double& z)
{
    const auto above = [&](double t) {
        const double px = ray.origin[0] + ray.direction[0] * t;
        const double pz = ray.origin[2] + ray.direction[2] * t;
        return ray.origin[1] + ray.direction[1] * t - static_cast<double>(ground(px, pz));
    };
    constexpr int kSteps = 96;
    double previous = 0.0;
    double previousHeight = above(0.0);
    if (previousHeight <= 0.0) {
        return false; // Starts below the ground.
    }
    for (int i = 1; i <= kSteps; ++i) {
        // Steps grow with distance: fine near the camera, coarse far away.
        const double t = maxDistance * static_cast<double>(i * i) / static_cast<double>(kSteps * kSteps);
        const double h = above(t);
        if (h <= 0.0) {
            double lo = previous;
            double hi = t;
            for (int k = 0; k < 24; ++k) {
                const double mid = 0.5 * (lo + hi);
                (above(mid) > 0.0 ? lo : hi) = mid;
            }
            const double hit = 0.5 * (lo + hi);
            x = ray.origin[0] + ray.direction[0] * hit;
            y = ray.origin[1] + ray.direction[1] * hit;
            z = ray.origin[2] + ray.direction[2] * hit;
            return true;
        }
        previous = t;
        previousHeight = h;
    }
    static_cast<void>(previousHeight);
    return false;
}

void BuildWaterHandles(const water::WaterBody& body, const Mat4& viewProjection, Vec2 rectMin, Vec2 rectMax, std::vector<WaterHandle>& out)
{
    const std::size_t n = PointCount(body);
    if (n == 0) {
        return;
    }
    const bool closed = body.kind == water::WaterBodyKind::Lake;
    for (std::size_t i = 0; i < n; ++i) {
        WaterHandle h;
        h.bodyId = body.id;
        h.index = static_cast<int>(i);
        h.world = HandleWorld(body, static_cast<int>(i), n);
        if (ProjectToScreen(h.world, viewProjection, rectMin, rectMax, h.screen)) {
            out.push_back(h);
        }
        if (i + 1 < n || closed) {
            const std::size_t next = (i + 1) % n;
            const Vec3 a = h.world;
            const Vec3 b = HandleWorld(body, static_cast<int>(next), n);
            WaterHandle m;
            m.bodyId = body.id;
            m.index = static_cast<int>(i) + 1;
            m.midpoint = true;
            m.world = Vec3(0.5f * (a.x + b.x), 0.5f * (a.y + b.y), 0.5f * (a.z + b.z));
            if (ProjectToScreen(m.world, viewProjection, rectMin, rectMax, m.screen)) {
                out.push_back(m);
            }
        }
    }
}

int PickWaterHandle(std::span<const WaterHandle> handles, Vec2 pixel, float radius)
{
    int best = -1;
    float bestDistance = radius;
    bool bestIsCorner = false;
    for (std::size_t i = 0; i < handles.size(); ++i) {
        const float d = Length(handles[i].screen - pixel);
        if (d > radius) {
            continue;
        }
        const bool corner = !handles[i].midpoint;
        if (best < 0 || (corner && !bestIsCorner) || (corner == bestIsCorner && d < bestDistance)) {
            best = static_cast<int>(i);
            bestDistance = d;
            bestIsCorner = corner;
        }
    }
    return best;
}

int NearestOutlineInsertIndex(const water::WaterBody& body, const Mat4& viewProjection, Vec2 rectMin, Vec2 rectMax, Vec2 pixel,
    float& distancePixels)
{
    const std::size_t n = PointCount(body);
    const bool closed = body.kind == water::WaterBodyKind::Lake;
    int best = -1;
    distancePixels = 1.0e30f;
    for (std::size_t i = 0; i < n && (i + 1 < n || closed); ++i) {
        Vec2 a;
        Vec2 b;
        const std::size_t next = (i + 1) % n;
        if (!ProjectToScreen(HandleWorld(body, static_cast<int>(i), n), viewProjection, rectMin, rectMax, a)
            || !ProjectToScreen(HandleWorld(body, static_cast<int>(next), n), viewProjection, rectMin, rectMax, b)) {
            continue;
        }
        const float d = PointToSegmentDistance(pixel, a, b);
        if (d < distancePixels) {
            distancePixels = d;
            best = static_cast<int>(i) + 1;
        }
    }
    return best;
}

double HandlePlaneY(const water::WaterBody& body, int index) noexcept
{
    if (body.kind == water::WaterBodyKind::River && index >= 0 && index < static_cast<int>(body.river.size())) {
        return body.river[static_cast<std::size_t>(index)].surfaceY;
    }
    return body.level;
}

} // namespace gte
