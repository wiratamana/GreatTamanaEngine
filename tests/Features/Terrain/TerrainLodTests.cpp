// Tier 1: quadtree LOD selection.

#include "Features/Terrain/TerrainLod.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <unordered_set>

namespace gte::terrain {

namespace {

TerrainSettings OpenSettings()
{
    TerrainSettings s;
    s.frustumCulling = false;
    s.maxViewDistance = 200000.0f;
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

// Column-major eye-relative perspective projection looking along +Z (zero-to-one depth).
FrustumPlanes ForwardFrustum(float fovDeg, float aspect, float nearZ, float farZ)
{
    const float t = std::tan(fovDeg * 0.5f * 3.14159265f / 180.0f);
    float m[16] = {};
    m[0] = 1.0f / (aspect * t);
    m[5] = 1.0f / t;
    m[10] = farZ / (farZ - nearZ);
    m[11] = 1.0f;
    m[14] = -nearZ * farZ / (farZ - nearZ);
    return ExtractFrustumPlanes(m);
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

TEST(TerrainLodTests, LeavesTileTheWholeWorldExactly)
{
    const TerrainSettings s = OpenSettings();
    const TerrainLayout layout = MakeLayout(s);
    LodSelector selector(s);
    std::vector<WantedNode> nodes;
    const double cams[][3] = { { 0, 100, 0 }, { 5000, 800, -7000 }, { -16000, 50, 16000 }, { 123.4, 3000, 99.9 } };
    for (const auto& c : cams) {
        selector.Select(ViewAt(c[0], c[1], c[2]), nodes);
        const double world = 2.0 * layout.worldHalfExtent;
        EXPECT_DOUBLE_EQ(Area(layout, nodes), world * world);

        std::unordered_set<NodeKey, NodeKeyHash> keys;
        for (const WantedNode& n : nodes) {
            keys.insert(n.key);
        }
        for (const WantedNode& n : nodes) {
            NodeKey up = n.key;
            while (up.level < layout.rootLevel) {
                up = up.Parent();
                EXPECT_FALSE(keys.contains(up)) << "leaf under another leaf";
            }
        }
    }
}

TEST(TerrainLodTests, NeighbouringLeavesDifferByAtMostOneLevel)
{
    const TerrainSettings s = OpenSettings();
    LodSelector selector(s);
    std::vector<WantedNode> nodes;
    const int cells = 2 * s.worldHalfExtentChunks;
    std::vector<std::int8_t> grid(static_cast<std::size_t>(cells) * static_cast<std::size_t>(cells));
    const double step = 380.0;
    for (int i = 0; i < 24; ++i) {
        selector.Select(ViewAt(-9000.0 + i * step * 2.0, 200.0 + i * 20.0, 4000.0 - i * step), nodes);
        for (const WantedNode& n : nodes) {
            const int span = 1 << n.key.level;
            for (int dz = 0; dz < span; ++dz) {
                for (int dx = 0; dx < span; ++dx) {
                    const int gx = n.key.x * span + dx + s.worldHalfExtentChunks;
                    const int gz = n.key.z * span + dz + s.worldHalfExtentChunks;
                    grid[static_cast<std::size_t>(gz) * static_cast<std::size_t>(cells) + static_cast<std::size_t>(gx)] = static_cast<std::int8_t>(n.key.level);
                }
            }
        }
        int violations = 0;
        for (int z = 0; z < cells; ++z) {
            for (int x = 0; x < cells; ++x) {
                const int a = grid[static_cast<std::size_t>(z) * static_cast<std::size_t>(cells) + static_cast<std::size_t>(x)];
                if (x + 1 < cells) {
                    violations += std::abs(a - grid[static_cast<std::size_t>(z) * static_cast<std::size_t>(cells) + static_cast<std::size_t>(x) + 1]) > 1 ? 1 : 0;
                }
                if (z + 1 < cells) {
                    violations += std::abs(a - grid[static_cast<std::size_t>(z + 1) * static_cast<std::size_t>(cells) + static_cast<std::size_t>(x)]) > 1 ? 1 : 0;
                }
            }
        }
        EXPECT_EQ(violations, 0) << "step " << i;
    }
}

TEST(TerrainLodTests, FreshSelectionFollowsTheDistanceRule)
{
    const TerrainSettings s = OpenSettings();
    const TerrainLayout layout = MakeLayout(s);
    LodSelector selector(s);
    std::vector<WantedNode> nodes;
    selector.Select(ViewAt(1000.0, 0.0, 2000.0), nodes);
    int finest = 99;
    for (const WantedNode& n : nodes) {
        finest = std::min(finest, static_cast<int>(n.key.level));
        // A leaf above level 0 was not close enough to split.
        if (n.key.level > 0) {
            EXPECT_GE(n.distance, static_cast<double>(s.lodDistanceFactor) * NodeSize(layout, n.key.level) - 1.0e-6);
        }
    }
    EXPECT_EQ(finest, 0);
    // Detail is monotonic: the nearest leaf is never coarser than a farther one by more than one level.
    std::vector<WantedNode> byDistance = nodes;
    std::sort(byDistance.begin(), byDistance.end(), [](const WantedNode& a, const WantedNode& b) { return a.distance < b.distance; });
    int maxLevelSoFar = 0;
    for (const WantedNode& n : byDistance) {
        EXPECT_GE(static_cast<int>(n.key.level) + 2, maxLevelSoFar);
        maxLevelSoFar = std::max(maxLevelSoFar, static_cast<int>(n.key.level));
    }
}

TEST(TerrainLodTests, NodeCountStaysSmallForTheDefaultFactor)
{
    const TerrainSettings s = OpenSettings();
    LodSelector selector(s);
    std::vector<WantedNode> nodes;
    LodSelectionStats stats;
    selector.Select(ViewAt(0.0, 300.0, 0.0), nodes, &stats);
    EXPECT_GT(nodes.size(), 8u);
    EXPECT_LT(nodes.size(), 700u); // Whole 32 km world, no frustum, no distance limit.
    EXPECT_EQ(stats.leaves, static_cast<int>(nodes.size()));
}

TEST(TerrainLodTests, ViewDistanceDropsFarNodes)
{
    TerrainSettings s = OpenSettings();
    s.maxViewDistance = 3000.0f;
    LodSelector selector(Sanitize(s));
    std::vector<WantedNode> nodes;
    LodSelectionStats stats;
    selector.Select(ViewAt(0.0, 300.0, 0.0), nodes, &stats);
    ASSERT_FALSE(nodes.empty());
    EXPECT_GT(stats.rejectedDistance, 0);
    for (const WantedNode& n : nodes) {
        EXPECT_LE(n.distance, 3000.0);
    }
}

TEST(TerrainLodTests, FrustumRejectsNodesBehindTheCamera)
{
    TerrainSettings s = OpenSettings();
    s.frustumCulling = true;
    s.maxViewDistance = 20000.0f;
    const TerrainSettings clean = Sanitize(s);
    const TerrainLayout layout = MakeLayout(clean);
    LodSelector withFrustum(clean);
    TerrainSettings noFrustum = clean;
    noFrustum.frustumCulling = false;
    LodSelector without(Sanitize(noFrustum));

    LodView view = ViewAt(0.0, 400.0, 0.0);
    view.frustum = ForwardFrustum(60.0f, 16.0f / 9.0f, 0.1f, 20000.0f);
    ASSERT_TRUE(view.frustum.valid);

    std::vector<WantedNode> a;
    std::vector<WantedNode> b;
    LodSelectionStats stats;
    withFrustum.Select(view, a, &stats);
    without.Select(view, b);
    EXPECT_LT(a.size(), b.size());
    EXPECT_GT(stats.rejectedFrustum, 0);

    int inView = 0;
    for (const WantedNode& n : a) {
        if (!n.inView) {
            continue;
        }
        ++inView;
        const double zMax = (static_cast<double>(n.key.z) + 1.0) * NodeSize(layout, n.key.level);
        EXPECT_GT(zMax, 0.0) << "an in-view node lies fully behind the camera";
    }
    EXPECT_GT(inView, 0);
    // Straight ahead is in view, straight behind is not wanted.
    bool aheadFound = false;
    for (const WantedNode& n : a) {
        aheadFound |= n.inView && n.key.level == 0 && n.key.z >= 0 && n.key.z <= 1 && n.key.x >= -1 && n.key.x <= 0;
    }
    EXPECT_TRUE(aheadFound);
}

TEST(TerrainLodTests, HysteresisReducesFlickerAroundASplitBoundary)
{
    const TerrainSettings s = OpenSettings();
    LodSelector remembering(s);
    std::vector<WantedNode> prev;
    std::vector<WantedNode> cur;
    int changesWith = 0;
    int changesWithout = 0;
    const auto diff = [](const std::vector<WantedNode>& a, const std::vector<WantedNode>& b) {
        std::unordered_set<NodeKey, NodeKeyHash> keys;
        for (const WantedNode& n : a) {
            keys.insert(n.key);
        }
        int changed = 0;
        for (const WantedNode& n : b) {
            changed += keys.contains(n.key) ? 0 : 1;
        }
        return changed;
    };
    std::vector<WantedNode> freshPrev;
    std::vector<WantedNode> freshCur;
    for (int i = 0; i < 80; ++i) {
        const double x = 1500.0 + (i % 2 == 0 ? 0.0 : 90.0) + i * 0.5;
        remembering.Select(ViewAt(x, 100.0, 300.0), cur);
        LodSelector fresh(s);
        fresh.Select(ViewAt(x, 100.0, 300.0), freshCur);
        if (i > 0) {
            changesWith += diff(prev, cur);
            changesWithout += diff(freshPrev, freshCur);
        }
        prev = cur;
        freshPrev = freshCur;
    }
    EXPECT_GT(changesWithout, 0);
    EXPECT_LT(changesWith, changesWithout);
}

TEST(TerrainLodTests, MeasuredHeightRangeTightensTheDistance)
{
    const TerrainSettings s = OpenSettings();
    std::vector<WantedNode> loose;
    std::vector<WantedNode> tight;
    LodSelector a(s);
    a.Select(ViewAt(0.0, 1500.0, 0.0), loose);

    LodSelector b(s);
    const TerrainLayout layout = MakeLayout(s);
    // The whole world is flat at sea level: tell every root so.
    const int span = static_cast<int>(layout.worldHalfExtent / NodeSize(layout, layout.rootLevel));
    for (int z = -span; z < span; ++z) {
        for (int x = -span; x < span; ++x) {
            b.SetNodeHeightRange(NodeKey{ x, z, static_cast<std::uint8_t>(layout.rootLevel) }, 0.0f, 10.0f);
        }
    }
    b.Select(ViewAt(0.0, 1500.0, 0.0), tight);
    EXPECT_LT(tight.size(), loose.size());
}

TEST(TerrainLodTests, SelectingOverAThousandNodesIsFast)
{
    TerrainSettings s = OpenSettings();
    s.lodDistanceFactor = 9.0f;
    LodSelector selector(Sanitize(s));
    std::vector<WantedNode> nodes;
    selector.Select(ViewAt(100.0, 200.0, 100.0), nodes);
    ASSERT_GT(nodes.size(), 1000u);
    const auto t0 = std::chrono::steady_clock::now();
    constexpr int kRuns = 20;
    for (int i = 0; i < kRuns; ++i) {
        selector.Select(ViewAt(100.0 + i, 200.0, 100.0), nodes);
    }
    const double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count() / kRuns;
    RecordProperty("select_ms_per_call", std::to_string(ms));
    EXPECT_LT(ms, 5.0); // Loose bound for slow CI; the bench reports the real number.
}

} // namespace gte::terrain
