// Tier 1: lake and river carve profile.

#include "Features/Water/World/WaterCarve.h"

#include <gtest/gtest.h>

#include <chrono>
#include <cmath>
#include <cstring>
#include <vector>

namespace gte::water {

namespace {

WaterBody MakeLake(double cx, double cz, double size, float level, float depth)
{
    WaterBody b;
    b.name = "Lake";
    b.kind = WaterBodyKind::Lake;
    b.level = level;
    b.depth = depth;
    b.polygon = MakeBoxPolygon(cx, cz, size, size);
    return b;
}

WaterBody MakeRiver()
{
    WaterBody b;
    b.name = "River";
    b.kind = WaterBodyKind::River;
    b.depth = 5.0f;
    b.river = { { 0.0, 0.0, 40.0f, 14.0f }, { 150.0, 50.0, 38.0f, 14.0f }, { 300.0, 0.0, 34.0f, 14.0f }, { 450.0, -50.0, 30.0f, 14.0f } };
    return b;
}

std::shared_ptr<const WaterCarve> CarveOf(std::vector<WaterBody> bodies)
{
    return WaterCarve::Build(*WaterBodySet::Build(std::move(bodies), {}));
}

} // namespace

TEST(WaterCarveTests, NoCarvingBodiesGivesNullCarve)
{
    EXPECT_EQ(CarveOf({}), nullptr);
    WaterBody ocean;
    ocean.kind = WaterBodyKind::Ocean;
    EXPECT_EQ(CarveOf({ ocean }), nullptr);
    WaterBody off = MakeLake(0, 0, 100, 10.0f, 5.0f);
    off.carveTerrain = false;
    EXPECT_EQ(CarveOf({ off }), nullptr);
}

TEST(WaterCarveTests, LakeBedReachesDepthAndFarAwayIsUntouched)
{
    const auto carve = CarveOf({ MakeLake(0, 0, 200, 50.0f, 8.0f) });
    ASSERT_NE(carve, nullptr);
    EXPECT_NEAR(carve->Apply(0.0, 0.0, 100.0f, 2.0), 42.0f, 1e-3f);
    EXPECT_EQ(carve->Apply(5000.0, 5000.0, 100.0f, 2.0), 100.0f);
    EXPECT_EQ(carve->Apply(0.0, 0.0, 10.0f, 2.0), 10.0f); // Already deeper than the bed.
    EXPECT_FALSE(carve->Affects({ 4000, 4000, 4100, 4100 }, 2.0));
    EXPECT_TRUE(carve->Affects({ -10, -10, 10, 10 }, 2.0));
}

TEST(WaterCarveTests, WithoutSealTheTerrainOnlyGoesDown)
{
    WaterBody lake = MakeLake(0, 0, 200, 50.0f, 8.0f);
    lake.sealRim = false;
    const auto carve = CarveOf({ lake, MakeRiver() });
    ASSERT_NE(carve, nullptr);
    for (int i = 0; i < 4000; ++i) {
        const double x = std::fmod(i * 37.7, 700.0) - 200.0;
        const double z = std::fmod(i * 91.3, 400.0) - 200.0;
        const float h = static_cast<float>(20.0 + std::fmod(i * 13.3, 80.0));
        EXPECT_LE(carve->Apply(x, z, h, 2.0), h);
    }
}

TEST(WaterCarveTests, SealRimRaisesLowGroundAroundTheShore)
{
    const auto carve = CarveOf({ MakeLake(0, 0, 200, 50.0f, 8.0f) });
    // Land 5 m below the water level, 12 m outside the edge: the dam lifts it above the surface.
    const float h = carve->Apply(112.0, 0.0, 45.0f, 2.0);
    EXPECT_GT(h, 50.0f);
    // High ground is never lowered below its own height by the dam.
    EXPECT_GE(carve->Apply(112.0, 0.0, 90.0f, 2.0), carve->Apply(112.0, 0.0, 90.0f, 2.0));
    // The bed stays under the water everywhere inside the outline.
    for (double x = -95.0; x <= 95.0; x += 10.0) {
        EXPECT_LT(carve->Apply(x, 0.0, 49.0f, 2.0), 50.0f);
    }
}

TEST(WaterCarveTests, RiverChannelFollowsDescendingSurface)
{
    WaterBody river = MakeRiver();
    const auto set = WaterBodySet::Build({ river }, {});
    const auto carve = WaterCarve::Build(*set);
    ASSERT_NE(carve, nullptr);
    const auto segs = set->Segments(0);
    float previous = 1.0e9f;
    for (std::size_t i = 0; i < segs.size(); i += 3) {
        const double cx = 0.5 * (segs[i].ax + segs[i].bx);
        const double cz = 0.5 * (segs[i].az + segs[i].bz);
        const float bed = carve->Apply(cx, cz, 200.0f, 1.0);
        EXPECT_LE(bed, previous + 1e-3f);
        previous = bed;
        EXPECT_LT(bed, 40.0f - 4.0f);
    }
    EXPECT_EQ(carve->Apply(150.0, 400.0, 200.0f, 1.0), 200.0f);
}

TEST(WaterCarveTests, CoarseSpacingFadesNarrowRivers)
{
    const auto carve = CarveOf({ MakeRiver() });
    const float fine = carve->Apply(0.0, 0.0, 200.0f, 2.0);
    const float coarse = carve->Apply(0.0, 0.0, 200.0f, 128.0);
    EXPECT_LT(fine, 100.0f);
    EXPECT_EQ(coarse, 200.0f);
}

TEST(WaterCarveTests, TileMatchesPointEvaluationBitForBit)
{
    const auto carve = CarveOf({ MakeLake(300, 0, 160, 30.0f, 6.0f), MakeRiver() });
    const int side = 85;
    std::vector<float> tile(static_cast<std::size_t>(side) * side);
    std::vector<float> base(tile.size());
    for (std::size_t i = 0; i < tile.size(); ++i) {
        base[i] = static_cast<float>(25.0 + std::fmod(i * 7.1, 40.0));
        tile[i] = base[i];
    }
    const double ox = 128.0;
    const double oz = -64.0;
    carve->ApplyTile(tile.data(), side, ox, oz, 2.0);
    int changed = 0;
    for (int j = 0; j < side; ++j) {
        for (int i = 0; i < side; ++i) {
            const std::size_t k = static_cast<std::size_t>(j) * side + i;
            const float p = carve->Apply(ox + i * 2.0, oz + j * 2.0, base[k], 2.0);
            EXPECT_EQ(std::memcmp(&p, &tile[k], sizeof(float)), 0) << i << "," << j;
            changed += p != base[k] ? 1 : 0;
        }
    }
    EXPECT_GT(changed, 500);
}

TEST(WaterCarveTests, TileCostPerNodeIsSmall)
{
    const auto carve = CarveOf({ MakeRiver(), MakeLake(300, 0, 160, 30.0f, 6.0f) });
    const int side = 85;
    std::vector<float> tile(static_cast<std::size_t>(side) * side, 35.0f);
    const auto t0 = std::chrono::steady_clock::now();
    const int reps = 50;
    for (int r = 0; r < reps; ++r) {
        std::fill(tile.begin(), tile.end(), 35.0f);
        carve->ApplyTile(tile.data(), side, 100.0, -50.0, 2.0);
    }
    const double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count() / reps;
    std::printf("[water] carve tile 85x85 (river + lake): %.3f ms\n", ms);
    EXPECT_LT(ms, 5.0);
}

TEST(WaterCarveTests, HashFollowsTheBodies)
{
    const auto a = CarveOf({ MakeLake(0, 0, 100, 10.0f, 5.0f) });
    const auto same = CarveOf({ MakeLake(0, 0, 100, 10.0f, 5.0f) });
    const auto deeper = CarveOf({ MakeLake(0, 0, 100, 10.0f, 6.0f) });
    EXPECT_EQ(a->Hash(), same->Hash());
    EXPECT_NE(a->Hash(), deeper->Hash());
}

} // namespace gte::water
