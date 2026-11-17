#include "Features/Terrain/Editor/TerrainCommands.h"

#include "FakeTerrainWorld.h"

#include <gtest/gtest.h>

#include <string>

namespace gte {

namespace {

using FakeWorld = FakeTerrainWorld;

TerrainCommandResult Exec(FakeWorld& world, const std::string& action,
    std::vector<std::pair<std::string, std::string>> params = {})
{
    return ExecuteTerrainCommand(world, TerrainCommandRequest{ action, std::move(params) });
}

} // namespace

TEST(TerrainCommandsTest, StateReportsStatsRuntimeAndSettings)
{
    FakeWorld world;
    const TerrainCommandResult result = Exec(world, "state");
    EXPECT_EQ(result.httpStatus, 200);
    EXPECT_NE(result.body.find("\"runtime\":{"), std::string::npos);
    EXPECT_NE(result.body.find("\"settings\":{"), std::string::npos);
    EXPECT_NE(result.body.find("\"heightScale\":"), std::string::npos);
    EXPECT_EQ(result.body.back(), '}');
}

TEST(TerrainCommandsTest, SetAppliesAllKeysInOneSettingsCall)
{
    FakeWorld world;
    const TerrainCommandResult result =
        Exec(world, "set", { { "heightScale", "800" }, { "seed", "5" }, { "maxVisibleNodes", "200" }, { "livePreview", "true" } });
    EXPECT_EQ(result.httpStatus, 200);
    EXPECT_FLOAT_EQ(world.settings.heightScale, 800.0f);
    EXPECT_EQ(world.settings.seed, 5u);
    EXPECT_EQ(world.runtime.maxVisibleNodes, 200);
    EXPECT_TRUE(world.preview);
    EXPECT_EQ(world.settingsCalls, 1);
}

TEST(TerrainCommandsTest, SetRejectsUnknownKeysWithoutChangingAnything)
{
    FakeWorld world;
    const TerrainCommandResult result = Exec(world, "set", { { "heightScale", "800" }, { "bogus", "1" } });
    EXPECT_EQ(result.httpStatus, 400);
    EXPECT_NE(result.body.find("bogus"), std::string::npos);
    EXPECT_EQ(world.settingsCalls, 0);
}

TEST(TerrainCommandsTest, OpenParsesProfileSlotsWaterAndBudget)
{
    FakeWorld world;
    const TerrainCommandResult result =
        Exec(world, "open", { { "profile", "geometry_only" }, { "poolSlots", "300" }, { "water", "false" }, { "budget", "false" } });
    EXPECT_EQ(result.httpStatus, 200);
    EXPECT_TRUE(world.open);
    EXPECT_EQ(world.lastOpen.profile, terrain::TerrainProfile::GeometryOnly);
    EXPECT_EQ(world.lastOpen.poolSlots, 300u);
    EXPECT_FALSE(world.lastOpen.water);
    EXPECT_FALSE(world.lastOpen.renderBudget);
    EXPECT_EQ(Exec(world, "open", { { "profile", "nope" } }).httpStatus, 400);
    EXPECT_EQ(Exec(world, "close").httpStatus, 200);
    EXPECT_FALSE(world.open);
}

TEST(TerrainCommandsTest, FlightMapsEveryOption)
{
    FakeWorld world;
    const TerrainCommandResult result = Exec(world, "flight",
        { { "kind", "line" }, { "x", "10" }, { "z", "-20" }, { "heading", "40" }, { "speed", "300" }, { "alt", "120" },
            { "lookDown", "5" }, { "fixedStep", "0.016" }, { "duration", "30" } });
    EXPECT_EQ(result.httpStatus, 200);
    EXPECT_TRUE(world.flying);
    EXPECT_EQ(world.flight.kind, terrain::FlightKind::Line);
    EXPECT_DOUBLE_EQ(world.flight.centerX, 10.0);
    EXPECT_DOUBLE_EQ(world.flight.centerZ, -20.0);
    EXPECT_DOUBLE_EQ(world.flight.headingDegrees, 40.0);
    EXPECT_DOUBLE_EQ(world.flight.speed, 300.0);
    EXPECT_DOUBLE_EQ(world.flight.altitudeAboveGround, 120.0);
    EXPECT_DOUBLE_EQ(world.flight.fixedStepSeconds, 0.016);
    EXPECT_EQ(Exec(world, "stop_flight").httpStatus, 200);
    EXPECT_FALSE(world.flying);
    EXPECT_EQ(Exec(world, "flight", { { "speed", "fast" } }).httpStatus, 400);
}

TEST(TerrainCommandsTest, SaveNeedsAPathAndFailuresAreConflicts)
{
    FakeWorld world;
    EXPECT_EQ(Exec(world, "save").httpStatus, 400);
    EXPECT_EQ(Exec(world, "save", { { "path", "C:/x/world.gta" } }).httpStatus, 200);
    EXPECT_EQ(world.savedPath, "C:/x/world.gta");
    EXPECT_EQ(Exec(world, "load", { { "path", "C:/missing.gta" } }).httpStatus, 409);
}

TEST(TerrainCommandsTest, UnknownActionIs404)
{
    FakeWorld world;
    EXPECT_EQ(Exec(world, "teleport").httpStatus, 404);
}

TEST(TerrainCommandsTest, MapIsAPngAndNeedsAnOpenWorld)
{
    FakeWorld world;
    EXPECT_EQ(Exec(world, "map").httpStatus, 409);
    world.open = true;
    world.nodes.push_back({});
    world.nodes.back().key = terrain::NodeKey{ 0, 0, 2 };
    world.nodes.back().minX = -100.0;
    world.nodes.back().maxX = 100.0;
    world.nodes.back().minZ = -100.0;
    world.nodes.back().maxZ = 100.0;
    const TerrainCommandResult result = Exec(world, "map", { { "size", "128" }, { "mode", "state" } });
    ASSERT_EQ(result.httpStatus, 200);
    EXPECT_EQ(result.contentType, "image/png");
    ASSERT_GT(result.body.size(), 8u);
    EXPECT_EQ(result.body.substr(1, 3), "PNG");
    EXPECT_FALSE(world.aabb); // The box flag is put back.
    EXPECT_EQ(Exec(world, "map", { { "mode", "rainbow" } }).httpStatus, 400);
    EXPECT_EQ(Exec(world, "map", { { "size", "big" } }).httpStatus, 400);
}

TEST(TerrainCommandsTest, CameraSetsModeAndPose)
{
    FakeWorld world;
    const TerrainCommandResult result =
        Exec(world, "cam", { { "mode", "orbit" }, { "x", "100" }, { "alt", "250" }, { "yaw", "45" }, { "pitch", "20" }, { "dist", "800" } });
    EXPECT_EQ(result.httpStatus, 200);
    EXPECT_EQ(world.cameraMode, terrain::CameraMode::Orbit);
    ASSERT_TRUE(world.lastPose.x.has_value());
    EXPECT_DOUBLE_EQ(*world.lastPose.x, 100.0);
    EXPECT_TRUE(world.lastPose.heightAboveGround);
    EXPECT_DOUBLE_EQ(*world.lastPose.y, 250.0);
    EXPECT_DOUBLE_EQ(*world.lastPose.yawDegrees, 45.0);
    EXPECT_DOUBLE_EQ(*world.lastPose.orbitDistance, 800.0);
    EXPECT_EQ(Exec(world, "cam", { { "mode", "fly" } }).httpStatus, 400);
    EXPECT_EQ(Exec(world, "cam", { { "bogus", "1" } }).httpStatus, 400);
}

TEST(TerrainCommandsTest, CameraInputQueuesFramesOfInput)
{
    FakeWorld world;
    const TerrainCommandResult result =
        Exec(world, "cam_input", { { "fwd", "1" }, { "boost", "true" }, { "lookx", "12" }, { "frames", "30" } });
    EXPECT_EQ(result.httpStatus, 200);
    EXPECT_FLOAT_EQ(world.queued.moveForward, 1.0f);
    EXPECT_TRUE(world.queued.boost);
    EXPECT_TRUE(world.queued.look);
    EXPECT_FLOAT_EQ(world.queued.lookDeltaX, 12.0f);
    EXPECT_EQ(world.queuedFrames, 30);
    EXPECT_EQ(Exec(world, "cam_input", { { "fwd", "x" } }).httpStatus, 400);
}
} // namespace gte
