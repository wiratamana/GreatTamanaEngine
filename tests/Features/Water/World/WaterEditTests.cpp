// Tier 1: point edits, ground helpers and the debug map picture.

#include "Features/Water/World/WaterEdit.h"
#include "Features/Water/World/WaterMapImage.h"

#include <gtest/gtest.h>

#include <cmath>

namespace gte::water {

TEST(WaterEditTests, LakeCornersMoveInsertAndRemoveWithinLimits)
{
    WaterBody lake = MakeBoxLake("L", 0, 0, 100, 100, 5.0f);
    std::string error;
    EXPECT_TRUE(EditMovePoint(lake, 2, 80.0, 90.0, std::nullopt, std::nullopt, error));
    EXPECT_EQ(lake.polygon[2].x, 80.0);
    EXPECT_FALSE(EditMovePoint(lake, 9, 0, 0, std::nullopt, std::nullopt, error));
    EXPECT_FALSE(EditMovePoint(lake, 0, std::nan(""), 0, std::nullopt, std::nullopt, error));

    EXPECT_TRUE(EditInsertPoint(lake, 1, 0.0, -80.0, error));
    EXPECT_EQ(lake.polygon.size(), 5u);
    EXPECT_EQ(lake.polygon[1].z, -80.0);
    EXPECT_TRUE(EditRemovePoint(lake, 1, error));
    EXPECT_TRUE(EditRemovePoint(lake, 0, error));
    EXPECT_FALSE(EditRemovePoint(lake, 0, error)) << "three corners must stay";
    while (static_cast<int>(lake.polygon.size()) < kMaxLakeVertices) {
        ASSERT_TRUE(EditInsertPoint(lake, -1, 1.0, 1.0, error));
    }
    EXPECT_FALSE(EditInsertPoint(lake, -1, 1.0, 1.0, error));
}

TEST(WaterEditTests, RiverPointsKeepHeightAndWidthFromNeighbours)
{
    WaterBody river = MakeRiverBody("R", { { 0, 0, 20.0f, 10.0f }, { 100, 0, 10.0f, 20.0f } });
    std::string error;
    EXPECT_TRUE(EditInsertPoint(river, 1, 50.0, 10.0, error));
    ASSERT_EQ(river.river.size(), 3u);
    EXPECT_FLOAT_EQ(river.river[1].surfaceY, 15.0f);
    EXPECT_FLOAT_EQ(river.river[1].width, 15.0f);
    EXPECT_TRUE(EditInsertPoint(river, -1, 150.0, 0.0, error));
    EXPECT_FLOAT_EQ(river.river[3].surfaceY, 10.0f);
    EXPECT_TRUE(EditMovePoint(river, 0, 5.0, 6.0, 30.0f, 40.0f, error));
    EXPECT_EQ(river.river[0].z, 6.0);
    EXPECT_FLOAT_EQ(river.river[0].surfaceY, 30.0f);
    EXPECT_FLOAT_EQ(river.river[0].width, 40.0f);
    EXPECT_TRUE(EditRemovePoint(river, 3, error));
    EXPECT_TRUE(EditRemovePoint(river, 2, error));
    EXPECT_FALSE(EditRemovePoint(river, 0, error)) << "two points must stay";
    WaterBody ocean = MakeOceanBody(0.0f);
    EXPECT_FALSE(EditInsertPoint(ocean, 0, 0, 0, error));
}

TEST(WaterEditTests, LeakCheckAndSuggestedLevelOnABowl)
{
    // Ground rises from 0 at the centre to 100 at 100 m, but a gap of 10 m on the east side.
    const HeightFn bowl = [](double x, double z) {
        const float h = static_cast<float>(std::min(100.0, std::hypot(x, z)));
        return (x > 90.0 && std::abs(z) < 20.0) ? 10.0f : h;
    };
    WaterBody lake = MakeBoxLake("L", 0, 0, 200, 200, 50.0f);
    const LakeLeakReport leaks = CheckLakeLeaks(lake, bowl);
    EXPECT_GT(leaks.samples, 80);
    EXPECT_GT(leaks.below, 0);
    EXPECT_LT(leaks.lowest, 20.0f);
    const float level = SuggestLakeLevel(lake, bowl, 0.0f);
    EXPECT_NEAR(level, leaks.lowest, 1e-3f);
    lake.level = level - 1.0f;
    EXPECT_EQ(CheckLakeLeaks(lake, bowl).below, 0);
}

TEST(WaterEditTests, AutoRiverLevelsNeverRise)
{
    WaterBody river = MakeRiverBody("R", { { 0, 0, 0, 10 }, { 100, 0, 0, 10 }, { 200, 0, 0, 10 }, { 300, 0, 0, 10 } });
    AutoRiverLevels(river, [](double x, double) { return static_cast<float>(100.0 + (x == 200.0 ? 40.0 : -x * 0.1)); }, 1.0f);
    for (std::size_t i = 1; i < river.river.size(); ++i) {
        EXPECT_LE(river.river[i].surfaceY, river.river[i - 1].surfaceY);
    }
    EXPECT_FLOAT_EQ(river.river[0].surfaceY, 99.0f);
}

TEST(WaterMapImageTests, DrawsOceanLakeRiverAndShoreBand)
{
    WaterBody ocean = MakeOceanBody(0.0f);
    WaterBody lake = MakeBoxLake("L", 600, 0, 200, 200, 80.0f);
    WaterBody river = MakeRiverBody("R", { { -800, -500, 60.0f, 30.0f }, { -400, -300, 55.0f, 30.0f }, { 0, -400, 50.0f, 30.0f } });
    const auto set = WaterBodySet::Build({ ocean, lake, river }, {});
    // Ground: sea for x < -200, a beach ramp of 0.05 up to 80 m, flat 90 m after.
    const HeightFn ground = [](double x, double) { return x < -200.0 ? -20.0f : static_cast<float>(std::min(90.0, (x + 200.0) * 0.05)); };

    WaterMapView view;
    view.size = 256;
    view.halfRange = 1000.0;
    const WaterMapImage img = RenderWaterMap(*set, ground, view, {});
    ASSERT_EQ(img.rgba.size(), 256u * 256u * 4u);
    EXPECT_GT(img.waterPixels, 256 * 256 / 8);
    EXPECT_GT(img.landPixels, 256 * 256 / 4);
    EXPECT_GT(img.shorePixels, 50);

    double px = 0, py = 0;
    ASSERT_TRUE(WorldToMapPixel(view, 600.0, 0.0, px, py));
    // Lake centre pixel is bluish water, a dry land pixel is not.
    const auto at = [&](int x, int y) { return &img.rgba[(static_cast<std::size_t>(y) * 256 + x) * 4]; };
    const std::uint8_t* lakePx = at(static_cast<int>(px), static_cast<int>(py));
    EXPECT_GT(lakePx[2], lakePx[0]);
    ASSERT_TRUE(WorldToMapPixel(view, 300.0, 700.0, px, py));
    const std::uint8_t* landPx = at(static_cast<int>(px), static_cast<int>(py));
    EXPECT_GT(landPx[1], landPx[2]);
    EXPECT_EQ(landPx[3], 255);
    EXPECT_FALSE(WorldToMapPixel(view, 5000.0, 0.0, px, py));
}

} // namespace gte::water
