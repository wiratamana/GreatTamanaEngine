// Tier 1 tests for the /physics_debug/* routes: pure parsing and JSON building, plus the 503 of a missing bridge.

#include "NetworkTestHelpers.h"
#include "Network/NetworkRoutes.h"
#include "Network/NetworkServer.h"

#include <gtest/gtest.h>
#include <httplib.h>
#include <nlohmann/json.hpp>

#include <initializer_list>
#include <limits>
#include <string>

namespace {

using namespace gte;
using namespace gte::Network;
using Params = PhysicsDebugQueryParams;

// ---- Query parsers --------------------------------------------------------

TEST(PhysicsDebugEntityQueryTests, RequiresEntityAndRejectsOthers)
{
    EXPECT_TRUE(ParsePhysicsDebugEntityQuery({ { "entity", "Furina" } }).valid);
    EXPECT_EQ(ParsePhysicsDebugEntityQuery({ { "entity", "Furina" } }).entity, "Furina");
    EXPECT_FALSE(ParsePhysicsDebugEntityQuery({}).valid);
    EXPECT_FALSE(ParsePhysicsDebugEntityQuery({ { "entity", "" } }).valid);

    const auto unknown = ParsePhysicsDebugEntityQuery({ { "entity", "A" }, { "bogus", "1" } });
    EXPECT_FALSE(unknown.valid);
    EXPECT_NE(unknown.errorMessage.find("bogus"), std::string::npos);

    const auto duplicate = ParsePhysicsDebugEntityQuery({ { "entity", "A" }, { "entity", "B" } });
    EXPECT_FALSE(duplicate.valid);
    EXPECT_NE(duplicate.errorMessage.find("entity"), std::string::npos);
}

TEST(PhysicsDebugSetQueryTests, SplitsEntityFromSettings)
{
    const auto parsed = ParsePhysicsDebugSetQuery({ { "gravity_y", "-98" }, { "entity", "A" }, { "rig_frozen", "true" } });
    ASSERT_TRUE(parsed.valid);
    EXPECT_EQ(parsed.entity, "A");
    ASSERT_EQ(parsed.pairs.size(), 2u);
    EXPECT_EQ(parsed.pairs[0].first, "gravity_y");
    EXPECT_EQ(parsed.pairs[0].second, "-98");

    EXPECT_FALSE(ParsePhysicsDebugSetQuery({}).valid);
    EXPECT_FALSE(ParsePhysicsDebugSetQuery({ { "entity", "A" } }).valid);
    EXPECT_FALSE(ParsePhysicsDebugSetQuery({ { "entity", "" }, { "gravity_y", "1" } }).valid);

    const auto duplicate = ParsePhysicsDebugSetQuery({ { "gravity_y", "1" }, { "gravity_y", "2" } });
    EXPECT_FALSE(duplicate.valid);
    EXPECT_EQ(duplicate.errorKey, "gravity_y");
}

TEST(PhysicsDebugPresetQueryTests, OnlyKnownPresets)
{
    EXPECT_TRUE(ParsePhysicsDebugPresetQuery({ { "name", "default" } }).valid);
    EXPECT_EQ(ParsePhysicsDebugPresetQuery({ { "name", "legacy" } }).name, "legacy");
    EXPECT_FALSE(ParsePhysicsDebugPresetQuery({ { "name", "Default" } }).valid);
    EXPECT_FALSE(ParsePhysicsDebugPresetQuery({}).valid);
    EXPECT_FALSE(ParsePhysicsDebugPresetQuery({ { "name", "legacy" }, { "x", "1" } }).valid);
}

TEST(PhysicsDebugSeekQueryTests, ParsesFrameAndFlag)
{
    const auto minimal = ParsePhysicsDebugSeekQuery({ { "entity", "A" }, { "frame", "12.5" } });
    ASSERT_TRUE(minimal.valid);
    EXPECT_FLOAT_EQ(minimal.frame, 12.5f);
    EXPECT_TRUE(minimal.resetPhysics);

    const auto full = ParsePhysicsDebugSeekQuery({ { "entity", "A" }, { "frame", "0" }, { "reset_physics", "false" } });
    ASSERT_TRUE(full.valid);
    EXPECT_FALSE(full.resetPhysics);

    EXPECT_FALSE(ParsePhysicsDebugSeekQuery({ { "entity", "A" } }).valid);
    EXPECT_FALSE(ParsePhysicsDebugSeekQuery({ { "entity", "A" }, { "frame", "12abc" } }).valid);
    EXPECT_FALSE(ParsePhysicsDebugSeekQuery({ { "entity", "A" }, { "frame", "+1" } }).valid);
    EXPECT_FALSE(ParsePhysicsDebugSeekQuery({ { "entity", "A" }, { "frame", "100001" } }).valid);
    EXPECT_FALSE(ParsePhysicsDebugSeekQuery({ { "entity", "A" }, { "frame", "1" }, { "reset_physics", "yes" } }).valid);
    EXPECT_FALSE(ParsePhysicsDebugSeekQuery({ { "entity", "A" }, { "frame", "1" }, { "frame", "2" } }).valid);
}

TEST(PhysicsDebugCaptureQueryTests, ParsesEnabledAndWindow)
{
    const auto minimal = ParsePhysicsDebugCaptureQuery({ { "enabled", "true" } });
    ASSERT_TRUE(minimal.valid);
    EXPECT_TRUE(minimal.enabled);
    EXPECT_EQ(minimal.windowFrames, 120u);

    const auto full = ParsePhysicsDebugCaptureQuery({ { "enabled", "false" }, { "window", "600" }, { "entity", "A" } });
    ASSERT_TRUE(full.valid);
    EXPECT_EQ(full.windowFrames, 600u);
    EXPECT_EQ(full.entity, "A");

    EXPECT_FALSE(ParsePhysicsDebugCaptureQuery({}).valid);
    EXPECT_FALSE(ParsePhysicsDebugCaptureQuery({ { "enabled", "1" } }).valid);
    EXPECT_FALSE(ParsePhysicsDebugCaptureQuery({ { "enabled", "true" }, { "window", "0" } }).valid);
    EXPECT_FALSE(ParsePhysicsDebugCaptureQuery({ { "enabled", "true" }, { "window", "601" } }).valid);
    EXPECT_FALSE(ParsePhysicsDebugCaptureQuery({ { "enabled", "true" }, { "window", "12abc" } }).valid);
    EXPECT_FALSE(ParsePhysicsDebugCaptureQuery({ { "enabled", "true" }, { "bogus", "1" } }).valid);
}

TEST(PhysicsDebugMetricsQueryTests, ParsesWindowAndGroup)
{
    const auto minimal = ParsePhysicsDebugMetricsQuery({ { "entity", "A" } });
    ASSERT_TRUE(minimal.valid);
    EXPECT_EQ(minimal.windowFrames, 0u);

    const auto full = ParsePhysicsDebugMetricsQuery({ { "entity", "A" }, { "window", "30" }, { "group", "Q" } });
    ASSERT_TRUE(full.valid);
    EXPECT_EQ(full.windowFrames, 30u);
    EXPECT_EQ(full.group, "Q");

    EXPECT_FALSE(ParsePhysicsDebugMetricsQuery({}).valid);
    EXPECT_FALSE(ParsePhysicsDebugMetricsQuery({ { "entity", "A" }, { "window", "-1" } }).valid);
    EXPECT_FALSE(ParsePhysicsDebugMetricsQuery({ { "entity", "A" }, { "x", "1" } }).valid);
}

TEST(PhysicsDebugJointsQueryTests, ParsesPaging)
{
    const auto minimal = ParsePhysicsDebugJointsQuery({ { "entity", "A" } });
    ASSERT_TRUE(minimal.valid);
    EXPECT_EQ(minimal.chain, -1);
    EXPECT_EQ(minimal.first, 0u);
    EXPECT_EQ(minimal.count, 200u);

    const auto full = ParsePhysicsDebugJointsQuery(
        { { "entity", "A" }, { "chain", "2" }, { "group", "HB1" }, { "first", "10" }, { "count", "2000" } });
    ASSERT_TRUE(full.valid);
    EXPECT_EQ(full.chain, 2);
    EXPECT_EQ(full.group, "HB1");
    EXPECT_EQ(full.first, 10u);
    EXPECT_EQ(full.count, 2000u);

    EXPECT_FALSE(ParsePhysicsDebugJointsQuery({ { "entity", "A" }, { "count", "0" } }).valid);
    EXPECT_FALSE(ParsePhysicsDebugJointsQuery({ { "entity", "A" }, { "count", "2001" } }).valid);
    EXPECT_FALSE(ParsePhysicsDebugJointsQuery({ { "entity", "A" }, { "chain", "x" } }).valid);
    EXPECT_FALSE(ParsePhysicsDebugJointsQuery({ { "entity", "A" }, { "first", "1.5" } }).valid);
    EXPECT_FALSE(ParsePhysicsDebugJointsQuery({ { "entity", "A" }, { "count", "1" }, { "count", "2" } }).valid);
}

TEST(PhysicsDebugImageQueryTests, DefaultsAndLimits)
{
    const auto minimal = ParsePhysicsDebugImageQuery({ { "entity", "A" } });
    ASSERT_TRUE(minimal.valid);
    EXPECT_EQ(minimal.request.view, "negz");
    EXPECT_EQ(minimal.request.mode, "both");
    EXPECT_EQ(minimal.request.width, 900u);
    EXPECT_EQ(minimal.request.height, 900u);
    EXPECT_TRUE(minimal.request.showColliders);
    EXPECT_TRUE(minimal.request.showJoints);
    EXPECT_FALSE(minimal.request.showLimits);

    const auto full = ParsePhysicsDebugImageQuery({ { "entity", "A" }, { "view", "top" }, { "mode", "sim" },
        { "width", "200" }, { "height", "2000" }, { "colliders", "false" }, { "joints", "false" }, { "limits", "true" },
        { "highlight", "Q" }, { "format", "json" } });
    ASSERT_TRUE(full.valid);
    EXPECT_EQ(full.request.view, "top");
    EXPECT_EQ(full.request.mode, "sim");
    EXPECT_EQ(full.request.width, 200u);
    EXPECT_EQ(full.request.height, 2000u);
    EXPECT_FALSE(full.request.showColliders);
    EXPECT_FALSE(full.request.showJoints);
    EXPECT_TRUE(full.request.showLimits);
    EXPECT_EQ(full.request.highlight, "Q");

    EXPECT_FALSE(ParsePhysicsDebugImageQuery({}).valid);
    EXPECT_FALSE(ParsePhysicsDebugImageQuery({ { "entity", "A" }, { "view", "left" } }).valid);
    EXPECT_FALSE(ParsePhysicsDebugImageQuery({ { "entity", "A" }, { "mode", "x" } }).valid);
    EXPECT_FALSE(ParsePhysicsDebugImageQuery({ { "entity", "A" }, { "width", "199" } }).valid);
    EXPECT_FALSE(ParsePhysicsDebugImageQuery({ { "entity", "A" }, { "height", "2001" } }).valid);
    EXPECT_FALSE(ParsePhysicsDebugImageQuery({ { "entity", "A" }, { "width", "12abc" } }).valid);
    EXPECT_FALSE(ParsePhysicsDebugImageQuery({ { "entity", "A" }, { "limits", "1" } }).valid);
    EXPECT_FALSE(ParsePhysicsDebugImageQuery({ { "entity", "A" }, { "bogus", "1" } }).valid);
    EXPECT_FALSE(ParsePhysicsDebugImageQuery({ { "entity", "A" }, { "view", "top" }, { "view", "negz" } }).valid);
}

// ---- JSON body parsers ----------------------------------------------------

TEST(PhysicsDebugPlayRequestTests, ParsesFields)
{
    const auto minimal = ParsePhysicsDebugPlayRequest(R"({"entity":"A","animation_gta_path":"C:\\x.gta"})");
    ASSERT_TRUE(minimal.valid);
    EXPECT_EQ(minimal.animationGtaPath, "C:\\x.gta");
    EXPECT_FALSE(minimal.hasFrame);
    EXPECT_FALSE(minimal.hasSpeed);
    EXPECT_FALSE(minimal.hasLoop);

    const auto full = ParsePhysicsDebugPlayRequest(
        R"({"entity":"A","animation_gta_path":"x","frame":30,"speed":0,"loop":false})");
    ASSERT_TRUE(full.valid);
    EXPECT_TRUE(full.hasFrame);
    EXPECT_FLOAT_EQ(full.frame, 30.0f);
    EXPECT_TRUE(full.hasSpeed);
    EXPECT_FLOAT_EQ(full.speed, 0.0f);
    EXPECT_TRUE(full.hasLoop);
    EXPECT_FALSE(full.loop);

    EXPECT_FALSE(ParsePhysicsDebugPlayRequest("").valid);
    EXPECT_FALSE(ParsePhysicsDebugPlayRequest("[]").valid);
    EXPECT_FALSE(ParsePhysicsDebugPlayRequest(R"({"animation_gta_path":"x"})").valid);
    EXPECT_FALSE(ParsePhysicsDebugPlayRequest(R"({"entity":"A"})").valid);
    EXPECT_FALSE(ParsePhysicsDebugPlayRequest(R"({"entity":"A","animation_gta_path":"x","frame":"3"})").valid);
    EXPECT_FALSE(ParsePhysicsDebugPlayRequest(R"({"entity":"A","animation_gta_path":"x","frame":-1})").valid);
    EXPECT_FALSE(ParsePhysicsDebugPlayRequest(R"({"entity":"A","animation_gta_path":"x","loop":1})").valid);
    EXPECT_FALSE(ParsePhysicsDebugPlayRequest(R"({"entity":"A","animation_gta_path":"x","bogus":1})").valid);
}

TEST(PhysicsDebugRunRequestTests, ParsesMinimalAndFull)
{
    const auto minimal = ParsePhysicsDebugRunRequest(R"({"entity":"A","frames":204,"render_hz":60})");
    ASSERT_TRUE(minimal.valid);
    EXPECT_EQ(minimal.run.frames, 204u);
    EXPECT_FLOAT_EQ(minimal.run.renderHz, 60.0f);
    EXPECT_TRUE(minimal.run.restore);
    EXPECT_EQ(minimal.run.traceEvery, 1u);

    const auto full = ParsePhysicsDebugRunRequest(R"({"entity":"A","frames":204,"render_hz":144.5,"start_frame":10,
        "warmup_frames":20,"restore":false,
        "settings":{"gravity_y":-98,"bend_limits_enabled":false,"damping_mode":"raw_pmx"},
        "trace":{"bones":["HB1_7_4"],"every":2},"snapshot_frames":[100,0]})");
    ASSERT_TRUE(full.valid) << full.errorMessage;
    EXPECT_FLOAT_EQ(full.run.renderHz, 144.5f);
    EXPECT_FLOAT_EQ(full.run.startFrame, 10.0f);
    EXPECT_EQ(full.run.warmupFrames, 20u);
    EXPECT_FALSE(full.run.restore);
    ASSERT_EQ(full.run.settingsPairs.size(), 3u);
    bool sawGravity = false;
    bool sawBool = false;
    bool sawText = false;
    for (const auto& [key, value] : full.run.settingsPairs) {
        sawGravity = sawGravity || (key == "gravity_y" && value == "-98");
        sawBool = sawBool || (key == "bend_limits_enabled" && value == "false");
        sawText = sawText || (key == "damping_mode" && value == "raw_pmx");
    }
    EXPECT_TRUE(sawGravity);
    EXPECT_TRUE(sawBool);
    EXPECT_TRUE(sawText);
    ASSERT_EQ(full.run.traceBones.size(), 1u);
    EXPECT_EQ(full.run.traceBones[0], "HB1_7_4");
    EXPECT_EQ(full.run.traceEvery, 2u);
    ASSERT_EQ(full.run.snapshotFrames.size(), 2u);
    EXPECT_EQ(full.run.snapshotFrames[0], 100u);
}

