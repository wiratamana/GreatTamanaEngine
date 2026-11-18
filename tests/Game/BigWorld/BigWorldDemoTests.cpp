// Drives the Big World state machine with fake ports: success path, failure at each phase, scene replaced,
// close while opening, settings restore and the cross-system checks.

#include "Game/BigWorld/BigWorldDemo.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <charconv>
#include <cmath>
#include <map>
#include <set>

namespace gte::bigworld {
namespace {

std::int64_t ParseInt(const std::string& text) noexcept
{
    std::int64_t value = 0;
    std::from_chars(text.data(), text.data() + text.size(), value);
    return value;
}

double ParseDouble(const std::string& text, double fallback) noexcept
{
    double value = 0.0;
    const auto [end, ec] = std::from_chars(text.data(), text.data() + text.size(), value);
    return (ec == std::errc{} && end == text.data() + text.size() && std::isfinite(value)) ? value : fallback;
}

bool ParseBool(const std::string& text) noexcept
{
    return text == "true" || text == "1";
}

bool HasWarningContaining(const std::vector<std::string>& warnings, std::string_view needle)
{
    return std::any_of(warnings.begin(), warnings.end(),
        [needle](const std::string& w) { return w.find(needle) != std::string::npos; });
}

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
        facts.cameraAboveGround = placement.heightAboveGround;
        return failCamera ? PortResult{ false, "camera refused", 0 } : PortResult{};
    }
    PortResult Close() override
    {
        ++closeCalls;
        facts.open = false;
        return {};
    }
    TerrainFacts Facts() const override { return facts; }
    std::optional<double> GroundHeight(double, double) const override { return ground; }

