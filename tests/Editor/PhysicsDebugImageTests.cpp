#include "Editor/PhysicsDebugImage.h"

#include <gtest/gtest.h>

#include <array>
#include <cstddef>
#include <cstdint>

namespace gte {
namespace {

using Rgba = std::array<std::uint8_t, 4>;

constexpr std::uint32_t kSize = 200;
constexpr Rgba kBackground{ 20, 22, 28, 255 };
constexpr Rgba kSim{ 230, 60, 60, 255 };
constexpr Rgba kAnim{ 60, 200, 90, 255 };
constexpr Rgba kCollider{ 80, 140, 255, 255 };
constexpr Rgba kHighlight{ 255, 0, 255, 255 };

Rgba PixelAt(const PhysicsDebugImage& image, int x, int y)
{
    const std::size_t i = (static_cast<std::size_t>(y) * image.width + static_cast<std::size_t>(x)) * 4u;
    return { image.rgba[i], image.rgba[i + 1], image.rgba[i + 2], image.rgba[i + 3] };
}

// True when any pixel within 1 px of (x, y) has exactly `color`.
bool HasColorNear(const PhysicsDebugImage& image, int x, int y, const Rgba& color)
{
    for (int dy = -1; dy <= 1; ++dy) {
        for (int dx = -1; dx <= 1; ++dx) {
            const int px = x + dx;
            const int py = y + dy;
            if (px >= 0 && py >= 0 && px < static_cast<int>(image.width) && py < static_cast<int>(image.height)
                && PixelAt(image, px, py) == color) {
                return true;
            }
        }
    }
    return false;
}

PhysicsDebugPoint Point(float x, float y, float z, PhysicsDebugColorClass colorClass = PhysicsDebugColorClass::Sim)
{
    return { { x, y, z }, colorClass };
}

PhysicsDebugSegment Segment(Float3 a, Float3 b, PhysicsDebugColorClass colorClass)
{
    return { a, b, colorClass };
}

PhysicsDebugSnapshot MakeSnapshot(const char* view)
{
    PhysicsDebugSnapshot snapshot;
    snapshot.valid = true;
    snapshot.view = view;
    snapshot.width = kSize;
    snapshot.height = kSize;
    return snapshot;
}

TEST(PhysicsDebugImageTest, ViewBasesMatchTheTable)
{
    struct Expected {
        const char* name;
        Float3 forward;
        Float3 right;
        Float3 up;
    };
    const Expected table[] = {
        { "negz", { 0, 0, 1 }, { 1, 0, 0 }, { 0, 1, 0 } },
        { "posz", { 0, 0, -1 }, { -1, 0, 0 }, { 0, 1, 0 } },
        { "posx", { -1, 0, 0 }, { 0, 0, 1 }, { 0, 1, 0 } },
        { "negx", { 1, 0, 0 }, { 0, 0, -1 }, { 0, 1, 0 } },
        { "top", { 0, -1, 0 }, { 1, 0, 0 }, { 0, 0, 1 } },
    };
    for (const Expected& e : table) {
        PhysicsDebugViewBasis basis;
        ASSERT_TRUE(TryGetPhysicsDebugViewBasis(e.name, basis)) << e.name;
        for (std::size_t i = 0; i < 3; ++i) {
            EXPECT_FLOAT_EQ(basis.forward[i], e.forward[i]) << e.name;
            EXPECT_FLOAT_EQ(basis.right[i], e.right[i]) << e.name;
            EXPECT_FLOAT_EQ(basis.up[i], e.up[i]) << e.name;
        }
    }
    PhysicsDebugViewBasis unused;
    EXPECT_FALSE(TryGetPhysicsDebugViewBasis("front", unused));
    EXPECT_FALSE(TryGetPhysicsDebugViewBasis("", unused));
}

TEST(PhysicsDebugImageTest, ThreePointsLandOnKnownPixelsInNegZView)
{
    PhysicsDebugSnapshot snapshot = MakeSnapshot("negz");
    snapshot.points = { Point(0, 0, 0), Point(10, 0, 0), Point(0, 5, 0) };
    const PhysicsDebugImage image = RenderPhysicsDebugSnapshot(snapshot);

    ASSERT_EQ(image.width, kSize);
    ASSERT_EQ(image.rgba.size(), static_cast<std::size_t>(kSize) * kSize * 4u);
    EXPECT_NEAR(image.pixelsPerUnit, 200.0f / 12.0f, 0.01f);
    EXPECT_TRUE(HasColorNear(image, 16, 141, kSim));
    EXPECT_TRUE(HasColorNear(image, 183, 141, kSim));
    EXPECT_TRUE(HasColorNear(image, 16, 58, kSim));
    EXPECT_FLOAT_EQ(image.boundsMax[0], 10.0f);
    EXPECT_FLOAT_EQ(image.boundsMax[1], 5.0f);
}

TEST(PhysicsDebugImageTest, ColliderAndLimitLinesDoNotChangeTheBounds)
{
    PhysicsDebugSnapshot snapshot = MakeSnapshot("negz");
    snapshot.points = { Point(0, 0, 0), Point(10, 5, 0) };
    snapshot.segments = { Segment({ -500, -500, 0 }, { 500, 500, 0 }, PhysicsDebugColorClass::Collider),
        Segment({ -900, 0, 0 }, { 900, 0, 0 }, PhysicsDebugColorClass::Limit) };
    const PhysicsDebugImage image = RenderPhysicsDebugSnapshot(snapshot);
    EXPECT_FLOAT_EQ(image.boundsMin[0], 0.0f);
    EXPECT_FLOAT_EQ(image.boundsMax[0], 10.0f);
    EXPECT_FLOAT_EQ(image.boundsMax[1], 5.0f);
}

TEST(PhysicsDebugImageTest, ThreePointsLandOnKnownPixelsInTopView)
{
    PhysicsDebugSnapshot snapshot = MakeSnapshot("top");
    snapshot.points = { Point(0, 7, 0), Point(10, 3, 0), Point(0, 9, 5) };
    const PhysicsDebugImage image = RenderPhysicsDebugSnapshot(snapshot);

    EXPECT_TRUE(HasColorNear(image, 16, 141, kSim));
    EXPECT_TRUE(HasColorNear(image, 183, 141, kSim));
    EXPECT_TRUE(HasColorNear(image, 16, 58, kSim));
}

TEST(PhysicsDebugImageTest, EmptySnapshotIsBackgroundPlusTriad)
{
    PhysicsDebugSnapshot snapshot = MakeSnapshot("negz");
    snapshot.valid = false;
    const PhysicsDebugImage image = RenderPhysicsDebugSnapshot(snapshot);

    int drawn = 0;
    for (int y = 0; y < static_cast<int>(image.height); ++y) {
        for (int x = 0; x < static_cast<int>(image.width); ++x) {
            if (PixelAt(image, x, y) == kBackground) {
                continue;
            }
            ++drawn;
            EXPECT_GE(x, 20) << x << "," << y;
            EXPECT_LE(x, 60) << x << "," << y;
            EXPECT_GE(y, static_cast<int>(kSize) - 60) << x << "," << y;
            EXPECT_LE(y, static_cast<int>(kSize) - 20) << x << "," << y;
        }
    }
    EXPECT_GT(drawn, 30);
    EXPECT_EQ(PixelAt(image, 100, 100), kBackground);
}

TEST(PhysicsDebugImageTest, UnknownViewFallsBackToNegZ)
{
    PhysicsDebugSnapshot snapshot = MakeSnapshot("nonsense");
    const PhysicsDebugImage image = RenderPhysicsDebugSnapshot(snapshot);
    EXPECT_FLOAT_EQ(image.basis.forward[2], 1.0f);
    EXPECT_FLOAT_EQ(image.basis.right[0], 1.0f);
}

TEST(PhysicsDebugImageTest, HighlightWinsOverSimAtTheSamePixel)
{
    PhysicsDebugSnapshot snapshot = MakeSnapshot("negz");
    // Highlight is listed first on purpose: draw order must not depend on list order.
    snapshot.segments = { Segment({ 0, 0, 0 }, { 10, 0, 0 }, PhysicsDebugColorClass::Highlight),
        Segment({ 0, 0, 0 }, { 10, 0, 0 }, PhysicsDebugColorClass::Sim) };
    const PhysicsDebugImage image = RenderPhysicsDebugSnapshot(snapshot);
    EXPECT_EQ(PixelAt(image, 100, 100), kHighlight);

    snapshot.segments.erase(snapshot.segments.begin());
    EXPECT_EQ(PixelAt(RenderPhysicsDebugSnapshot(snapshot), 100, 100), kSim);
}

TEST(PhysicsDebugImageTest, SimIsDrawnOverAnimAndColliderIsUnderneath)
{
    PhysicsDebugSnapshot snapshot = MakeSnapshot("negz");
    snapshot.segments = { Segment({ 0, 0, 0 }, { 10, 0, 0 }, PhysicsDebugColorClass::Sim),
        Segment({ 0, 0, 0 }, { 10, 0, 0 }, PhysicsDebugColorClass::Anim),
        Segment({ 0, 0, 0 }, { 10, 0, 0 }, PhysicsDebugColorClass::Collider) };
    EXPECT_EQ(PixelAt(RenderPhysicsDebugSnapshot(snapshot), 100, 100), kSim);

    snapshot.segments.erase(snapshot.segments.begin());
    EXPECT_EQ(PixelAt(RenderPhysicsDebugSnapshot(snapshot), 100, 100), kAnim);

    snapshot.segments.erase(snapshot.segments.begin());
    EXPECT_EQ(PixelAt(RenderPhysicsDebugSnapshot(snapshot), 100, 100), kCollider);
}

TEST(PhysicsDebugImageTest, AnchorIsAWhiteCrossAndDrawnLast)
{
    PhysicsDebugSnapshot snapshot = MakeSnapshot("negz");
    snapshot.points = { Point(5, 0, 0, PhysicsDebugColorClass::Anchor), Point(5, 0, 0, PhysicsDebugColorClass::Sim) };
    snapshot.segments = { Segment({ 0, 0, 0 }, { 10, 0, 0 }, PhysicsDebugColorClass::Highlight) };
    const PhysicsDebugImage image = RenderPhysicsDebugSnapshot(snapshot);
    EXPECT_EQ(PixelAt(image, 100, 100), (Rgba{ 255, 255, 255, 255 }));
}

} // namespace
} // namespace gte