TEST(PhysicsDebugRunRequestTests, RejectsBadValues)
{
    EXPECT_FALSE(ParsePhysicsDebugRunRequest("").valid);
    EXPECT_FALSE(ParsePhysicsDebugRunRequest(R"({"frames":10,"render_hz":60})").valid);
    EXPECT_FALSE(ParsePhysicsDebugRunRequest(R"({"entity":"A","render_hz":60})").valid);
    EXPECT_FALSE(ParsePhysicsDebugRunRequest(R"({"entity":"A","frames":10})").valid);
    EXPECT_FALSE(ParsePhysicsDebugRunRequest(R"({"entity":"A","frames":0,"render_hz":60})").valid);
    EXPECT_FALSE(ParsePhysicsDebugRunRequest(R"({"entity":"A","frames":3601,"render_hz":60})").valid);
    EXPECT_FALSE(ParsePhysicsDebugRunRequest(R"({"entity":"A","frames":10.5,"render_hz":60})").valid);
    EXPECT_FALSE(ParsePhysicsDebugRunRequest(R"({"entity":"A","frames":10,"render_hz":9})").valid);
    EXPECT_FALSE(ParsePhysicsDebugRunRequest(R"({"entity":"A","frames":10,"render_hz":481})").valid);
    EXPECT_FALSE(ParsePhysicsDebugRunRequest(R"({"entity":"A","frames":10,"render_hz":60,"start_frame":100001})").valid);
    EXPECT_FALSE(ParsePhysicsDebugRunRequest(R"({"entity":"A","frames":10,"render_hz":60,"warmup_frames":10})").valid);
    EXPECT_FALSE(ParsePhysicsDebugRunRequest(R"({"entity":"A","frames":10,"render_hz":60,"restore":"yes"})").valid);
    EXPECT_FALSE(ParsePhysicsDebugRunRequest(R"({"entity":"A","frames":10,"render_hz":60,"settings":[]})").valid);
    EXPECT_FALSE(ParsePhysicsDebugRunRequest(R"({"entity":"A","frames":10,"render_hz":60,"settings":{"a":null}})").valid);
    EXPECT_FALSE(ParsePhysicsDebugRunRequest(R"({"entity":"A","frames":10,"render_hz":60,"trace":{"every":0}})").valid);
    EXPECT_FALSE(ParsePhysicsDebugRunRequest(R"({"entity":"A","frames":10,"render_hz":60,"trace":{"bones":[1]}})").valid);
    EXPECT_FALSE(ParsePhysicsDebugRunRequest(R"({"entity":"A","frames":10,"render_hz":60,"trace":{"x":1}})").valid);
    EXPECT_FALSE(ParsePhysicsDebugRunRequest(R"({"entity":"A","frames":10,"render_hz":60,"snapshot_frames":[10]})").valid);
    EXPECT_FALSE(ParsePhysicsDebugRunRequest(R"({"entity":"A","frames":10,"render_hz":60,"bogus":1})").valid);
}

