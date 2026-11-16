// Tier 1: the LOD selector keeps the number of leaves under a budget by lowering the detail.

#include "Terrain/TerrainLod.h"

#include <gtest/gtest.h>

#include <cmath>
#include <unordered_set>

namespace gte::terrain {

namespace {

TerrainSettings OpenWorld()
{
    TerrainSettings s;
    s.frustumCulling = false;
    s.maxViewDistance = 200000.0f;
    s.lodDistanceFactor = 2.5f;
    return Sanitize(s);
}

LodView ViewAt(double x, double y, double z)
{
    LodView v;
    v.cameraX = x;
    v.cameraY = y;
    v.cameraZ = z;
    return v;
}

double Area(const TerrainLayout& l, const std::vector<WantedNode>& nodes)
{
    double sum = 0.0;
    for (const WantedNode& n : nodes) {
        const double s = NodeSize(l, n.key.level);
        sum += s * s;
    }
    return sum;
}

} // namespace

TEST(TerrainLodBudgetTests, NoBudgetMeansOnePassAtFullDetail)
{
    LodSelector selector(OpenWorld());
    std::vector<WantedNode> nodes;
    LodSelectionStats stats;
    selector.Select(ViewAt(0, 300, 0), nodes, &stats);
    EXPECT_EQ(stats.passes, 1);
    EXPECT_DOUBLE_EQ(stats.distanceScale, 1.0);
    EXPECT_FALSE(stats.overBudget);
}

TEST(TerrainLodBudgetTests, BudgetLowersTheLeafCountAndStillTilesTheWorld)
{
    const TerrainSettings s = OpenWorld();
    const TerrainLayout layout = MakeLayout(s);
    LodSelector free(s);
    std::vector<WantedNode> unlimited;
    LodSelectionStats base;
    free.Select(ViewAt(0, 300, 0), unlimited, &base);

    TerrainSettings floorSettings = s;
    floorSettings.lodDistanceFactor = 1.5f;
    LodSelector floorSelector(floorSettings);
    LodSelectionStats floor;
    floorSelector.Select(ViewAt(0, 300, 0), unlimited, &floor);
    ASSERT_LT(floor.leaves, base.leaves) << "the test needs room between full detail and the floor";

    const int cap = (base.leaves + floor.leaves) / 2;
    LodSelector capped(s);
    capped.SetBudget(LodBudget{ 0, cap });
    std::vector<WantedNode> nodes;
    LodSelectionStats stats;
    capped.Select(ViewAt(0, 300, 0), nodes, &stats);
    EXPECT_GT(stats.passes, 1);
    EXPECT_LT(stats.distanceScale, 1.0);
    EXPECT_LT(stats.leaves, base.leaves);
    EXPECT_LE(stats.leaves, cap);
    EXPECT_FALSE(stats.overBudget);
    const double world = 2.0 * layout.worldHalfExtent;
    EXPECT_DOUBLE_EQ(Area(layout, nodes), world * world) << "a capped selection must still tile the world";
}

TEST(TerrainLodBudgetTests, InViewCapUsesOnlyTheNodesInTheFrustum)
{
    TerrainSettings s = OpenWorld();
    s.frustumCulling = true;
    s.maxViewDistance = 20000.0f;
    s = Sanitize(s);

    // Eye-relative projection looking along +Z, 60 degrees, 16:9.
    const float t = std::tan(30.0f * 3.14159265f / 180.0f);
    float m[16] = {};
    m[0] = 1.0f / (16.0f / 9.0f * t);
    m[5] = 1.0f / t;
    m[10] = 20000.0f / (20000.0f - 3.0f);
    m[11] = 1.0f;
    m[14] = -3.0f * 20000.0f / (20000.0f - 3.0f);
    LodView view = ViewAt(0, 400, -8000);
    view.frustum = ExtractFrustumPlanes(m);
    ASSERT_TRUE(view.frustum.valid);

    LodSelector open(s);
    std::vector<WantedNode> nodes;
    LodSelectionStats base;
    open.Select(view, nodes, &base);

    TerrainSettings floorSettings = s;
    floorSettings.lodDistanceFactor = 1.5f;
    LodSelector floorSelector(floorSettings);
    LodSelectionStats floor;
    floorSelector.Select(view, nodes, &floor);
    ASSERT_LT(floor.inView, base.inView);

    const int cap = (base.inView + floor.inView) / 2;
    LodSelector capped(s);
    capped.SetBudget(LodBudget{ cap, 0 });
    LodSelectionStats stats;
    capped.Select(view, nodes, &stats);
    EXPECT_LE(stats.inView, cap);
    EXPECT_LT(stats.inView, base.inView);
}

TEST(TerrainLodBudgetTests, DetailNeverDropsBelowTheBalanceLimitAndOverBudgetIsReported)
{
    const TerrainSettings s = OpenWorld();
    LodSelector selector(s);
    selector.SetBudget(LodBudget{ 0, 1 }); // Impossible on purpose.
    std::vector<WantedNode> nodes;
    LodSelectionStats stats;
    selector.Select(ViewAt(0, 100, 0), nodes, &stats);
    EXPECT_TRUE(stats.overBudget);
    EXPECT_GE(stats.distanceScale * static_cast<double>(s.lodDistanceFactor), 1.5 - 1e-9);
    EXPECT_FALSE(nodes.empty());
}

TEST(TerrainLodBudgetTests, DetailComesBackOnceTheViewIsWellUnderTheCap)
{
    const TerrainSettings s = OpenWorld();
    LodSelector selector(s);
    selector.SetBudget(LodBudget{ 0, 150 });
    std::vector<WantedNode> nodes;
    LodSelectionStats stats;
    selector.Select(ViewAt(0, 300, 0), nodes, &stats);
    const double lowered = selector.DistanceScale();
    ASSERT_LT(lowered, 1.0);

    // Far from everything fine: the same cap is no problem, so the scale climbs back frame by frame.
    for (int i = 0; i < 400; ++i) {
        selector.Select(ViewAt(0, 60000, 0), nodes, &stats);
    }
    EXPECT_GT(selector.DistanceScale(), lowered);
}

TEST(TerrainLodBudgetTests, LeavesDoNotOverlapWhileTheScaleChanges)
{
    const TerrainSettings s = OpenWorld();
    const TerrainLayout layout = MakeLayout(s);
    LodSelector selector(s);
    selector.SetBudget(LodBudget{ 0, 120 });
    std::vector<WantedNode> nodes;
    for (int i = 0; i < 30; ++i) {
        selector.Select(ViewAt(-4000.0 + i * 280.0, 200.0 + i * 15.0, 1500.0 - i * 190.0), nodes);
        std::unordered_set<NodeKey, NodeKeyHash> keys;
        for (const WantedNode& n : nodes) {
            keys.insert(n.key);
        }
        for (const WantedNode& n : nodes) {
            NodeKey up = n.key;
            while (up.level < layout.rootLevel) {
                up = up.Parent();
                ASSERT_FALSE(keys.contains(up));
            }
        }
    }
}

} // namespace gte::terrain
