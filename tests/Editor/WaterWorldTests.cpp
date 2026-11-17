// Tier 1: the water world feeds the terrain carve (debounced) and the Water feature, and answers the HTTP commands.

#include "Editor/Water/WaterCommands.h"
#include "Editor/Water/WaterWorld.h"
#include "../Features/Terrain/Editor/FakeTerrainWorld.h"
#include "Features/Water/World/WaterEdit.h"
#include "Features/Water/World/WaterJson.h"

#include <gtest/gtest.h>

#include <filesystem>

namespace gte {

namespace {

// Records what the world pushes to the render feature.
class FakeWaterFeature final : public fx::IFeatureDebugEndpoint {
public:
    fx::FeatureDebugResult GetState() const override { return {}; }
    fx::FeatureDebugResult Set(std::span<const fx::FeatureDebugArg> args) override
    {
        ++sets;
        lastSet.assign(args.begin(), args.end());
        fx::FeatureDebugResult r;
        r.ok = acceptSet;
        r.badKey = acceptSet ? "" : "seaLevel";
        return r;
    }
    fx::FeatureDebugResult Invoke(std::string_view action, std::span<const fx::FeatureDebugArg> args) override
    {
        calls.push_back(std::string(action));
        if (action != "clear_bodies") {
            lastBodyArgs.assign(args.begin(), args.end());
        }
        return {};
    }
    std::string Arg(const std::string& key) const
    {
        for (const auto& a : lastBodyArgs) {
            if (a.key == key) {
                return a.value;
            }
        }
        return {};
    }