// ---- Builders -------------------------------------------------------------

constexpr const char* kTrickyName = "Fu\"ri\\na";

bool HasKeys(const nlohmann::json& object, std::initializer_list<const char*> keys)
{
    for (const char* key : keys) {
        if (!object.contains(key)) {
            return false;
        }
    }
    return true;
}

PhysicsAnimatorReport MakeAnimator()
{
    PhysicsAnimatorReport a;
    a.present = true;
    a.playing = true;
    a.loop = true;
    a.speed = 1.0f;
    a.frame = 12.0f;
    a.lastFrame = 105;
    a.animationGtaPath = "C:\\clip \"x\".gta";
    return a;
}

TEST(PhysicsDebugBuilderTests, ListHasEveryKeyAndEscapesNames)
{
    PhysicsDebugListReport report;
    PhysicsDebugListEntry entry;
    entry.name = kTrickyName;
    entry.entityIndex = 3;
    entry.entityGeneration = 2;
    entry.meshGtaPath = "C:\\m.gta";
    entry.chainCount = 3;
    entry.jointCount = 260;
    entry.animator = MakeAnimator();
    report.entries.push_back(entry);

    const nlohmann::json body = nlohmann::json::parse(BuildPhysicsDebugListResponseJson(report));
    ASSERT_TRUE(body["entities"].is_array());
    const nlohmann::json& e = body["entities"][0];
    EXPECT_TRUE(HasKeys(e, { "name", "entity", "mesh_gta_path", "chains", "joints", "rig_enabled", "rig_frozen", "animator" }));
    EXPECT_TRUE(HasKeys(e["entity"], { "index", "generation" }));
    EXPECT_TRUE(HasKeys(e["animator"], { "present", "playing", "loop", "speed", "frame", "last_frame", "animation_gta_path" }));
    EXPECT_EQ(e["name"].get<std::string>(), kTrickyName);
    EXPECT_EQ(e["animator"]["animation_gta_path"].get<std::string>(), "C:\\clip \"x\".gta");
}

