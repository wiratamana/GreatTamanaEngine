// Drives the Big World state machine with fake ports: success path, failure at each phase, scene replaced,
// close while opening, settings restore and the cross-system checks.

#include "Game/BigWorld/BigWorldDemo.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <cmath>
#include <map>
#include <set>

namespace gte::bigworld {
namespace {

class FakeTerrain final : public ITerrainPort {
public:
    PortResult Open(const TerrainOpenRequest& request) override
    {
        ++openCalls;
        lastProfile = request.profile;
        if (failOpen) {
            return { false, "no pool memory", 0 };
        }
        facts.available = true;
        facts.open = true;
        return {};
    }
    PortResult PlaceCamera(const CameraPlacement& placement) override
    {
        ++cameraCalls;
        lastCamera = placement;
        return failCamera ? PortResult{ false, "camera refused", 0 } : PortResult{};
    }
    PortResult Close() override
    {
        ++closeCalls;
        facts.open = false;
        return {};
    }
    TerrainFacts Facts() const override { return facts; }

    TerrainFacts facts;
    CameraPlacement lastCamera;
    std::string lastProfile;
    bool failOpen = false;
    bool failCamera = false;
    int openCalls = 0;
    int cameraCalls = 0;
    int closeCalls = 0;
};

class FakeWater final : public IWaterPort {
public:
    PortResult AddLake(const LakeRequest& request) override
    {
        lakes.push_back(request);
        if (failLake) {
            return { false, "lake refused", 0 };
        }
        return AddBody();
    }
    PortResult AddRiver(const RiverRequest& request) override
    {
        rivers.push_back(request);
        if (failRiver) {
            return { false, "river refused", 0 };
        }
        return AddBody();
    }
    PortResult RemoveBody(std::uint32_t id) override
    {
        removed.push_back(id);
        facts.bodyCount = std::max(0, facts.bodyCount - 1);
        return {};
    }
    WaterFacts Facts() const override { return facts; }

    // The real world applies the carve a little after the edit; the test calls this when it should happen.
    void SettleCarve()
    {
        facts.carvePending = false;
        ++facts.carveApplies;
    }

    WaterFacts facts;
    std::vector<LakeRequest> lakes;
    std::vector<RiverRequest> rivers;
    std::vector<std::uint32_t> removed;
    bool failLake = false;
    bool failRiver = false;

private:
    PortResult AddBody()
    {
        PortResult r;
        r.id = nextId++;
        ++facts.bodyCount;
        facts.carvePending = true;
        return r;
    }
    std::uint32_t nextId = 10;
};

class FakeFeatures final : public IFeaturePort {
public:
    bool Has(std::string_view feature) const override { return values.contains(std::string(feature)); }
    PortResult Snapshot(std::string_view feature, std::span<const std::string> keys) override
    {
        ++snapshotCalls;
        for (const std::string& key : keys) {
            saved.push_back({ std::string(feature), key, values[std::string(feature)][key] });
        }
        return failSnapshot ? PortResult{ false, "snapshot refused", 0 } : PortResult{};
    }
    PortResult Set(std::string_view feature, std::span<const KeyValue> kvs) override
    {
        if (failSet) {
            return { false, "set refused", 0 };
        }
        for (const KeyValue& kv : kvs) {
            values[std::string(feature)][kv.key] = kv.value;
            setLog.push_back(std::string(feature) + "." + kv.key + "=" + kv.value);
        }
        return {};
    }
    PortResult RestoreAll() override
    {
        ++restoreCalls;
        for (const Saved& s : saved) {
            values[s.feature][s.key] = s.value;
        }
        saved.clear();
        return {};
    }
    std::vector<std::optional<std::string>> Read(std::string_view feature, std::span<const std::string_view> keys) const override
    {
        std::vector<std::optional<std::string>> out;
        const auto f = values.find(std::string(feature));
        if (f == values.end()) {
            return out;
        }
        for (const std::string_view key : keys) {
            const auto it = f->second.find(std::string(key));
            out.push_back(it == f->second.end() ? std::nullopt : std::optional<std::string>(it->second));
        }
        return out;
    }