    int sets = 0;
    bool acceptSet = true;
    std::vector<fx::FeatureDebugArg> lastSet;
    std::vector<fx::FeatureDebugArg> lastBodyArgs;
    std::vector<std::string> calls;
};

struct Rig {
    FakeTerrainWorld terrain;
    FakeWaterFeature feature;
    bool withFeature = true;
    WaterWorld world{ [this] {
        WaterWorldServices s;
        s.terrain = &terrain;
        s.waterFeature = withFeature ? &feature : nullptr;
        return s;
    } };
    Rig() { terrain.open = true; }
};

TerrainCommandResult Exec(Rig& rig, const std::string& action, std::vector<std::pair<std::string, std::string>> params = {})
{
    TerrainCommandRequest request;
    request.action = "water." + action;
    request.params = std::move(params);
    return ExecuteWaterCommand(rig.world, request);
}

bool Contains(const std::string& text, const std::string& part) { return text.find(part) != std::string::npos; }

} // namespace

TEST(WaterWorldTests, StartsWithOneOceanAndPushesItToTheFeature)
{
    Rig rig;
    rig.world.Tick(1.0);
    ASSERT_EQ(rig.world.Bodies()->Bodies().size(), 1u);
    EXPECT_EQ(rig.world.Bodies()->Bodies()[0].kind, water::WaterBodyKind::Ocean);
    EXPECT_EQ(rig.feature.sets, 1);
    ASSERT_GE(rig.feature.calls.size(), 2u);
    EXPECT_EQ(rig.feature.calls.front(), "clear_bodies");
    EXPECT_EQ(rig.feature.calls.back(), "add_ocean");
    EXPECT_TRUE(rig.world.Stats().featureConnected);
    EXPECT_FALSE(rig.terrain.oceanQuad) << "the flat quad is hidden once the feature takes over";
}

TEST(WaterWorldTests, WithoutTheFeatureTheQuadStaysAndNothingIsPushed)
{
    Rig rig;
    rig.withFeature = false;
    rig.world.Tick(1.0);
    EXPECT_FALSE(rig.world.Stats().featureConnected);
    EXPECT_TRUE(rig.terrain.oceanQuad);
}

TEST(WaterWorldTests, LakeAndRiverReachTheFeatureWithTheirShape)
{
    Rig rig;
    water::WaterBody lake = water::MakeBoxLake("Lake", 100, 200, 80, 60, 33.0f);
    ASSERT_TRUE(rig.world.AddBody(lake).ok);
    rig.world.Tick(1.0);
    EXPECT_EQ(rig.feature.calls.back(), "add_lake");
    EXPECT_EQ(rig.feature.Arg("level"), "33.0000");
    EXPECT_TRUE(Contains(rig.feature.Arg("points"), "60.0000,170.0000;140.0000,170.0000;140.0000,230.0000;60.0000,230.0000"));

    water::WaterBody river = water::MakeRiverBody("River", { { 0, 0, 20.0f, 10.0f }, { 100, 0, 18.0f, 14.0f }, { 200, 50, 15.0f, 14.0f } });
    ASSERT_TRUE(rig.world.AddBody(river).ok);
    rig.world.Tick(2.0);
    EXPECT_EQ(rig.feature.calls.back(), "add_river");
    EXPECT_EQ(rig.feature.Arg("spline"), "1");
    EXPECT_EQ(rig.feature.Arg("samples"), "31");
    EXPECT_TRUE(Contains(rig.feature.Arg("points"), "0.0000,0.0000,20.0000,5.0000;"));
}

TEST(WaterWorldTests, CarveWaitsForTheDebounceAndSendsOnlyDirtyRects)
{
    Rig rig;
    rig.world.Tick(1.0);
    EXPECT_EQ(rig.terrain.carveCalls, 0);
    const auto added = rig.world.AddBody(water::MakeBoxLake("Lake", 0, 0, 100, 100, 10.0f));
    ASSERT_TRUE(added.ok);
    rig.world.Tick(1.1);
    EXPECT_EQ(rig.terrain.carveCalls, 0) << "still inside the 0.3 s rest";
    EXPECT_TRUE(rig.world.Stats().carvePending);

    // Dragging a corner keeps resetting the timer.
    ASSERT_TRUE(rig.world.MovePoint(added.id, 0, -60.0, -60.0, std::nullopt, std::nullopt).ok);
    rig.world.Tick(1.3);
    ASSERT_TRUE(rig.world.MovePoint(added.id, 0, -70.0, -70.0, std::nullopt, std::nullopt).ok);
    rig.world.Tick(1.5);
    EXPECT_EQ(rig.terrain.carveCalls, 0);

    rig.world.Tick(1.9);
    EXPECT_EQ(rig.terrain.carveCalls, 1);
    ASSERT_NE(rig.terrain.lastCarve, nullptr);
    EXPECT_FALSE(rig.terrain.lastDirty.empty());
    EXPECT_FALSE(rig.world.Stats().carvePending);
    const std::uint64_t firstHash = rig.terrain.lastCarve->Hash();

    // Look-only edits do not touch the terrain.
    water::WaterBody recolor = *rig.world.Bodies()->Find(added.id);
    recolor.look.waveScale = 0.1f;
    ASSERT_TRUE(rig.world.UpdateBody(recolor).ok);
    rig.world.Tick(3.0);
    EXPECT_EQ(rig.terrain.carveCalls, 1);

    // Removing the lake hands over a null carve and dirties the old footprint.
    ASSERT_TRUE(rig.world.RemoveBody(added.id).ok);
    rig.world.FlushCarve();
    rig.world.Tick(3.05);
    EXPECT_EQ(rig.terrain.carveCalls, 2);
    EXPECT_EQ(rig.terrain.lastCarve, nullptr);
    EXPECT_FALSE(rig.terrain.lastDirty.empty());
    EXPECT_NE(firstHash, 0u);
}

TEST(WaterWorldTests, MasterCarveSwitchRemovesTheCarve)
{
    Rig rig;
    ASSERT_TRUE(rig.world.AddBody(water::MakeBoxLake("Lake", 0, 0, 100, 100, 10.0f)).ok);
    rig.world.FlushCarve();
    rig.world.Tick(5.0);
    ASSERT_NE(rig.terrain.lastCarve, nullptr);
    water::WaterSettings s = rig.world.Settings();
    s.carveTerrain = false;
    ASSERT_TRUE(rig.world.SetSettings(s).ok);
    rig.world.FlushCarve();
    rig.world.Tick(6.0);
    EXPECT_EQ(rig.terrain.lastCarve, nullptr);
}

TEST(WaterWorldTests, RefusedEditsChangeNothing)
{
    Rig rig;
    const std::uint64_t before = rig.world.Stats().revision;
    water::WaterBody bad;
    bad.kind = water::WaterBodyKind::Lake;
    bad.polygon = { { 0, 0 }, { 1, 1 } };
    EXPECT_FALSE(rig.world.AddBody(bad).ok);
    EXPECT_FALSE(rig.world.AddBody(water::MakeOceanBody(0.0f)).ok) << "a second ocean";
    EXPECT_FALSE(rig.world.RemoveBody(999).ok);
    EXPECT_FALSE(rig.world.MovePoint(1, 0, 0, 0, std::nullopt, std::nullopt).ok) << "an ocean has no points";
    EXPECT_EQ(rig.world.Stats().revision, before);
    EXPECT_EQ(rig.world.Bodies()->Bodies().size(), 1u);
    for (int i = 0; i < 15; ++i) {
        ASSERT_TRUE(rig.world.AddBody(water::MakeBoxLake("", i * 300.0, 0, 100, 100, 5.0f)).ok);
    }
    EXPECT_FALSE(rig.world.AddBody(water::MakeBoxLake("", 9000, 0, 100, 100, 5.0f)).ok) << "17th body";
}

TEST(WaterWorldTests, FeatureRefusalIsReportedAndRetried)
{
    Rig rig;
    rig.feature.acceptSet = false;
    rig.world.Tick(1.0);
    EXPECT_FALSE(rig.world.Stats().featureConnected);
    EXPECT_NE(rig.world.Stats().lastError.find("seaLevel"), std::string::npos);
    rig.feature.acceptSet = true;
    rig.world.Tick(1.5);
    EXPECT_EQ(rig.feature.sets, 1) << "retries only after a second";
    rig.world.Tick(2.2);
    EXPECT_TRUE(rig.world.Stats().featureConnected);
}

TEST(WaterWorldTests, SaveAndLoadRoundTrip)
{
    Rig rig;
    ASSERT_TRUE(rig.world.AddBody(water::MakeBoxLake("Lake", 10, 20, 80, 60, 12.0f)).ok);
    water::WaterSettings s = rig.world.Settings();
    s.waveHeight = 3.0f;
    ASSERT_TRUE(rig.world.SetSettings(s).ok);
    const auto path = std::filesystem::temp_directory_path() / "gte_water_roundtrip.json";
    ASSERT_TRUE(rig.world.SaveFile(path).ok);
    const std::uint64_t hash = rig.world.Bodies()->Hash();

    Rig other;
    ASSERT_TRUE(other.world.LoadFile(path).ok);
    EXPECT_EQ(other.world.Bodies()->Hash(), hash);
    EXPECT_FALSE(other.world.LoadFile(path.string() + ".missing").ok);
    std::filesystem::remove(path);
}

TEST(WaterWorldTests, ProbeReportsDepthAndShoreDistance)
{
    Rig rig;
    // The fake ground is 100 + 0.01 x + 0.02 z. A lake at level 105 around the origin covers ground < 105 only partly.
    ASSERT_TRUE(rig.world.AddBody(water::MakeBoxLake("Lake", 0, 0, 100, 100, 105.0f)).ok);
    const water::WaterProbeResult inside = rig.world.Probe(0.0, 0.0);
    ASSERT_TRUE(inside.terrainKnown);
    EXPECT_TRUE(inside.water.hasWater);
    EXPECT_NEAR(inside.depth, 5.0, 1e-6);
    ASSERT_TRUE(inside.shoreDistance.has_value());
    EXPECT_NEAR(*inside.shoreDistance, 50.0, 2.1);
    rig.terrain.open = false;
    EXPECT_FALSE(rig.world.Probe(0.0, 0.0).terrainKnown);
}

TEST(WaterCommandsTests, AddListProbeMoveRemoveThroughTheRoutes)
{
    Rig rig;
    TerrainCommandResult r = Exec(rig, "add_lake", { { "cx", "100" }, { "cz", "100" }, { "sx", "120" }, { "sz", "80" }, { "level", "7" }, { "name", "Pond" } });
    ASSERT_EQ(r.httpStatus, 200) << r.body;
    EXPECT_TRUE(Contains(r.body, "\"id\":2"));

    r = Exec(rig, "add_river", { { "points", "0,20,0,10;100,18,50,12;200,15,0,12" }, { "depth", "3" } });
    ASSERT_EQ(r.httpStatus, 200) << r.body;
    r = Exec(rig, "add_river", { { "points", "0,20,0;100,18,50" }, { "width", "9" } });
    EXPECT_EQ(r.httpStatus, 200) << r.body;
    r = Exec(rig, "add_river", { { "points", "0,20" } });
    EXPECT_EQ(r.httpStatus, 400);
    r = Exec(rig, "add_river", {});
    EXPECT_EQ(r.httpStatus, 400);

    r = Exec(rig, "list");
    EXPECT_TRUE(Contains(r.body, "\"Pond\""));
    EXPECT_TRUE(Contains(r.body, "\"river\""));

    r = Exec(rig, "move_point", { { "id", "2" }, { "index", "1" }, { "x", "150" }, { "z", "120" } });
    ASSERT_EQ(r.httpStatus, 200) << r.body;
    EXPECT_EQ(rig.world.Bodies()->Find(2)->polygon[1].x, 150.0);
    r = Exec(rig, "move_point", { { "id", "2" }, { "index", "9" }, { "x", "1" }, { "z", "1" } });
    EXPECT_EQ(r.httpStatus, 409);
    r = Exec(rig, "add_point", { { "id", "2" }, { "index", "1" }, { "x", "130" }, { "z", "50" } });
    EXPECT_EQ(r.httpStatus, 200);
    r = Exec(rig, "remove_point", { { "id", "2" }, { "index", "1" } });
    EXPECT_EQ(r.httpStatus, 200);

    r = Exec(rig, "set_body", { { "id", "2" }, { "level", "9" }, { "depth", "4" }, { "carve", "false" } });
    ASSERT_EQ(r.httpStatus, 200) << r.body;
    EXPECT_FLOAT_EQ(rig.world.Bodies()->Find(2)->level, 9.0f);
    EXPECT_FALSE(rig.world.Bodies()->Find(2)->carveTerrain);
    r = Exec(rig, "set_body", { { "id", "2" }, { "bogus", "1" } });
    EXPECT_EQ(r.httpStatus, 400);

    r = Exec(rig, "probe", { { "x", "100" }, { "z", "100" } });
    ASSERT_EQ(r.httpStatus, 200) << r.body;
    EXPECT_TRUE(Contains(r.body, "\"hasWater\":true"));
    EXPECT_TRUE(Contains(r.body, "\"kind\":\"lake\""));
    r = Exec(rig, "probe", { { "x", "abc" }, { "z", "1" } });
    EXPECT_EQ(r.httpStatus, 400);

    r = Exec(rig, "remove", { { "id", "2" } });
    EXPECT_EQ(r.httpStatus, 200);
    r = Exec(rig, "remove", { { "id", "2" } });
    EXPECT_EQ(r.httpStatus, 409);
    r = Exec(rig, "nope");
    EXPECT_EQ(r.httpStatus, 404);
    r = Exec(rig, "clear");
    EXPECT_EQ(r.httpStatus, 200);
    EXPECT_TRUE(rig.world.Bodies()->Bodies().empty());
}

TEST(WaterCommandsTests, SetStateMapAndLeaks)
{
    Rig rig;
    TerrainCommandResult r = Exec(rig, "set", { { "waveHeight", "2.5" }, { "ssrEnabled", "true" }, { "shallowColor", "0.1,0.2,0.3" }, { "carveDebounce", "0.1" } });
    ASSERT_EQ(r.httpStatus, 200) << r.body;
    EXPECT_FLOAT_EQ(rig.world.Settings().waveHeight, 2.5f);
    EXPECT_TRUE(rig.world.Settings().ssrEnabled);
    EXPECT_DOUBLE_EQ(rig.world.CarveDebounce(), 0.1);
    r = Exec(rig, "set", { { "waveHeight", "oops" } });
    EXPECT_EQ(r.httpStatus, 400);
    EXPECT_FLOAT_EQ(rig.world.Settings().waveHeight, 2.5f) << "all or nothing";
    r = Exec(rig, "set", { { "quality", "low" } });
    EXPECT_EQ(r.httpStatus, 200);
    EXPECT_EQ(rig.world.Settings().waveCount, 2);

    r = Exec(rig, "state");
    EXPECT_TRUE(Contains(r.body, "\"bodyCount\":1"));
    EXPECT_TRUE(Contains(r.body, "\"settings\":{"));

    ASSERT_EQ(Exec(rig, "add_lake", { { "cx", "0" }, { "cz", "0" }, { "sx", "100" }, { "sz", "100" }, { "level", "90" } }).httpStatus, 200);
    r = Exec(rig, "leaks", { { "id", "2" } });
    ASSERT_EQ(r.httpStatus, 200) << r.body;
    EXPECT_TRUE(Contains(r.body, "\"below\":0"));

    r = Exec(rig, "map", { { "size", "128" }, { "range", "200" } });
    ASSERT_EQ(r.httpStatus, 200);
    EXPECT_EQ(r.contentType, "image/png");
    EXPECT_GT(r.body.size(), 100u);
    EXPECT_EQ(static_cast<unsigned char>(r.body[1]), 'P');
    rig.terrain.open = false;
    EXPECT_EQ(Exec(rig, "map").httpStatus, 409);

    r = Exec(rig, "save");
    EXPECT_EQ(r.httpStatus, 400);
}

TEST(WaterCommandsTests, AddLakeCanPickItsLevelFromTheGround)
{
    Rig rig;
    const TerrainCommandResult r = Exec(rig, "add_lake", { { "cx", "0" }, { "cz", "0" }, { "sx", "100" }, { "sz", "100" }, { "auto_level", "true" } });
    ASSERT_EQ(r.httpStatus, 200) << r.body;
    // The lowest outline ground of the fake slope is at (-50, -50): 100 - 0.5 - 1.0 = 98.5.
    EXPECT_NEAR(rig.world.Bodies()->Find(2)->level, 98.5f, 1.0f);
}

} // namespace gte