TEST(PhysicsDebugBuilderTests, StateHasEveryKey)
{
    PhysicsDebugStateReport report;
    report.entityName = kTrickyName;
    report.settings = { { "gravity_y", "-98" }, { "bend_limits_enabled", "true" }, { "damping_mode", "mapped" },
        { "bend_joint_floor_degrees", "3" } };
    report.chains.push_back({});
    report.groups.push_back({});

    const nlohmann::json body = nlohmann::json::parse(BuildPhysicsDebugStateResponseJson(report));
    EXPECT_TRUE(HasKeys(body, { "success", "entity", "settings", "time", "chains", "groups", "colliders", "detection", "capture" }));
    EXPECT_TRUE(HasKeys(body["time"], { "paused", "frozen_this_frame", "frame_count" }));
    EXPECT_TRUE(HasKeys(body["chains"][0], { "index", "anchor_bone", "anchor_bone_index", "joints", "root_children",
        "extra_constraints", "constraint_iterations", "collision_enabled", "max_plausible_root_delta", "initialized", "reseed_count" }));
    EXPECT_TRUE(HasKeys(body["groups"][0], { "key", "chain", "joints" }));
    EXPECT_TRUE(HasKeys(body["detection"], { "orphans", "cross_chain_joints_dropped", "duplicate_bodies_dropped", "bend_limit_missing" }));
    EXPECT_TRUE(HasKeys(body["capture"], { "enabled", "window_frames", "frames_recorded" }));
    EXPECT_EQ(body["entity"].get<std::string>(), kTrickyName);
    EXPECT_TRUE(body["settings"]["bend_limits_enabled"].is_boolean());
    EXPECT_TRUE(body["settings"]["gravity_y"].is_number());
    EXPECT_TRUE(body["settings"]["damping_mode"].is_string());
}

