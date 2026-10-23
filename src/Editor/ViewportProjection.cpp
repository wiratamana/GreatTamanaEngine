#include "ViewportProjection.h"

#include <cmath>

namespace gte {

namespace {

bool IsFinite(const Vec3& v) noexcept
{
    return std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z);
}

bool IsFinite(const Vec4& v) noexcept
{
    return std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z) && std::isfinite(v.w);
}

} // namespace

Vec2 ClipToScreen(const Vec4& clip, Vec2 rectMin, Vec2 rectMax) noexcept
{
    const float ndcX = clip.x / clip.w;
    const float ndcY = clip.y / clip.w;
    return Vec2(rectMin.x + (ndcX * 0.5f + 0.5f) * (rectMax.x - rectMin.x),
        rectMin.y + (ndcY * 0.5f + 0.5f) * (rectMax.y - rectMin.y));
}

bool ProjectToScreen(const Vec3& point, const Mat4& viewProj, Vec2 rectMin, Vec2 rectMax, Vec2& out) noexcept
{
    if (!IsFinite(point)) {
        return false;
    }
    const Vec4 clip = viewProj * Vec4(point, 1.0f);
    if (!(clip.w > kMinClipW) || !IsFinite(clip)) {
        return false;
    }
    out = ClipToScreen(clip, rectMin, rectMax);
    return true;
}

bool ClipSegmentToView(const Mat4& viewProj, Vec3 a, Vec3 b, Vec4& ca, Vec4& cb) noexcept
{
    if (!IsFinite(a) || !IsFinite(b)) {
        return false;
    }
    Vec4 clipA = viewProj * Vec4(a, 1.0f);
    Vec4 clipB = viewProj * Vec4(b, 1.0f);
    if (!IsFinite(clipA) || !IsFinite(clipB)) {
        return false;
    }
    const bool frontA = clipA.w > kMinClipW;
    const bool frontB = clipB.w > kMinClipW;
    if (!frontA && !frontB) {
        return false;
    }
    if (!frontA) {
        const float t = (kMinClipW - clipA.w) / (clipB.w - clipA.w);
        clipA = clipA + (clipB - clipA) * t;
        clipA.w = kMinClipW;
    } else if (!frontB) {
        const float t = (kMinClipW - clipB.w) / (clipA.w - clipB.w);
        clipB = clipB + (clipA - clipB) * t;
        clipB.w = kMinClipW;
    }
    ca = clipA;
    cb = clipB;
    return true;
}

float PointToSegmentDistance(Vec2 p, Vec2 a, Vec2 b) noexcept
{
    const Vec2 ab = b - a;
    const float lengthSq = Dot(ab, ab);
    float t = 0.0f;
    if (lengthSq > 0.0f) {
        t = Clamp(Dot(p - a, ab) / lengthSq, 0.0f, 1.0f);
    }
    return Length(p - (a + ab * t));
}

void ProjectSegments(std::span<const FrustumSegment> segments, const Mat4& viewProj, Vec2 rectMin, Vec2 rectMax,
    std::array<std::uint8_t, 4> rgba, float width, std::vector<OverlaySegment2D>& out)
{
    out.reserve(out.size() + segments.size());
    for (const FrustumSegment& segment : segments) {
        Vec4 ca;
        Vec4 cb;
        if (!ClipSegmentToView(viewProj, segment.a, segment.b, ca, cb)) {
            continue;
        }
        OverlaySegment2D line;
        line.a = ClipToScreen(ca, rectMin, rectMax);
        line.b = ClipToScreen(cb, rectMin, rectMax);
        line.rgba = rgba;
        line.width = width;
        out.push_back(line);
    }
}

} // namespace gte
