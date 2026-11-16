#include "Editor/Terrain/TerrainCommands.h"

#include <gtest/gtest.h>

#include <string>

namespace gte {

namespace {

class FakeWorld final : public terrain::ITerrainWorld {
public:
    const terrain::TerrainSettings& Settings() const override { return settings; }
    terrain::TerrainResult SetSettings(const terrain::TerrainSettings& s) override
    {
        settings = s;
        ++settingsCalls;
        return {};
    }
    const terrain::TerrainRuntimeOptions& RuntimeOptions() const override { return runtime; }
    terrain::TerrainResult SetRuntimeOptions(const terrain::TerrainRuntimeOptions& o) override
    {
        runtime = o;
        return {};
    }
    bool IsOpen() const override { return open; }
    terrain::TerrainResult Open(const terrain::TerrainOpenOptions& o) override
    {
        open = true;
        lastOpen = o;
        return { true, "opened", terrain::SettingsChange::None };
    }
    terrain::TerrainResult Close() override
    {
        open = false;
        return { true, "closed", terrain::SettingsChange::None };
    }
    terrain::TerrainResult SetLivePreview(bool enabled) override
    {
        preview = enabled;
        return {};
    }
    bool LivePreview() const override { return preview; }
    terrain::TerrainResult SetPreviewWindow(const terrain::TerrainPreviewWindow& w) override
    {
        window = w;
        return {};
    }
    terrain::TerrainPreviewWindow PreviewWindow() const override { return window; }
    void SetDebugAabb(bool enabled) override { aabb = enabled; }
    bool DebugAabb() const override { return aabb; }
    void CollectNodes(std::vector<terrain::TerrainNodeInfo>&) const override {}
    terrain::TerrainStats Stats() const override { return {}; }
    terrain::TerrainResult SaveAsset(const std::filesystem::path& p) override
    {
        savedPath = p.string();
        return {};
    }
    terrain::TerrainResult LoadAsset(const std::filesystem::path&) override { return { false, "no such file", {} }; }
    terrain::TerrainResult StartFlight(const terrain::FlightSpec& s) override
    {
        flight = s;
        flying = true;
        return {};
    }
    void StopFlight() override { flying = false; }
    bool FlightActive() const override { return flying; }

    terrain::TerrainSettings settings;
    terrain::TerrainRuntimeOptions runtime;
    terrain::TerrainOpenOptions lastOpen;
    terrain::TerrainPreviewWindow window;
    terrain::FlightSpec flight;
    std::string savedPath;
    int settingsCalls = 0;
    bool open = false;
    bool preview = false;
    bool aabb = false;
    bool flying = false;
};

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

} // namespace gte