TEST(PhysicsDebugBuilderTests, SettingsPlayCaptureAndSuccess)
{
    const nlohmann::json settings = nlohmann::json::parse(
        BuildPhysicsDebugSettingsResponseJson({ { "fixed_step_hz", "120" }, { "bend_joint_floor_degrees", "3" } }));
    EXPECT_TRUE(settings["success"].get<bool>());
    EXPECT_EQ(settings["settings"]["fixed_step_hz"].get<int>(), 120);
    EXPECT_TRUE(settings["settings"].contains("bend_joint_floor_degrees"));
    EXPECT_TRUE(settings["settings"]["bend_joint_floor_degrees"].is_number());

    const nlohmann::json play = nlohmann::json::parse(BuildPhysicsDebugPlayResponseJson(kTrickyName, MakeAnimator()));
    EXPECT_TRUE(HasKeys(play, { "success", "entity", "animator" }));
    EXPECT_EQ(play["entity"].get<std::string>(), kTrickyName);

    PhysicsCaptureStatus status;
    status.enabled = true;
    status.windowFrames = 120;
    status.framesRecorded = 7;
    const nlohmann::json capture = nlohmann::json::parse(BuildPhysicsDebugCaptureResponseJson(status));
    EXPECT_TRUE(HasKeys(capture, { "success", "enabled", "window_frames", "frames_recorded" }));

    EXPECT_TRUE(nlohmann::json::parse(BuildPhysicsDebugSuccessResponseJson())["success"].get<bool>());
}

