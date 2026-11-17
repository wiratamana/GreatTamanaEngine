// Body packing, polygon and river queries of the Water feature.

#include "Features/Water/WaterBodyDesc.h"
#include "Features/Water/WaterMath.h"

#include <gtest/gtest.h>

#include <cmath>
#include <cstddef>
#include <vector>

namespace gte {
namespace {

WaterBodyPoint P(float x, float z, float level = 0.0f, float halfWidth = 0.0f)
{
    WaterBodyPoint p;
    p.xz = Vec2(x, z);
    p.level = level;
    p.halfWidth = halfWidth;
    return p;
}

std::vector<WaterBodyPoint> Square()
{
    return { P(0, 0), P(10, 0), P(10, 10), P(0, 10) };
}

TEST(WaterBodyTest, PolygonContainsUsesTheEvenOddRule)
{
    const std::vector<WaterBodyPoint> square = Square();
    EXPECT_TRUE(WaterPointInPolygon(square, Vec2(5, 5)));
    EXPECT_FALSE(WaterPointInPolygon(square, Vec2(-1, 5)));
    EXPECT_FALSE(WaterPointInPolygon(square, Vec2(5, 11)));

    // An L shape: the notch is outside.
    const std::vector<WaterBodyPoint> ell = { P(0, 0), P(10, 0), P(10, 4), P(4, 4), P(4, 10), P(0, 10) };
    EXPECT_TRUE(WaterPointInPolygon(ell, Vec2(2, 8)));
    EXPECT_FALSE(WaterPointInPolygon(ell, Vec2(8, 8)));
    EXPECT_FALSE(WaterPointInPolygon(std::vector<WaterBodyPoint>{ P(0, 0), P(1, 1) }, Vec2(0.2f, 0.2f)));
}

TEST(WaterBodyTest, RiverQueryGivesSignedDistanceLevelAndTangent)
{
    const std::vector<WaterBodyPoint> river = { P(0, 0, 10.0f, 4.0f), P(100, 0, 6.0f, 8.0f) };
    float level = 0.0f;
    float halfWidth = 0.0f;
    Vec2 tangent;

    const float inside = WaterRiverQuery(river, Vec2(50, 2), level, halfWidth, tangent);
    EXPECT_NEAR(halfWidth, 6.0f, 1e-4f);
    EXPECT_NEAR(level, 8.0f, 1e-4f);
    EXPECT_NEAR(inside, 2.0f - 6.0f, 1e-4f);
    EXPECT_NEAR(tangent.x, 1.0f, 1e-5f);

    const float outside = WaterRiverQuery(river, Vec2(50, 20), level, halfWidth, tangent);
    EXPECT_GT(outside, 0.0f);

    // Past the end the nearest point is the end point.
    const float beyond = WaterRiverQuery(river, Vec2(110, 0), level, halfWidth, tangent);
    EXPECT_NEAR(beyond, 10.0f - 8.0f, 1e-4f);
    EXPECT_NEAR(level, 6.0f, 1e-4f);
}

TEST(WaterBodyTest, SplineSamplesPassThroughTheControlPointsAndRespectTheCap)
{
    const std::vector<WaterBodyPoint> control = { P(0, 0, 5, 3), P(100, 50, 4, 4), P(200, 0, 3, 5) };
    const std::vector<WaterBodyPoint> line = WaterSampleSpline(control, 8);
    ASSERT_EQ(line.size(), 2u * 8u + 1u);
    EXPECT_EQ(line.front().xz, control.front().xz);
    EXPECT_EQ(line.back().xz, control.back().xz);
    EXPECT_NEAR(line[8].xz.x, 100.0f, 1e-3f);
    EXPECT_NEAR(line[8].level, 4.0f, 1e-4f);

    std::vector<WaterBodyPoint> many;
    for (int i = 0; i < 20; ++i) {
        many.push_back(P(static_cast<float>(i) * 10.0f, std::sin(static_cast<float>(i)) * 20.0f, 1.0f, 2.0f));
    }
    const std::vector<WaterBodyPoint> capped = WaterSampleSpline(many, 32);
    EXPECT_EQ(capped.size(), kWaterMaxRiverPoints);
    EXPECT_EQ(capped.front().xz, many.front().xz);
    EXPECT_EQ(capped.back().xz, many.back().xz);
}

TEST(WaterBodyTest, EmptyListPacksOneImplicitOceanAtTheSeaLevel)
{
    std::uint32_t count = 0;
    const WaterBodiesGpu packed = PackWaterBodies({}, 7.5f, count);
    EXPECT_EQ(count, 1u);
    EXPECT_EQ(packed.counts[0], 1u);
    EXPECT_FLOAT_EQ(packed.headers[0].info[0], 0.0f);
    EXPECT_FLOAT_EQ(packed.headers[0].info[1], 7.5f);
    EXPECT_TRUE(WaterAnyBodyBelowEye(packed, 10.0f));
    EXPECT_FALSE(WaterAnyBodyBelowEye(packed, 7.0f));
}

TEST(WaterBodyTest, PackBuildsBoundsPointRangesAndDropsInvalidBodies)
{
    std::vector<WaterBodyDesc> bodies(4);
    bodies[0].kind = WaterBodyKind::LakeBox;
    bodies[0].level = 12.0f;
    bodies[0].boxMin = Vec2(10, 20);
    bodies[0].boxMax = Vec2(30, 60);

    bodies[1].kind = WaterBodyKind::LakePolygon;
    bodies[1].level = 5.0f;
    bodies[1].points = Square();

    bodies[2].kind = WaterBodyKind::LakePolygon; // Two points: invalid.
    bodies[2].points = { P(0, 0), P(1, 1) };

    bodies[3].kind = WaterBodyKind::River;
    bodies[3].level = 3.0f;
    bodies[3].flowSpeed = 2.0f;
    bodies[3].points = { P(0, 0, 3, 5), P(100, 0, 2, 5) };

    std::uint32_t count = 0;
    const WaterBodiesGpu packed = PackWaterBodies(bodies, 0.0f, count);

    EXPECT_EQ(count, 3u);
    EXPECT_FLOAT_EQ(packed.headers[0].bounds[0], 10.0f);
    EXPECT_FLOAT_EQ(packed.headers[0].bounds[3], 60.0f);
    EXPECT_EQ(packed.headers[1].range[0], 0u);
    EXPECT_EQ(packed.headers[1].range[1], 4u);
    EXPECT_FLOAT_EQ(packed.headers[2].info[0], 3.0f);
    EXPECT_EQ(packed.headers[2].range[0], 4u);
    EXPECT_FLOAT_EQ(packed.headers[2].bounds[0], -5.0f);
    EXPECT_FLOAT_EQ(packed.headers[2].bounds[2], 105.0f);
    EXPECT_FLOAT_EQ(packed.headers[2].shape[0], 2.0f);
    EXPECT_FLOAT_EQ(packed.headers[2].shape[1], 2.0f);
    EXPECT_FLOAT_EQ(packed.headers[2].shape[2], 3.0f);
    EXPECT_FLOAT_EQ(packed.headers[0].shape[1], 12.0f);
    EXPECT_FLOAT_EQ(packed.points[5][0], 100.0f);
    EXPECT_FLOAT_EQ(packed.points[5][2], 2.0f);
    EXPECT_FLOAT_EQ(packed.points[5][3], 5.0f);
}

TEST(WaterBodyTest, PackCapsBodiesAndRejectsNonFiniteInput)
{
    std::vector<WaterBodyDesc> bodies(kWaterMaxBodies + 4);
    std::uint32_t count = 0;
    EXPECT_EQ(PackWaterBodies(bodies, 0.0f, count).counts[0], kWaterMaxBodies);
    EXPECT_EQ(count, kWaterMaxBodies);

    std::vector<WaterBodyDesc> bad(1);
    bad[0].level = std::nanf("");
    PackWaterBodies(bad, 0.0f, count);
    EXPECT_EQ(count, 0u);

    std::vector<WaterBodyDesc> inverted(1);
    inverted[0].kind = WaterBodyKind::LakeBox;
    inverted[0].boxMin = Vec2(5, 5);
    inverted[0].boxMax = Vec2(1, 1);
    PackWaterBodies(inverted, 0.0f, count);
    EXPECT_EQ(count, 0u);
}

TEST(WaterBodyTest, PackSharesTheFixedPointPool)
{
    std::vector<WaterBodyDesc> bodies;
    for (int i = 0; i < 12; ++i) {
        WaterBodyDesc d;
        d.kind = WaterBodyKind::LakePolygon;
        for (int k = 0; k < 30; ++k) {
            d.points.push_back(P(static_cast<float>(k), static_cast<float>(k * k % 7)));
        }
        bodies.push_back(d);
    }
    std::uint32_t count = 0;
    PackWaterBodies(bodies, 0.0f, count);
    EXPECT_EQ(count, kWaterMaxPoints / 30u); // 8 bodies of 30 points fit in 256.
}

TEST(WaterBodyTest, ProbeChoosesTheHighestCoveringBodyAndAddsTheWaveHeight)
{
    std::vector<WaterBodyDesc> bodies(3);
    bodies[0].kind = WaterBodyKind::Ocean;
    bodies[0].level = 0.0f;
    bodies[1].kind = WaterBodyKind::LakeBox;
    bodies[1].level = 12.0f;
    bodies[1].waveScale = 0.5f;
    bodies[1].boxMin = Vec2(0, 0);
    bodies[1].boxMax = Vec2(100, 100);
    bodies[2].kind = WaterBodyKind::River;
    bodies[2].level = 5.0f;
    bodies[2].points = { P(-200, 50, 5, 10), P(200, 50, 5, 10) };

    const WaterSettings safe = SanitizeWaterSettings(WaterSettings{});
    const WaterProbe inLake = WaterProbePoint(bodies, safe, 0.0, Vec2(50, 50));
    EXPECT_EQ(inLake.body, 1);
    EXPECT_FLOAT_EQ(inLake.level, 12.0f);
    EXPECT_FLOAT_EQ(inLake.waveScale, 0.5f);
    EXPECT_NEAR(inLake.surfaceY, 12.0f + inLake.waveHeight, 1e-5f);

    const WaterProbe inRiver = WaterProbePoint(bodies, safe, 0.0, Vec2(-150, 55));
    EXPECT_EQ(inRiver.body, 2);
    EXPECT_FLOAT_EQ(inRiver.level, 5.0f);

    const WaterProbe seaOnly = WaterProbePoint(bodies, safe, 0.0, Vec2(-150, 500));
    EXPECT_EQ(seaOnly.body, 0);

    const WaterProbe implicit = WaterProbePoint({}, safe, 0.0, Vec2(1, 1));
    EXPECT_TRUE(implicit.inWater);
    EXPECT_FLOAT_EQ(implicit.level, safe.seaLevel);

    std::vector<WaterBodyDesc> none(1);
    none[0].kind = WaterBodyKind::LakeBox;
    none[0].boxMin = Vec2(0, 0);
    none[0].boxMax = Vec2(1, 1);
    EXPECT_FALSE(WaterProbePoint(none, safe, 0.0, Vec2(5, 5)).inWater);
}

} // namespace
} // namespace gte
