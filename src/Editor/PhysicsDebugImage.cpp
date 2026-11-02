#include "PhysicsDebugImage.h"

#include "OverlayRaster.h"
#include "ViewportProjection.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>

namespace gte {

namespace {

using Rgba = std::array<std::uint8_t, 4>;

constexpr Rgba kBackground{ 20, 22, 28, 255 };
constexpr Rgba kAnimColor{ 60, 200, 90, 255 };
constexpr Rgba kSimColor{ 230, 60, 60, 255 };
constexpr Rgba kColliderColor{ 80, 140, 255, 255 };
constexpr Rgba kLimitColor{ 255, 220, 60, 200 };
constexpr Rgba kHighlightColor{ 255, 0, 255, 255 };
constexpr Rgba kAnchorColor{ 255, 255, 255, 255 };

constexpr std::uint32_t kDefaultSize = 900;
constexpr std::uint32_t kMaxSize = 4096;
constexpr float kTriadLength = 30.0f;
constexpr float kTriadMargin = 25.0f;
constexpr float kPointArm = 2.0f;
constexpr float kAnchorArm = 3.0f;

Float3 Cross(const Float3& a, const Float3& b) noexcept
{
    return { a[1] * b[2] - a[2] * b[1], a[2] * b[0] - a[0] * b[2], a[0] * b[1] - a[1] * b[0] };
}

double Dot(const Float3& a, const Float3& b) noexcept
{
    return static_cast<double>(a[0]) * b[0] + static_cast<double>(a[1]) * b[1] + static_cast<double>(a[2]) * b[2];
}

bool IsFinite(const Float3& p) noexcept
{
    return std::isfinite(p[0]) && std::isfinite(p[1]) && std::isfinite(p[2]);
}

Rgba ColorOf(PhysicsDebugColorClass colorClass) noexcept
{
    switch (colorClass) {
    case PhysicsDebugColorClass::Anim: return kAnimColor;
    case PhysicsDebugColorClass::Sim: return kSimColor;
    case PhysicsDebugColorClass::Collider: return kColliderColor;
    case PhysicsDebugColorClass::Limit: return kLimitColor;
    case PhysicsDebugColorClass::Highlight: return kHighlightColor;
    case PhysicsDebugColorClass::Anchor: return kAnchorColor;
    }
    return kSimColor;
}

// Back to front: later classes overwrite earlier ones.
int DrawRank(PhysicsDebugColorClass colorClass) noexcept
{
    switch (colorClass) {
    case PhysicsDebugColorClass::Collider: return 0;
    case PhysicsDebugColorClass::Limit: return 1;
    case PhysicsDebugColorClass::Anim: return 2;
    case PhysicsDebugColorClass::Sim: return 3;
    case PhysicsDebugColorClass::Highlight: return 4;
    case PhysicsDebugColorClass::Anchor: return 5;
    }
    return 3;
}

struct Projection {
    PhysicsDebugViewBasis basis;
    double centerU = 0.0;
    double centerV = 0.0;
    double scale = 1.0;
    double halfWidth = 0.0;
    double halfHeight = 0.0;