PhysicsMetricsReport MakeMetrics()
{
    PhysicsMetricsReport m;
    m.frames = 204;
    m.renderHz = 60.0f;
    GroupMetricsReport group;
    group.key = kTrickyName;
    group.jointCount = 5;
    group.segDirErrDeg.count = 4;
    group.segDirErrDeg.p95 = 1.5;
    m.groups.push_back(group);
    m.all.key = "ALL";
    m.all.chainIndex = -1;
    m.stepsHistogram = { 1, 2, 3, 4 };
    return m;
}

void ExpectMetricKeys(const nlohmann::json& body)
{
    EXPECT_TRUE(HasKeys(body, { "success", "entity", "frames", "render_hz", "steps_histogram", "groups", "all",
        "welded_root_children", "collision", "bend", "reseed_count", "nan_count" }));
    EXPECT_EQ(body["steps_histogram"].size(), 4u);
    EXPECT_TRUE(HasKeys(body["groups"][0], { "key", "chain", "joints", "dist_from_animated", "seg_dir_err_deg",
        "bend_delta_deg", "limit_violation_deg", "abs_stretch", "writeback_err", "accel_local" }));
    EXPECT_TRUE(HasKeys(body["groups"][0]["seg_dir_err_deg"], { "n", "mean", "p50", "p95", "p99", "max" }));
    EXPECT_TRUE(HasKeys(body["all"], { "key", "chain", "joints", "accel_local" }));
    EXPECT_TRUE(HasKeys(body["collision"], { "pushed_particles_per_frame", "mean_push_distance",
        "bind_pose_penetrating_joints", "bind_pose_pushed_joints" }));
    EXPECT_TRUE(HasKeys(body["bend"], { "clamped_joints_per_frame" }));
    EXPECT_EQ(body["groups"][0]["key"].get<std::string>(), kTrickyName);
}