    TerrainFacts facts;
    std::optional<double> ground;
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
        for (auto it = saved.rbegin(); it != saved.rend(); ++it) {
            values[it->feature][it->key] = it->value;
        }
        saved.clear();
        return {};
    }
    FeatureState GetState() const override
    {
        FeatureState out;
        if (const auto g = values.find("Grass"); g != values.end()) {
            const auto get = [&](const char* key) -> std::string {
                const auto it = g->second.find(key);
                return it == g->second.end() ? std::string{} : it->second;
            };
            out.grass.available = true;
            out.grass.status = get("game.status");
            out.grass.tilePresent = ParseBool(get("tile.present"));
            out.grass.seaLevel = ParseDouble(get("tile.seaLevel"), -1.0e6);
            out.grass.tileRevision = ParseInt(get("tile.revision"));
            out.grass.blades[0] = ParseInt(get("game.stats.accepted0"));
            out.grass.blades[1] = ParseInt(get("game.stats.accepted1"));
            out.grass.blades[2] = ParseInt(get("game.stats.accepted2"));
            out.grass.shadowCascades = ParseInt(get("game.shadow.cascades"));
            out.grass.receivesSunShadow = out.grass.shadowCascades > 0;
            out.grass.receivesCloudShadow = ParseBool(get("game.shadow.cloud"));
            out.grass.castCascades = ParseInt(get("game.shadow.castCascades"));
            out.grass.castBlades = ParseInt(get("game.shadow.castBlades"));
        }
        if (const auto c = values.find("VolumetricCloud"); c != values.end()) {
            out.cloudsAvailable = true;
            const auto it = c->second.find("enabled");
            out.cloudsEnabled = it != c->second.end() && ParseBool(it->second);
        }
        if (const auto s = values.find("Shadow"); s != values.end()) {
            const auto it = s->second.find("game.sunElevationDegrees");
            if (it != s->second.end()) {
                out.sunAvailable = true;
                out.sunElevationDegrees = ParseDouble(it->second, 0.0);
                out.sunAboveHorizon = out.sunElevationDegrees > 0.0;
            }
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
        features.values["Bloom"]["enabled"] = "true";
        features.values["Bloom"]["threshold"] = "1";
        features.values["Bloom"]["intensity"] = "0.3";
        features.values["VolumetricCloud"]["enabled"] = "false";
        features.values["VolumetricCloud"]["coverage"] = "0.8";
        features.values["VolumetricCloud"]["shadowRangeKm"] = "32";
        features.values["VolumetricCloud"]["shadowSoftness"] = "1.5";
        features.values["VolumetricCloud"]["shadowFill"] = "0.25";
        features.values["VolumetricCloud"]["sunRays"] = "0.8";
        features.values["VolumetricCloud"]["sunRaysContrast"] = "1";
        features.values["VolumetricCloud"]["sunRaysDecay"] = "1.5";
        features.values["VolumetricCloud"]["sunRaysLength"] = "1.2";
        features.values["VolumetricCloud"]["maxSteps"] = "128";
        features.values["Shadow"]["game.sunElevationDegrees"] = "35";
        features.values["Grass"]["enabled"] = "true";
        features.values["Grass"]["densityLod0"] = "16";
        features.values["Grass"]["game.status"] = "NoTile";
        features.values["Grass"]["tile.present"] = "false";
        features.values["Grass"]["tile.seaLevel"] = "-1000000";
        features.values["Grass"]["game.shadow.cascades"] = "0";
        features.values["Grass"]["game.shadow.cloud"] = "false";
        features.values["Grass"]["game.shadow.castCascades"] = "0";
        features.values["Grass"]["game.shadow.castBlades"] = "0";
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

    // A world that behaves: the terrain streams in, the carve lands, grass starts to draw with sun and cloud shadows.
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
            features.values["Grass"]["game.shadow.cascades"] = "2";
            const bool cloudsOn = features.values.contains("VolumetricCloud")
                && features.values["VolumetricCloud"]["enabled"] == "true";
            features.values["Grass"]["game.shadow.cloud"] = cloudsOn ? "true" : "false";
            const double alt = terrain.facts.cameraAboveGround.value_or(terrain.lastCamera.heightAboveGround);
            features.values["Grass"]["game.shadow.castCascades"] = alt < kGrassShadowCastMaxAltitude ? "1" : "0";
            features.values["Grass"]["game.shadow.castBlades"] = alt < kGrassShadowCastMaxAltitude ? "69000" : "0";
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

    ASSERT_TRUE(demo.Open(MakeBigWorldPlan(Quality::Medium), now).ok);
    EXPECT_EQ(terrain.lastProfile, "lean");

    ASSERT_TRUE(demo.Open(MakeBigWorldPlan(Quality::Lean), now).ok);
    EXPECT_EQ(terrain.lastProfile, "lean");
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
    EXPECT_EQ(features.values["VolumetricCloud"]["coverage"], "0.55");
    EXPECT_EQ(features.values["VolumetricCloud"]["shadowRangeKm"], "16");
    EXPECT_EQ(features.values["VolumetricCloud"]["shadowSoftness"], "0.48");
    EXPECT_EQ(features.values["VolumetricCloud"]["shadowFill"], "0.22");
    EXPECT_EQ(features.values["VolumetricCloud"]["sunRays"], "0.55");
    EXPECT_EQ(features.values["VolumetricCloud"]["sunRaysContrast"], "1.65");
    EXPECT_EQ(features.values["VolumetricCloud"]["sunRaysDecay"], "0.98");
    EXPECT_EQ(features.values["VolumetricCloud"]["sunRaysLength"], "1.25");
    EXPECT_EQ(features.values["VolumetricCloud"]["maxSteps"], "80");
    EXPECT_EQ(features.values["Bloom"]["threshold"], "2");
    EXPECT_EQ(features.values["Bloom"]["intensity"], "0.2");
    EXPECT_EQ(features.values["Grass"]["preset"], "Big");
    ASSERT_EQ(water.lakes.size(), 1u);
    EXPECT_EQ(water.rivers.size(), 1u);
    EXPECT_EQ(demo.State().waterBodyIds.size(), 2u);
    EXPECT_TRUE(demo.State().warnings.empty());
    EXPECT_TRUE(demo.State().info.empty());
}

TEST_F(BigWorldDemoTest, EachTierAppliesItsSettingsAndRestoresRoundTripOnClose)
{
    struct Case {
        Quality quality;
        const char* coverage;
        const char* maxSteps;
    };
    for (const Case c : { Case{ Quality::Big, "0.55", "80" }, Case{ Quality::Medium, "0.55", "72" }, Case{ Quality::Lean, "0.5", "56" } }) {
        OpenAndRunToReady(c.quality);
        EXPECT_EQ(terrain.lastProfile, "lean") << QualityName(c.quality);
        EXPECT_EQ(features.values["VolumetricCloud"]["coverage"], c.coverage) << QualityName(c.quality);
        EXPECT_EQ(features.values["VolumetricCloud"]["shadowRangeKm"], "16") << QualityName(c.quality);
        EXPECT_EQ(features.values["VolumetricCloud"]["shadowSoftness"], "0.48") << QualityName(c.quality);
        EXPECT_EQ(features.values["VolumetricCloud"]["shadowFill"], "0.22") << QualityName(c.quality);
        EXPECT_EQ(features.values["VolumetricCloud"]["sunRays"], "0.55") << QualityName(c.quality);
        EXPECT_EQ(features.values["VolumetricCloud"]["sunRaysContrast"], "1.65") << QualityName(c.quality);
        EXPECT_EQ(features.values["VolumetricCloud"]["sunRaysDecay"], "0.98") << QualityName(c.quality);
        EXPECT_EQ(features.values["VolumetricCloud"]["sunRaysLength"], "1.25") << QualityName(c.quality);
        EXPECT_EQ(features.values["VolumetricCloud"]["maxSteps"], c.maxSteps) << QualityName(c.quality);
        EXPECT_EQ(features.values["Bloom"]["threshold"], "2") << QualityName(c.quality);
        EXPECT_EQ(features.values["Bloom"]["intensity"], "0.2") << QualityName(c.quality);
        ASSERT_TRUE(demo.Close().ok);
        EXPECT_EQ(features.values["VolumetricCloud"]["coverage"], "0.8") << QualityName(c.quality);
        EXPECT_EQ(features.values["VolumetricCloud"]["enabled"], "false");
        EXPECT_EQ(features.values["VolumetricCloud"]["shadowRangeKm"], "32");
        EXPECT_EQ(features.values["VolumetricCloud"]["shadowSoftness"], "1.5");
        EXPECT_EQ(features.values["VolumetricCloud"]["shadowFill"], "0.25");
        EXPECT_EQ(features.values["VolumetricCloud"]["sunRays"], "0.8");
        EXPECT_EQ(features.values["VolumetricCloud"]["sunRaysContrast"], "1");
        EXPECT_EQ(features.values["VolumetricCloud"]["sunRaysDecay"], "1.5");
        EXPECT_EQ(features.values["VolumetricCloud"]["sunRaysLength"], "1.2");
        EXPECT_EQ(features.values["VolumetricCloud"]["maxSteps"], "128");
        EXPECT_EQ(features.values["Bloom"]["threshold"], "1");
        EXPECT_EQ(features.values["Bloom"]["intensity"], "0.3");
    }
}

TEST_F(BigWorldDemoTest, CloudCoverageOverrideAppliesAndRestoresOnClose)
{
    BigWorldPlan plan = MakeBigWorldPlan(Quality::Big);
    SetCloudCoverage(plan, 0.35);
    ASSERT_TRUE(demo.Open(plan, now).ok);
    Pump(5.0, [&] { HealthyWorld(); });
    ASSERT_TRUE(demo.Ready());
    EXPECT_EQ(features.values["VolumetricCloud"]["coverage"], "0.35");
    ASSERT_TRUE(demo.Close().ok);
    EXPECT_EQ(features.values["VolumetricCloud"]["coverage"], "0.8");
}

TEST_F(BigWorldDemoTest, TheLakeIsAheadOfTheCameraAndTheRiverRunsAlongTheHeading)
{
    BigWorldPlan plan = MakeBigWorldPlan(Quality::Big);
    plan.spotX = 0.0;
    plan.spotZ = 0.0;
    plan.headingDegrees = 90.0;
    plan.lakeDistance = 110.0;
    plan.riverStart = -200.0;
    plan.riverEnd = 300.0;
    plan.riverSide = 140.0;
    ASSERT_TRUE(demo.Open(plan, now).ok);
    Pump(5.0, [&] { HealthyWorld(); });
    ASSERT_EQ(water.lakes.size(), 1u);
    ASSERT_GE(water.lakes[0].outline.size(), 3u);
    double cx = 0.0;
    for (const PlanPoint& p : water.lakes[0].outline) {
        cx += p.x;
    }
    EXPECT_NEAR(cx / static_cast<double>(water.lakes[0].outline.size()), 110.0, 25.0);
    EXPECT_DOUBLE_EQ(water.lakes[0].bankWidth, plan.lakeBankWidth);
    ASSERT_EQ(water.rivers.size(), 1u);
    ASSERT_GE(water.rivers[0].points.size(), 2u);
    EXPECT_NEAR(water.rivers[0].points.front().x, -200.0, 1e-6);
    EXPECT_NEAR(water.rivers[0].points.back().x, 300.0, 1e-6);
}

TEST_F(BigWorldDemoTest, LeanTierAddsNoRiver)
{
    OpenAndRunToReady(Quality::Lean);
    EXPECT_TRUE(water.rivers.empty());
    EXPECT_EQ(features.values["Grass"]["preset"], "Lean");
    EXPECT_TRUE(demo.State().warnings.empty());
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
    EXPECT_EQ(features.values["VolumetricCloud"]["coverage"], "0.8");
    EXPECT_EQ(features.values["VolumetricCloud"]["shadowFill"], "0.25");
    EXPECT_EQ(features.values["VolumetricCloud"]["sunRays"], "0.8");
    EXPECT_EQ(features.values["VolumetricCloud"]["sunRaysContrast"], "1");
    EXPECT_EQ(features.values["VolumetricCloud"]["sunRaysDecay"], "1.5");
    EXPECT_EQ(features.values["VolumetricCloud"]["sunRaysLength"], "1.2");
    EXPECT_EQ(features.values["Bloom"]["threshold"], "1");
    EXPECT_EQ(features.values["Bloom"]["intensity"], "0.3");
    EXPECT_EQ(features.values["Grass"]["densityLod0"], "16");
    ASSERT_EQ(demo.State().warnings.size(), 1u); // no stale "no height tile" from the last poll
    EXPECT_EQ(demo.State().warnings[0], kSceneReplacedNote);
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
    EXPECT_EQ(features.values["VolumetricCloud"]["coverage"], "0.8");
    EXPECT_EQ(features.values["VolumetricCloud"]["shadowFill"], "0.25");
    EXPECT_EQ(features.values["VolumetricCloud"]["sunRays"], "0.8");
    EXPECT_EQ(features.values["VolumetricCloud"]["sunRaysContrast"], "1");
    EXPECT_EQ(features.values["VolumetricCloud"]["sunRaysDecay"], "1.5");
    EXPECT_EQ(features.values["VolumetricCloud"]["sunRaysLength"], "1.2");
    EXPECT_EQ(features.values["Bloom"]["threshold"], "1");
    EXPECT_EQ(features.values["Bloom"]["intensity"], "0.3");
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

TEST_F(BigWorldDemoTest, ASpotUnderTheSeaIsReportedAndDryGroundIsNot)
{
    OpenAndRunToReady();
    terrain.ground = 25.0;
    Pump(1.0);
    EXPECT_TRUE(demo.State().warnings.empty());
    terrain.ground = -31.0;
    Pump(1.0);
    ASSERT_FALSE(demo.State().warnings.empty());
    EXPECT_NE(demo.State().warnings[0].find("under the sea"), std::string::npos);
}

TEST_F(BigWorldDemoTest, ADisconnectedWaterFeatureIsReported)
{
    OpenAndRunToReady();
    water.facts.featureConnected = false;
    Pump(1.0);
    ASSERT_FALSE(demo.State().warnings.empty());
    EXPECT_NE(demo.State().warnings[0].find("not connected"), std::string::npos);
}

TEST_F(BigWorldDemoTest, MissingSunShadowCascadesOnGrassWarnsAndPublishedCascadesDoNot)
{
    OpenAndRunToReady();
    EXPECT_TRUE(demo.State().warnings.empty());

    features.values["Grass"]["game.shadow.cascades"] = "0";
    Pump(1.0);
    EXPECT_TRUE(HasWarningContaining(demo.State().warnings, "grass gets no sun shadow"));

    features.values["Grass"]["game.shadow.cascades"] = "2";
    Pump(1.0);
    EXPECT_TRUE(demo.State().warnings.empty());
}

TEST_F(BigWorldDemoTest, GrassNotCastingWarnsBelow15mAndReportsInfoAtOrAbove15m)
{
    OpenAndRunToReady();
    EXPECT_TRUE(demo.State().warnings.empty());
    EXPECT_TRUE(demo.State().info.empty());

    // Camera is at 6 m (< 15 m): missing grass shadow casting is a warning.
    features.values["Grass"]["game.shadow.castCascades"] = "0";
    features.values["Grass"]["game.shadow.castBlades"] = "0";
    Pump(1.0);
    EXPECT_TRUE(HasWarningContaining(demo.State().warnings, "casts no sun shadow"));
    EXPECT_TRUE(demo.State().info.empty());

    // Camera at 15 m or higher: cascades are coarser than shadowMaxTexelM by design, so report info without warning.
    terrain.facts.cameraAboveGround = 22.0;
    Pump(1.0);
    EXPECT_TRUE(demo.State().warnings.empty());
    EXPECT_FALSE(demo.State().info.empty());
    EXPECT_NE(demo.State().info.find("15"), std::string::npos);

    // Back within range with active casting: both warning and info clear.
    terrain.facts.cameraAboveGround = 6.0;
    features.values["Grass"]["game.shadow.castCascades"] = "1";
    Pump(1.0);
    EXPECT_TRUE(demo.State().warnings.empty());
    EXPECT_TRUE(demo.State().info.empty());
}

TEST_F(BigWorldDemoTest, MissingCloudShadowWarnsOnlyWhenCloudsAreEnabled)
{
    OpenAndRunToReady();
    EXPECT_TRUE(demo.State().warnings.empty());

    features.values["Grass"]["game.shadow.cloud"] = "false";
    Pump(1.0);
    EXPECT_TRUE(HasWarningContaining(demo.State().warnings, "cloud shadow"));

    // With clouds turned off, no cloud shadow map is expected.
    features.values["VolumetricCloud"]["enabled"] = "false";
    Pump(1.0);
    EXPECT_TRUE(demo.State().warnings.empty());

    features.values["VolumetricCloud"]["enabled"] = "true";
    features.values["Grass"]["game.shadow.cloud"] = "true";
    Pump(1.0);
    EXPECT_TRUE(demo.State().warnings.empty());
}

TEST_F(BigWorldDemoTest, SunAtOrBelowHorizonWarnsAndAboveHorizonDoesNot)
{
    OpenAndRunToReady();
    EXPECT_TRUE(demo.State().warnings.empty());

    features.values["Shadow"]["game.sunElevationDegrees"] = "0";
    Pump(1.0);
    EXPECT_TRUE(HasWarningContaining(demo.State().warnings, "horizon"));

    features.values["Shadow"]["game.sunElevationDegrees"] = "-6.5";
    Pump(1.0);
    EXPECT_TRUE(HasWarningContaining(demo.State().warnings, "horizon"));

    features.values["Shadow"]["game.sunElevationDegrees"] = "18";
    Pump(1.0);
    EXPECT_TRUE(demo.State().warnings.empty());
}

TEST_F(BigWorldDemoTest, HealthReadsTheGrassCountersAndShadowState)
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
    EXPECT_TRUE(g.receivesSunShadow);
    EXPECT_TRUE(g.receivesCloudShadow);
    EXPECT_EQ(g.castCascades, 1);
    EXPECT_EQ(g.castBlades, 69000);
    EXPECT_TRUE(demo.State().health.cloudsEnabled);
    EXPECT_TRUE(demo.State().health.sunAboveHorizon);
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

TEST_F(BigWorldDemoTest, JsonCarriesPhasePlanKeysShadowKeysAndEscapesText)
{
    OpenAndRunToReady();
    std::string json = BigWorldStateToJson(demo.State());
    EXPECT_NE(json.find("\"phase\":\"Ready\""), std::string::npos);
    EXPECT_NE(json.find("\"ready\":true"), std::string::npos);
    EXPECT_NE(json.find("\"info\":\"\""), std::string::npos);
    EXPECT_NE(json.find("\"terrainProfile\":\"lean\""), std::string::npos);
    EXPECT_NE(json.find("\"cloudCoverage\":0.55"), std::string::npos);
    EXPECT_NE(json.find("\"cloudShadowRangeKm\":16"), std::string::npos);
    EXPECT_NE(json.find("\"cloudShadowSoftness\":0.48"), std::string::npos);
    EXPECT_NE(json.find("\"cloudShadowFill\":0.22"), std::string::npos);
    EXPECT_NE(json.find("\"cloudSunRays\":0.55"), std::string::npos);
    EXPECT_NE(json.find("\"cloudSunRaysContrast\":1.65"), std::string::npos);
    EXPECT_NE(json.find("\"cloudSunRaysDecay\":0.98"), std::string::npos);
    EXPECT_NE(json.find("\"cloudSunRaysLength\":1.25"), std::string::npos);
    EXPECT_NE(json.find("\"bloomThreshold\":2"), std::string::npos);
    EXPECT_NE(json.find("\"bloomIntensity\":0.2"), std::string::npos);
    EXPECT_EQ(json.find("volumetric"), std::string::npos);
    EXPECT_EQ(json.find("cloudShaftMist"), std::string::npos);
    EXPECT_NE(json.find("\"grass\":{"), std::string::npos);
    EXPECT_NE(json.find("\"receivesSunShadow\":true"), std::string::npos);
    EXPECT_NE(json.find("\"receivesCloudShadow\":true"), std::string::npos);
    EXPECT_NE(json.find("\"castCascades\":1"), std::string::npos);
    EXPECT_NE(json.find("\"castBlades\":69000"), std::string::npos);
    EXPECT_NE(json.find("\"waterBodyIds\":[10,11]"), std::string::npos);

    BigWorldState odd;
    odd.error = "bad \"quote\" and \\ slash";
    json = BigWorldStateToJson(odd);
    EXPECT_NE(json.find("bad \\\"quote\\\" and \\\\ slash"), std::string::npos);
    EXPECT_NE(json.find("\"receivesSunShadow\":false"), std::string::npos);
    EXPECT_NE(json.find("\"receivesCloudShadow\":false"), std::string::npos);
    EXPECT_NE(json.find("\"castCascades\":0"), std::string::npos);
    EXPECT_NE(json.find("\"castBlades\":0"), std::string::npos);
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