    Vec2 ToScreen(const Float3& p) const noexcept
    {
        const double u = Dot(p, basis.right);
        const double v = Dot(p, basis.up);
        return Vec2(static_cast<float>(halfWidth + (u - centerU) * scale), static_cast<float>(halfHeight - (v - centerV) * scale));
    }
};

OverlaySegment2D MakeSegment(Vec2 a, Vec2 b, const Rgba& color) noexcept
{
    OverlaySegment2D segment;
    segment.a = a;
    segment.b = b;
    segment.rgba = color;
    return segment;
}

void AddCross(std::vector<OverlaySegment2D>& out, Vec2 center, float arm, const Rgba& color)
{
    out.push_back(MakeSegment(Vec2(center.x - arm, center.y), Vec2(center.x + arm, center.y), color));
    out.push_back(MakeSegment(Vec2(center.x, center.y - arm), Vec2(center.x, center.y + arm), color));
}

} // namespace

bool TryGetPhysicsDebugViewBasis(std::string_view viewName, PhysicsDebugViewBasis& out)
{
    Float3 forward{};
    Float3 up{ 0.0f, 1.0f, 0.0f };
    if (viewName == "negz") {
        forward = { 0.0f, 0.0f, 1.0f };
    } else if (viewName == "posz") {
        forward = { 0.0f, 0.0f, -1.0f };
    } else if (viewName == "posx") {
        forward = { -1.0f, 0.0f, 0.0f };
    } else if (viewName == "negx") {
        forward = { 1.0f, 0.0f, 0.0f };
    } else if (viewName == "top") {
        forward = { 0.0f, -1.0f, 0.0f };
        up = { 0.0f, 0.0f, 1.0f };
    } else {
        return false;
    }
    out.forward = forward;
    out.up = up;
    out.right = Cross(up, forward);
    return true;
}

PhysicsDebugImage RenderPhysicsDebugSnapshot(const PhysicsDebugSnapshot& snapshot)
{
    PhysicsDebugImage image;
    image.width = snapshot.width == 0 ? kDefaultSize : std::clamp(snapshot.width, 1u, kMaxSize);
    image.height = snapshot.height == 0 ? kDefaultSize : std::clamp(snapshot.height, 1u, kMaxSize);
    if (!TryGetPhysicsDebugViewBasis(snapshot.view, image.basis)) {
        TryGetPhysicsDebugViewBasis("negz", image.basis);
    }

    const int width = static_cast<int>(image.width);
    const int height = static_cast<int>(image.height);
    image.rgba.resize(static_cast<std::size_t>(width) * static_cast<std::size_t>(height) * 4u);
    for (std::size_t i = 0; i < image.rgba.size(); i += 4) {
        std::copy(kBackground.begin(), kBackground.end(), image.rgba.begin() + static_cast<std::ptrdiff_t>(i));
    }

    // Bounds of every finite point, in 3D and projected on the view axes.
    bool any = false;
    Float3 boundsMin{};
    Float3 boundsMax{};
    double minU = 0.0;
    double maxU = 0.0;
    double minV = 0.0;
    double maxV = 0.0;
    const auto include = [&](const Float3& p) {
        if (!IsFinite(p)) {
            return;
        }
        const double u = Dot(p, image.basis.right);
        const double v = Dot(p, image.basis.up);
        if (!any) {
            boundsMin = p;
            boundsMax = p;
            minU = maxU = u;
            minV = maxV = v;
            any = true;
            return;
        }
        for (std::size_t i = 0; i < 3; ++i) {
            boundsMin[i] = std::min(boundsMin[i], p[i]);
            boundsMax[i] = std::max(boundsMax[i], p[i]);
        }
        minU = std::min(minU, u);
        maxU = std::max(maxU, u);
        minV = std::min(minV, v);
        maxV = std::max(maxV, v);
    };
    for (const PhysicsDebugSegment& segment : snapshot.segments) {
        include(segment.a);
        include(segment.b);
    }
    for (const PhysicsDebugPoint& point : snapshot.points) {
        include(point.p);
    }
    image.boundsMin = boundsMin;
    image.boundsMax = boundsMax;

    // 10 percent margin on each side, one uniform scale, centered.
    double extentU = maxU - minU;
    double extentV = maxV - minV;
    if (!any || (extentU < 1.0e-6 && extentV < 1.0e-6)) {
        extentU = 1.0;
        extentV = 1.0;
    }
    extentU = std::max(extentU, 1.0e-6) * 1.2;
    extentV = std::max(extentV, 1.0e-6) * 1.2;

    Projection projection;
    projection.basis = image.basis;
    projection.centerU = (minU + maxU) * 0.5;
    projection.centerV = (minV + maxV) * 0.5;
    projection.scale = std::min(static_cast<double>(width) / extentU, static_cast<double>(height) / extentV);
    projection.halfWidth = width * 0.5;
    projection.halfHeight = height * 0.5;
    image.pixelsPerUnit = static_cast<float>(projection.scale);

    // Lines first, sorted back to front.
    std::vector<const PhysicsDebugSegment*> segmentOrder;
    segmentOrder.reserve(snapshot.segments.size());
    for (const PhysicsDebugSegment& segment : snapshot.segments) {
        segmentOrder.push_back(&segment);
    }
    std::stable_sort(segmentOrder.begin(), segmentOrder.end(),
        [](const PhysicsDebugSegment* a, const PhysicsDebugSegment* b) { return DrawRank(a->colorClass) < DrawRank(b->colorClass); });

    std::vector<OverlaySegment2D> lines;
    lines.reserve(segmentOrder.size());
    for (const PhysicsDebugSegment* segment : segmentOrder) {
        if (IsFinite(segment->a) && IsFinite(segment->b)) {
            lines.push_back(MakeSegment(projection.ToScreen(segment->a), projection.ToScreen(segment->b), ColorOf(segment->colorClass)));
        }
    }

    // Point crosses, anchors last.
    std::vector<const PhysicsDebugPoint*> pointOrder;
    pointOrder.reserve(snapshot.points.size());
    for (const PhysicsDebugPoint& point : snapshot.points) {
        pointOrder.push_back(&point);
    }
    std::stable_sort(pointOrder.begin(), pointOrder.end(),
        [](const PhysicsDebugPoint* a, const PhysicsDebugPoint* b) { return DrawRank(a->colorClass) < DrawRank(b->colorClass); });
    for (const PhysicsDebugPoint* point : pointOrder) {
        if (!IsFinite(point->p)) {
            continue;
        }
        const bool anchor = point->colorClass == PhysicsDebugColorClass::Anchor;
        AddCross(lines, projection.ToScreen(point->p), anchor ? kAnchorArm : kPointArm, ColorOf(point->colorClass));
    }

    // Axis triad in the lower left corner, projected with the same right/up.
    const Vec2 triadOrigin(kTriadMargin, static_cast<float>(height) - kTriadMargin);
    const std::array<Float3, 3> axes{ { { 1.0f, 0.0f, 0.0f }, { 0.0f, 1.0f, 0.0f }, { 0.0f, 0.0f, 1.0f } } };
    const std::array<Rgba, 3> axisColors{ { { 255, 0, 0, 255 }, { 0, 255, 0, 255 }, { 0, 0, 255, 255 } } };
    for (std::size_t i = 0; i < axes.size(); ++i) {
        const float dx = static_cast<float>(Dot(axes[i], image.basis.right)) * kTriadLength;
        const float dy = -static_cast<float>(Dot(axes[i], image.basis.up)) * kTriadLength;
        lines.push_back(MakeSegment(triadOrigin, Vec2(triadOrigin.x + dx, triadOrigin.y + dy), axisColors[i]));
    }

    RasterizeSegments(image.rgba, width, height, lines);
    return image;
}

} // namespace gte