TEST(PhysicsDebugBuilderTests, RunHasEveryKey)
{
    PhysicsRunReport report;
    report.metrics = MakeMetrics();
    report.settingsUsed = { { "gravity_y", "-98" } };
    report.wrapped = false;
    PhysicsTraceSeries series;
    series.bone = "HB1_7_4";
    series.display.push_back({ 1.0f, 2.0f, 3.0f });
    series.animated.push_back({ 1.0f, 2.0f, 3.0f });
    series.fk.push_back({ 1.0f, 2.0f, 3.0f });
    report.trace.push_back(series);
    PhysicsRunSnapshot snapshot;
    snapshot.frame = 100;
    snapshot.joints.push_back({});
    report.snapshots.push_back(snapshot);

    const nlohmann::json body = nlohmann::json::parse(BuildPhysicsDebugRunResponseJson(kTrickyName, report));
    ExpectMetricKeys(body);
    EXPECT_TRUE(HasKeys(body, { "settings_used", "wrapped", "physics_ms", "trace", "snapshots" }));
    EXPECT_TRUE(HasKeys(body["physics_ms"], { "median", "p95" }));
    EXPECT_TRUE(HasKeys(body["trace"][0], { "bone", "display", "animated", "fk" }));
    EXPECT_TRUE(HasKeys(body["snapshots"][0], { "frame", "joints" }));
    EXPECT_TRUE(HasKeys(body["snapshots"][0]["joints"][0], { "bone", "animated", "display", "fk" }));
    EXPECT_TRUE(body["welded_root_children"].is_null());
}

TEST(PhysicsDebugBuilderTests, WeldedAggregateAppearsWhenFilled)
{
    PhysicsMetricsReport metrics = MakeMetrics();
    metrics.weldedRootChildren.key = "WELDED_ROOT_CHILDREN";
    metrics.weldedRootChildren.jointCount = 15;

    const nlohmann::json body = nlohmann::json::parse(BuildPhysicsDebugMetricsResponseJson(kTrickyName, metrics));
    ExpectMetricKeys(body);
    ASSERT_TRUE(body["welded_root_children"].is_object());
    EXPECT_TRUE(HasKeys(body["welded_root_children"], { "joints", "dist_from_animated", "seg_dir_err_deg" }));
    EXPECT_FALSE(body.contains("trace"));
    EXPECT_FALSE(body.contains("snapshots"));
}

TEST(PhysicsDebugBuilderTests, NonFiniteNumbersBecomeNull)
{
    PhysicsMetricsReport metrics = MakeMetrics();
    metrics.all.accelLocal.mean = std::numeric_limits<double>::quiet_NaN();
    const nlohmann::json body = nlohmann::json::parse(BuildPhysicsDebugMetricsResponseJson("A", metrics));
    EXPECT_TRUE(body["all"]["accel_local"]["mean"].is_null());
}

TEST(PhysicsDebugBuilderTests, JointsHaveEveryKey)
{
    PhysicsJointsReport report;
    report.total = 204;
    report.first = 0;
    PhysicsJointRow row;
    row.bone = kTrickyName;
    row.collisionMask = 0xffbf;
    row.ignoredColliders = { "a\\b", "c\"d" };
    row.bendLimitDeg = 0.0f;
    row.ownLimitWithFloorDeg = 3.0f;
    report.rows.push_back(row);

    const nlohmann::json body = nlohmann::json::parse(BuildPhysicsDebugJointsResponseJson(report));
    EXPECT_TRUE(HasKeys(body, { "success", "total", "first", "rows" }));
    const nlohmann::json& r = body["rows"][0];
    EXPECT_TRUE(HasKeys(r, { "i", "chain", "bone", "bone_index", "group", "parent_joint", "parent_bone", "rest_length",
        "rigid_body", "mass", "damping", "pmx_linear_damping", "stiffness", "collision_radius", "collision_fit_radius",
        "ignored_colliders", "group_bit", "collision_mask", "bend_limit_deg", "own_limit_with_floor_deg",
        "cumulative_bend_limit_deg", "effective_bend_limit_deg", "pmx_joint", "pmx_rot_lower_deg", "pmx_rot_upper_deg",
        "pmx_joint_found", "live" }));
    EXPECT_FLOAT_EQ(r["bend_limit_deg"].get<float>(), 0.0f);
    EXPECT_FLOAT_EQ(r["own_limit_with_floor_deg"].get<float>(), 3.0f);
    EXPECT_TRUE(HasKeys(r["live"], { "valid", "animated", "display", "particle", "fk", "dist_from_animated", "seg_dir_err_deg" }));
    EXPECT_EQ(r["collision_mask"].get<std::string>(), "0xffbf");
    EXPECT_EQ(r["ignored_colliders"][1].get<std::string>(), "c\"d");
    EXPECT_EQ(r["bone"].get<std::string>(), kTrickyName);
    EXPECT_EQ(r["pmx_rot_lower_deg"].size(), 3u);
}