    struct Saved {
        std::string feature;
        std::string key;
        std::string value;
    };
    std::map<std::string, std::map<std::string, std::string>> values;
    std::vector<Saved> saved;
    std::vector<std::string> setLog;
    bool failSnapshot = false;
    bool failSet = false;
    int snapshotCalls = 0;
    int restoreCalls = 0;
};

class BigWorldDemoTest : public ::testing::Test {
protected:
    BigWorldDemoTest() : demo({ &terrain, &water, &features })
    {
        features.values["VolumetricCloud"]["enabled"] = "false";
        features.values["Grass"]["enabled"] = "true";
        features.values["Grass"]["densityLod0"] = "16";
        features.values["Grass"]["game.status"] = "NoTile";
        features.values["Grass"]["tile.present"] = "false";
        features.values["Grass"]["tile.seaLevel"] = "-1000000";
        water.facts.available = true;
        water.facts.featureConnected = true;
        water.facts.hasOcean = true;
        water.facts.oceanLevel = 0.0;
        terrain.facts.available = true;
    }

    // Moves the clock forward in small steps; `hook` runs before each tick to play the world's part.
    template <typename Hook>
    void Pump(double seconds, Hook hook)
    {
        const double end = now + seconds;
        while (now < end) {
            now += 0.1;
            hook();
            demo.Tick(now);
        }
    }

    void Pump(double seconds)
    {
        Pump(seconds, [] {});
    }

    // A world that behaves: the terrain streams in, the carve lands, grass starts to draw.
    void HealthyWorld()
    {
        terrain.facts.nodesResident = 40;
        terrain.facts.nodesPending = 0;
        if (water.facts.carvePending) {
            water.SettleCarve();
        }
        if (features.values["Grass"].contains("preset")) {
            features.values["Grass"]["game.status"] = "Ok";
            features.values["Grass"]["tile.present"] = "true";
            features.values["Grass"]["tile.seaLevel"] = "0";
        }
    }

    void OpenAndRunToReady(Quality quality = Quality::Big)
    {
        ASSERT_TRUE(demo.Open(MakeBigWorldPlan(quality), now).ok);
        Pump(5.0, [&] { HealthyWorld(); });
        ASSERT_TRUE(demo.Ready()) << PhaseName(demo.State().phase) << " " << demo.State().status;
    }

