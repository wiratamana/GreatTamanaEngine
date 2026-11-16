#include "Editor/Terrain/TerrainOverlayData.h"
#include "Editor/Terrain/TerrainUiBridge.h"

#include "FakeTerrainWorld.h"

#include <gtest/gtest.h>

#include <set>

namespace gte {

TEST(TerrainUiBridgeTest, AnEmptyRequestChangesNothing)
{
    FakeTerrainWorld world;
    TerrainUiBridge bridge;
    EXPECT_TRUE(bridge.request.Empty());
    ApplyTerrainUiRequest(world, bridge);
    EXPECT_EQ(world.settingsCalls, 0);
    EXPECT_TRUE(world.order.empty());
}

TEST(TerrainUiBridgeTest, SettingsGoInBeforeOpenAndTheRequestIsConsumed)
{
    FakeTerrainWorld world;
    TerrainUiBridge bridge;
    bridge.request.settings = terrain::Sanitize(terrain::TerrainSettings{});
    bridge.request.settings->seed = 99;
    bridge.request.open = terrain::TerrainOpenOptions{};
    ApplyTerrainUiRequest(world, bridge);
    EXPECT_EQ(world.settingsCalls, 1);
    EXPECT_EQ(world.settings.seed, 99u);
    EXPECT_TRUE(world.open);
    EXPECT_TRUE(bridge.request.Empty());
    EXPECT_EQ(bridge.snapshot.message, "opened");
    EXPECT_FALSE(bridge.snapshot.messageIsError);
}

TEST(TerrainUiBridgeTest, CloseRunsBeforeANewOpen)
{
    FakeTerrainWorld world;
    world.open = true;
    TerrainUiBridge bridge;
    bridge.request.close = true;
    bridge.request.open = terrain::TerrainOpenOptions{};
    ApplyTerrainUiRequest(world, bridge);
    EXPECT_EQ(world.order, "close;open;");
}

TEST(TerrainUiBridgeTest, FlagsModesAndFlightAreForwarded)
{
    FakeTerrainWorld world;
    TerrainUiBridge bridge;
    bridge.request.livePreview = true;
    bridge.request.debugAabb = true;
    bridge.request.cameraMode = terrain::CameraMode::FreeFly;
    terrain::FlightSpec spec;
    spec.speed = 77.0;
    bridge.request.flight = spec;
    ApplyTerrainUiRequest(world, bridge);
    EXPECT_TRUE(world.preview);
    EXPECT_TRUE(world.aabb);
    EXPECT_EQ(world.cameraMode, terrain::CameraMode::FreeFly);
    EXPECT_TRUE(world.flying);
    EXPECT_DOUBLE_EQ(world.flight.speed, 77.0);
}

TEST(TerrainUiBridgeTest, CameraInputIsFedOnceAndSummed)
{
    FakeTerrainWorld world;
    TerrainUiBridge bridge;
    terrain::CameraInput a;
    a.lookDeltaX = 2.0f;
    terrain::CameraInput b;
    b.lookDeltaX = 5.0f;
    terrain::AccumulateInput(bridge.request.cameraInput, a);
    terrain::AccumulateInput(bridge.request.cameraInput, b);
    bridge.request.hasCameraInput = true;
    ApplyTerrainUiRequest(world, bridge);
    EXPECT_EQ(world.feedCalls, 1);
    EXPECT_FLOAT_EQ(world.fed.lookDeltaX, 7.0f);
    ApplyTerrainUiRequest(world, bridge);
    EXPECT_EQ(world.feedCalls, 1);
}

TEST(TerrainUiBridgeTest, SaveShowsBusyFirstAndRunsOnTheNextFrame)
{
    FakeTerrainWorld world;
    TerrainUiBridge bridge;
    bridge.request.savePath = std::string("C:/x/world.gta");
    ApplyTerrainUiRequest(world, bridge);
    EXPECT_TRUE(world.savedPath.empty());
    EXPECT_FALSE(bridge.snapshot.busy.empty());
    ApplyTerrainUiRequest(world, bridge);
    EXPECT_EQ(world.savedPath, "C:/x/world.gta");
    EXPECT_TRUE(bridge.snapshot.busy.empty());
}

TEST(TerrainUiBridgeTest, FailedLoadReportsAnError)
{
    FakeTerrainWorld world;
    TerrainUiBridge bridge;
    bridge.request.loadPath = std::string("C:/missing.gta");
    ApplyTerrainUiRequest(world, bridge);
    EXPECT_FALSE(bridge.snapshot.busy.empty());
    ApplyTerrainUiRequest(world, bridge);
    EXPECT_TRUE(bridge.snapshot.busy.empty());
    EXPECT_TRUE(bridge.snapshot.messageIsError);
    EXPECT_EQ(bridge.snapshot.message, "no such file");
}

TEST(TerrainUiBridgeTest, PublishCopiesTheWorldAndNodesOnlyWhileTheBoxFlagIsOn)
{
    FakeTerrainWorld world;
    world.open = true;
    world.stats.nodesResident = 42;
    world.nodes.push_back({});
    TerrainUiBridge bridge;
    const Mat4 vp = Mat4::Identity();
    PublishTerrainSnapshot(world, bridge, &vp);
    EXPECT_TRUE(bridge.snapshot.valid);
    EXPECT_TRUE(bridge.snapshot.open);
    EXPECT_EQ(bridge.snapshot.stats.nodesResident, 42);
    EXPECT_TRUE(bridge.snapshot.hasViewProjection);
    EXPECT_TRUE(bridge.snapshot.nodes.empty());
    world.aabb = true;
    PublishTerrainSnapshot(world, bridge, nullptr);
    EXPECT_EQ(bridge.snapshot.nodes.size(), 1u);
    EXPECT_FALSE(bridge.snapshot.hasViewProjection);
    EXPECT_TRUE(bridge.snapshot.debugAabb);
}

// --- Overlay data -------------------------------------------------------------------------------------------

TEST(TerrainOverlayDataTest, BoxHasTwelveDistinctEdgesOfTheRightLength)
{
    terrain::TerrainNodeInfo node;
    node.minX = 0.0;
    node.maxX = 10.0;
    node.minY = 0.0;
    node.maxY = 20.0;
    node.minZ = 0.0;
    node.maxZ = 30.0;
    std::vector<FrustumSegment> edges;
    AppendNodeBoxSegments(node, edges);
    ASSERT_EQ(edges.size(), 12u);
    std::multiset<int> lengths;
    for (const FrustumSegment& e : edges) {
        lengths.insert(static_cast<int>(Length(e.a - e.b) + 0.5f));
    }
    EXPECT_EQ(lengths.count(10), 4u);
    EXPECT_EQ(lengths.count(20), 4u);
    EXPECT_EQ(lengths.count(30), 4u);
}

TEST(TerrainOverlayDataTest, BucketsFollowStateOrLevel)
{
    terrain::TerrainNodeInfo node;
    node.key.level = 4;
    node.enabled = true;
    node.inView = true;
    EXPECT_EQ(OverlayBucketOf(node, true), 0);
    node.inView = false;
    EXPECT_EQ(OverlayBucketOf(node, true), 1);
    node.enabled = false;
    EXPECT_EQ(OverlayBucketOf(node, true), 2);
    EXPECT_EQ(OverlayBucketOf(node, false), kOverlayStateBuckets + 4);
    node.key.level = 200;
    EXPECT_EQ(OverlayBucketOf(node, false), kOverlayBucketCount - 1);
}

TEST(TerrainOverlayDataTest, EveryBucketHasAnOpaqueColour)
{
    std::set<std::array<std::uint8_t, 4>> colours;
    for (int bucket = 0; bucket < kOverlayBucketCount; ++bucket) {
        const auto colour = OverlayBucketColor(bucket);
        EXPECT_EQ(colour[3], 255);
        colours.insert(colour);
    }
    EXPECT_EQ(colours.size(), static_cast<std::size_t>(kOverlayBucketCount));
}

} // namespace gte