TEST(PhysicsDebugBuilderTests, CollidersHaveEveryKey)
{
    PhysicsCollidersReport report;
    PhysicsColliderRow row;
    row.rigidBodyName = kTrickyName;
    row.shape = "sphere";
    row.mask = 0x86e0;
    report.rows.push_back(row);
    PhysicsBindPosePenetration penetration;
    penetration.bone = "b";
    penetration.collider = "c";
    penetration.push = 0.1f;
    report.bindPosePenetrations.push_back(penetration);

    const nlohmann::json body = nlohmann::json::parse(BuildPhysicsDebugCollidersResponseJson(report));
    EXPECT_TRUE(HasKeys(body, { "success", "colliders", "bind_pose_penetrations" }));
    EXPECT_TRUE(HasKeys(body["colliders"][0], { "i", "rigid_body", "bone", "shape", "size", "world_center",
        "world_rotation", "group", "mask" }));
    EXPECT_EQ(body["colliders"][0]["world_rotation"].size(), 4u);
    EXPECT_EQ(body["colliders"][0]["mask"].get<std::string>(), "0x86e0");
    EXPECT_TRUE(HasKeys(body["bind_pose_penetrations"][0], { "chain", "joint", "bone", "collider", "push" }));
}

TEST(PhysicsDebugBuilderTests, ImageJsonBodyHasViewAndLegend)
{
    PhysicsDebugImageViewInfo view;
    view.name = "negz";
    view.pixelsPerUnit = 12.5f;
    const nlohmann::json body = nlohmann::json::parse(BuildPhysicsDebugImageJsonBody(900, 800, "QUJD", view, { kTrickyName }));
    EXPECT_TRUE(HasKeys(body, { "width", "height", "format", "data_base64", "view", "legend", "highlighted_bones" }));
    EXPECT_EQ(body["format"].get<std::string>(), "png");
    EXPECT_EQ(body["data_base64"].get<std::string>(), "QUJD");
    EXPECT_TRUE(HasKeys(body["view"], { "name", "right", "up", "forward", "bounds_min", "bounds_max", "pixels_per_unit" }));
    EXPECT_TRUE(HasKeys(body["legend"], { "anim", "sim", "collider", "limit", "highlight", "axes" }));
    EXPECT_EQ(body["highlighted_bones"][0].get<std::string>(), kTrickyName);
}

// ---- Route level ----------------------------------------------------------

TEST(PhysicsDebugRoutesNoBridgeTests, NullBridgeAnswers503)
{
    Network::NetworkServer server; // Every bridge pointer defaults to nullptr.
    server.Start(0);
    ASSERT_TRUE(server.IsRunning());

    httplib::Client client("127.0.0.1", server.BoundPort());
    Network::TestHelpers::WaitUntilAcceptingConnections(server, client);

    const httplib::Result list = client.Get("/physics_debug/list");
    ASSERT_TRUE(list != nullptr);
    EXPECT_EQ(list->status, 503);

    const httplib::Result state = client.Get("/physics_debug/state?entity=A");
    ASSERT_TRUE(state != nullptr);
    EXPECT_EQ(state->status, 503);

    const httplib::Result run =
        client.Post("/physics_debug/run", R"({"entity":"A","frames":10,"render_hz":60})", "application/json");
    ASSERT_TRUE(run != nullptr);
    EXPECT_EQ(run->status, 503);

    const httplib::Result bad = client.Get("/physics_debug/state?entity=A&bogus=1");
    ASSERT_TRUE(bad != nullptr);
    EXPECT_EQ(bad->status, 400);
}

} // namespace