    FakeTerrain terrain;
    FakeWater water;
    FakeFeatures features;
    BigWorldDemo demo;
    double now = 100.0;
};

TEST_F(BigWorldDemoTest, StartsIdle)
{
    EXPECT_EQ(demo.State().phase, Phase::Idle);
    EXPECT_FALSE(demo.Active());
    EXPECT_FALSE(demo.Ready());
}

TEST_F(BigWorldDemoTest, OpenOpensTheTerrainAndPlacesTheCameraAtOnce)
{
    BigWorldPlan plan = MakeBigWorldPlan(Quality::Big);
    plan.spotX = -1234.0;
    plan.spotZ = 55.0;
    plan.heightAboveGround = 9.0;
    ASSERT_TRUE(demo.Open(plan, now).ok);
    EXPECT_EQ(demo.State().phase, Phase::WaitingForNodes);
    EXPECT_EQ(terrain.openCalls, 1);
    EXPECT_EQ(terrain.lastProfile, "lean");
    EXPECT_EQ(terrain.cameraCalls, 1);
    EXPECT_DOUBLE_EQ(terrain.lastCamera.x, -1234.0);
    EXPECT_DOUBLE_EQ(terrain.lastCamera.z, 55.0);
    EXPECT_DOUBLE_EQ(terrain.lastCamera.heightAboveGround, 9.0);
    EXPECT_TRUE(demo.State().terrainOwned);
}

TEST_F(BigWorldDemoTest, WaitsWhileTerrainIsStillStreaming)
{
    ASSERT_TRUE(demo.Open(MakeBigWorldPlan(Quality::Big), now).ok);
    terrain.facts.nodesResident = 40;
    terrain.facts.nodesPending = 5;
    Pump(3.0);
    EXPECT_EQ(demo.State().phase, Phase::WaitingForNodes);
    EXPECT_TRUE(water.lakes.empty());
}

TEST_F(BigWorldDemoTest, WaitsForEnoughResidentNodes)
{
    ASSERT_TRUE(demo.Open(MakeBigWorldPlan(Quality::Big), now).ok);
    terrain.facts.nodesResident = 3;
    Pump(3.0);
    EXPECT_EQ(demo.State().phase, Phase::WaitingForNodes);
}

TEST_F(BigWorldDemoTest, SuccessPathReachesReadyAndSwitchesFeaturesOn)
{
    OpenAndRunToReady();
    EXPECT_EQ(demo.State().status, "ready");
    EXPECT_EQ(features.values["VolumetricCloud"]["enabled"], "true");
    EXPECT_EQ(features.values["Grass"]["preset"], "Big");
    ASSERT_EQ(water.lakes.size(), 1u);
    EXPECT_EQ(water.rivers.size(), 1u);
    EXPECT_EQ(demo.State().waterBodyIds.size(), 2u);
    EXPECT_TRUE(demo.State().warnings.empty());
}

TEST_F(BigWorldDemoTest, TheLakeIsAheadOfTheCameraAndTheRiverCrossesTheView)
{
    BigWorldPlan plan = MakeBigWorldPlan(Quality::Big);
    plan.spotX = 0.0;
    plan.spotZ = 0.0;
    plan.headingDegrees = 90.0;
    plan.lakeDistance = 110.0;
    plan.riverDistance = 160.0;
    plan.riverLength = 400.0;
    ASSERT_TRUE(demo.Open(plan, now).ok);
    Pump(5.0, [&] { HealthyWorld(); });
    ASSERT_EQ(water.lakes.size(), 1u);
    EXPECT_NEAR(water.lakes[0].centerX, 110.0, 1e-6);
    ASSERT_EQ(water.rivers.size(), 1u);
    ASSERT_EQ(water.rivers[0].points.size(), 5u);
    for (const PlanPoint& p : water.rivers[0].points) {
        EXPECT_NEAR(p.x, 160.0, 1e-6);
    }
    EXPECT_NEAR(std::abs(water.rivers[0].points.front().z - water.rivers[0].points.back().z), 400.0, 1e-6);
}

TEST_F(BigWorldDemoTest, LeanTierAddsNoRiver)
{
    OpenAndRunToReady(Quality::Lean);
    EXPECT_TRUE(water.rivers.empty());
    EXPECT_EQ(features.values["Grass"]["preset"], "Lean");
}

TEST_F(BigWorldDemoTest, ARefusedRiverIsOnlyAWarning)
{
    water.failRiver = true;
    OpenAndRunToReady();
    ASSERT_EQ(demo.State().warnings.size(), 1u);
    EXPECT_NE(demo.State().warnings[0].find("river"), std::string::npos);
}

TEST_F(BigWorldDemoTest, WaterIsAddedOnlyAfterTheTerrainIsResident)
{
    ASSERT_TRUE(demo.Open(MakeBigWorldPlan(Quality::Big), now).ok);
    Pump(2.0);
    EXPECT_TRUE(water.lakes.empty());
    terrain.facts.nodesResident = 40;
    Pump(0.5);
    EXPECT_EQ(water.lakes.size(), 1u);
}

TEST_F(BigWorldDemoTest, FeaturesStayUntouchedUntilTheWaterCarveSettled)
{
    ASSERT_TRUE(demo.Open(MakeBigWorldPlan(Quality::Big), now).ok);
    terrain.facts.nodesResident = 40;
    Pump(3.0); // the carve never settles
    EXPECT_EQ(demo.State().phase, Phase::WaitingForWater);
    EXPECT_EQ(features.values["VolumetricCloud"]["enabled"], "false");
    EXPECT_EQ(features.snapshotCalls, 0);
}

TEST_F(BigWorldDemoTest, ACarveThatNeverSettlesTimesOutWithAWarning)
{
    BigWorldPlan plan = MakeBigWorldPlan(Quality::Big);
    plan.waterWaitSeconds = 2.0;
    ASSERT_TRUE(demo.Open(plan, now).ok);
    terrain.facts.nodesResident = 40;
    Pump(5.0);
    EXPECT_GE(static_cast<int>(demo.State().phase), static_cast<int>(Phase::EnablingFeatures));
    ASSERT_FALSE(demo.State().warnings.empty());
    EXPECT_NE(demo.State().warnings[0].find("carve"), std::string::npos);
}

TEST_F(BigWorldDemoTest, GrassThatNeverDrawsEndsReadyWithAWarning)
{
    BigWorldPlan plan = MakeBigWorldPlan(Quality::Big);
    plan.grassWaitSeconds = 2.0;
    ASSERT_TRUE(demo.Open(plan, now).ok);
    Pump(6.0, [&] {
        terrain.facts.nodesResident = 40;
        if (water.facts.carvePending) {
            water.SettleCarve();
        }
    });
    EXPECT_TRUE(demo.Ready());
    EXPECT_EQ(demo.State().status, "ready, without grass");
    ASSERT_FALSE(demo.State().warnings.empty());
}

TEST_F(BigWorldDemoTest, ATerrainThatNeverStreamsFailsAndCleansUp)
{
    BigWorldPlan plan = MakeBigWorldPlan(Quality::Big);
    plan.nodeWaitSeconds = 3.0;
    ASSERT_TRUE(demo.Open(plan, now).ok);
    terrain.facts.nodesPending = 9;
    Pump(5.0);
    EXPECT_EQ(demo.State().phase, Phase::Failed);
    EXPECT_FALSE(demo.State().error.empty());
    EXPECT_EQ(terrain.closeCalls, 1);
    EXPECT_FALSE(terrain.facts.open);
    EXPECT_TRUE(water.lakes.empty());
}

TEST_F(BigWorldDemoTest, ARefusedTerrainOpenFailsAtOnce)
{
    terrain.failOpen = true;
    const PortResult r = demo.Open(MakeBigWorldPlan(Quality::Big), now);
    EXPECT_FALSE(r.ok);
    EXPECT_NE(r.message.find("no pool memory"), std::string::npos);
    EXPECT_EQ(demo.State().phase, Phase::Failed);
    EXPECT_EQ(terrain.closeCalls, 0);
    EXPECT_EQ(terrain.cameraCalls, 0);
}

TEST_F(BigWorldDemoTest, ARefusedCameraClosesTheTerrainAgain)
{
    terrain.failCamera = true;
    EXPECT_FALSE(demo.Open(MakeBigWorldPlan(Quality::Big), now).ok);
    EXPECT_EQ(demo.State().phase, Phase::Failed);
    EXPECT_EQ(terrain.closeCalls, 1);
}

TEST_F(BigWorldDemoTest, ARefusedLakeFailsAndClosesTheTerrain)
{
    water.failLake = true;
    ASSERT_TRUE(demo.Open(MakeBigWorldPlan(Quality::Big), now).ok);
    Pump(3.0, [&] { HealthyWorld(); });
    EXPECT_EQ(demo.State().phase, Phase::Failed);
    EXPECT_NE(demo.State().error.find("lake"), std::string::npos);
    EXPECT_EQ(terrain.closeCalls, 1);
    EXPECT_EQ(features.values["VolumetricCloud"]["enabled"], "false");
}

TEST_F(BigWorldDemoTest, AMissingRequiredFeatureFailsAndRemovesTheWater)
{
    features.values.erase("VolumetricCloud");
    ASSERT_TRUE(demo.Open(MakeBigWorldPlan(Quality::Big), now).ok);
    Pump(5.0, [&] { HealthyWorld(); });
    EXPECT_EQ(demo.State().phase, Phase::Failed);
    EXPECT_NE(demo.State().error.find("VolumetricCloud"), std::string::npos);
    EXPECT_EQ(water.removed.size(), 2u);
    EXPECT_EQ(terrain.closeCalls, 1);
    EXPECT_GE(features.restoreCalls, 1);
}

TEST_F(BigWorldDemoTest, AMissingOptionalFeatureIsOnlyAWarning)
{
    features.values.erase("VolumetricCloud");
    BigWorldPlan plan = MakeBigWorldPlan(Quality::Big);
    plan.tweaks[0].required = false; // clouds
    ASSERT_TRUE(demo.Open(plan, now).ok);
    Pump(5.0, [&] { HealthyWorld(); });
    EXPECT_TRUE(demo.Ready());
    EXPECT_FALSE(features.values.contains("VolumetricCloud"));
    ASSERT_FALSE(demo.State().warnings.empty());
}

TEST_F(BigWorldDemoTest, ARefusedFeatureSetFailsTheDemo)
{
    features.failSet = true;
    ASSERT_TRUE(demo.Open(MakeBigWorldPlan(Quality::Big), now).ok);
    Pump(5.0, [&] { HealthyWorld(); });
    EXPECT_EQ(demo.State().phase, Phase::Failed);
    EXPECT_GE(features.restoreCalls, 1);
}

TEST_F(BigWorldDemoTest, MissingPortsRefuseTheOpen)
{
    BigWorldDemo broken({ &terrain, nullptr, &features });
    const PortResult r = broken.Open(MakeBigWorldPlan(Quality::Big), now);
    EXPECT_FALSE(r.ok);
    EXPECT_NE(r.message.find("water"), std::string::npos);
    EXPECT_EQ(terrain.openCalls, 0);
}

TEST_F(BigWorldDemoTest, TheTerrainClosingWhileWaitingFailsTheDemo)
{
    ASSERT_TRUE(demo.Open(MakeBigWorldPlan(Quality::Big), now).ok);
    terrain.facts.nodesPending = 4;
    Pump(1.0);
    terrain.facts.open = false; // another scene replaced it
    Pump(1.0);
    EXPECT_EQ(demo.State().phase, Phase::Failed);
    EXPECT_EQ(terrain.closeCalls, 0);
}

TEST_F(BigWorldDemoTest, ASceneReplacedWhileReadyRestoresEverythingAndGoesIdle)
{
    OpenAndRunToReady();
    terrain.facts.open = false;
    Pump(1.0);
    EXPECT_EQ(demo.State().phase, Phase::Idle);
    EXPECT_EQ(terrain.closeCalls, 0);
    EXPECT_EQ(water.removed.size(), 2u);
    EXPECT_EQ(features.values["VolumetricCloud"]["enabled"], "false");
    EXPECT_EQ(features.values["Grass"]["densityLod0"], "16");
    ASSERT_FALSE(demo.State().warnings.empty());
    EXPECT_NE(demo.State().warnings[0].find("replaced"), std::string::npos);
}

TEST_F(BigWorldDemoTest, CloseUndoesEveryChange)
{
    OpenAndRunToReady();
    const PortResult closed = demo.Close();
    EXPECT_TRUE(closed.ok);
    EXPECT_EQ(demo.State().phase, Phase::Idle);
    EXPECT_EQ(terrain.closeCalls, 1);
    EXPECT_EQ(water.removed.size(), 2u);
    EXPECT_EQ(features.values["VolumetricCloud"]["enabled"], "false");
    EXPECT_EQ(features.values["Grass"]["densityLod0"], "16");
    EXPECT_TRUE(demo.State().waterBodyIds.empty());
    EXPECT_FALSE(demo.State().terrainOwned);
}

TEST_F(BigWorldDemoTest, CloseWhileStillStreamingClosesTheTerrain)
{
    ASSERT_TRUE(demo.Open(MakeBigWorldPlan(Quality::Big), now).ok);
    terrain.facts.nodesPending = 3;
    Pump(1.0);
    EXPECT_TRUE(demo.Close().ok);
    EXPECT_EQ(demo.State().phase, Phase::Idle);
    EXPECT_EQ(terrain.closeCalls, 1);
    Pump(2.0, [&] { terrain.facts.nodesResident = 40; });
    EXPECT_EQ(demo.State().phase, Phase::Idle);
    EXPECT_TRUE(water.lakes.empty());
}

TEST_F(BigWorldDemoTest, CloseWhileIdleIsHarmless)
{
    const PortResult r = demo.Close();
    EXPECT_TRUE(r.ok);
    EXPECT_EQ(terrain.closeCalls, 0);
    EXPECT_EQ(features.restoreCalls, 0);
}

TEST_F(BigWorldDemoTest, CloseAfterAFailureReturnsToIdle)
{
    terrain.failOpen = true;
    demo.Open(MakeBigWorldPlan(Quality::Big), now);
    ASSERT_EQ(demo.State().phase, Phase::Failed);
    EXPECT_TRUE(demo.Close().ok);
    EXPECT_EQ(demo.State().phase, Phase::Idle);
    EXPECT_TRUE(demo.State().error.empty());
}

TEST_F(BigWorldDemoTest, OpeningAgainWhileReadyClosesTheFirstWorldFirst)
{
    OpenAndRunToReady();
    ASSERT_TRUE(demo.Open(MakeBigWorldPlan(Quality::Lean), now).ok);
    EXPECT_EQ(terrain.openCalls, 2);
    EXPECT_EQ(terrain.closeCalls, 1);
    EXPECT_EQ(water.removed.size(), 2u);
    EXPECT_EQ(demo.State().phase, Phase::WaitingForNodes);
    EXPECT_EQ(demo.State().plan.quality, Quality::Lean);
}

TEST_F(BigWorldDemoTest, AFailedOpenCanBeRetried)
{
    terrain.failOpen = true;
    demo.Open(MakeBigWorldPlan(Quality::Big), now);
    terrain.failOpen = false;
    EXPECT_TRUE(demo.Open(MakeBigWorldPlan(Quality::Big), now).ok);
    EXPECT_EQ(demo.State().phase, Phase::WaitingForNodes);
}

TEST_F(BigWorldDemoTest, OceanLevelMismatchBetweenWaterAndGrassIsReported)
{
    OpenAndRunToReady();
    features.values["Grass"]["tile.seaLevel"] = "3.5";
    Pump(1.0);
    ASSERT_FALSE(demo.State().warnings.empty());
    EXPECT_NE(demo.State().warnings[0].find("ocean level"), std::string::npos);
    features.values["Grass"]["tile.seaLevel"] = "0";
    Pump(1.0);
    EXPECT_TRUE(demo.State().warnings.empty());
}

TEST_F(BigWorldDemoTest, GrassWithoutAnOceanWhileWaterHasOneIsReported)
{
    OpenAndRunToReady();
    features.values["Grass"]["tile.seaLevel"] = "-1000000";
    Pump(1.0);
    ASSERT_FALSE(demo.State().warnings.empty());
    EXPECT_NE(demo.State().warnings[0].find("no ocean"), std::string::npos);
}

TEST_F(BigWorldDemoTest, ADisconnectedWaterFeatureIsReported)
{
    OpenAndRunToReady();
    water.facts.featureConnected = false;
    Pump(1.0);
    ASSERT_FALSE(demo.State().warnings.empty());
    EXPECT_NE(demo.State().warnings[0].find("not connected"), std::string::npos);
}

TEST_F(BigWorldDemoTest, HealthReadsTheGrassCounters)
{
    features.values["Grass"]["game.stats.accepted0"] = "100";
    features.values["Grass"]["game.stats.accepted1"] = "200";
    features.values["Grass"]["game.stats.accepted2"] = "300";
    features.values["Grass"]["tile.revision"] = "7";
    OpenAndRunToReady();
    const GrassHealth& g = demo.State().health.grass;
    EXPECT_TRUE(g.available);
    EXPECT_EQ(g.status, "Ok");
    EXPECT_EQ(g.blades[0], 100);
    EXPECT_EQ(g.blades[1], 200);
    EXPECT_EQ(g.blades[2], 300);
    EXPECT_EQ(g.tileRevision, 7);
    EXPECT_TRUE(demo.State().health.cloudsEnabled);
}

TEST_F(BigWorldDemoTest, TickDoesNotPollMoreOftenThanItsInterval)
{
    ASSERT_TRUE(demo.Open(MakeBigWorldPlan(Quality::Big), now).ok);
    demo.Tick(now + 0.11);
    terrain.facts.nodesResident = 40;
    demo.Tick(now + 0.12); // too soon: not read yet
    EXPECT_EQ(demo.State().phase, Phase::WaitingForNodes);
    demo.Tick(now + 0.3);
    EXPECT_NE(demo.State().phase, Phase::WaitingForNodes);
}

TEST_F(BigWorldDemoTest, JsonCarriesPhaseAndHealthAndEscapesText)
{
    OpenAndRunToReady();
    std::string json = BigWorldStateToJson(demo.State());
    EXPECT_NE(json.find("\"phase\":\"Ready\""), std::string::npos);
    EXPECT_NE(json.find("\"ready\":true"), std::string::npos);
    EXPECT_NE(json.find("\"grass\":{"), std::string::npos);
    EXPECT_NE(json.find("\"waterBodyIds\":[10,11]"), std::string::npos);

    BigWorldState odd;
    odd.error = "bad \"quote\" and \\ slash";
    json = BigWorldStateToJson(odd);
    EXPECT_NE(json.find("bad \\\"quote\\\" and \\\\ slash"), std::string::npos);
}

TEST_F(BigWorldDemoTest, EveryPhaseHasAName)
{
    for (const Phase p : { Phase::Idle, Phase::OpeningTerrain, Phase::WaitingForNodes, Phase::AddingWater, Phase::WaitingForWater,
             Phase::EnablingFeatures, Phase::WaitingForGrass, Phase::Ready, Phase::Failed }) {
        EXPECT_NE(std::string(PhaseName(p)), "");
    }
}

} // namespace
} // namespace gte::bigworld
