// Tier 1: scripted flight path, camera rotation convention and the control helpers.

#include "Features/Terrain/TerrainControl.h"
#include "Features/Terrain/TerrainFlight.h"

#include <gtest/gtest.h>

#include <cmath>

namespace gte::terrain {

namespace {

double Hills(double x, double z)
{
    return 300.0 + 250.0 * std::sin(x * 0.002) * std::cos(z * 0.0015);
}

} // namespace

TEST(TerrainFlightTests, TwoRunsWithTheSameFixedStepGiveIdenticalPoses)
{
    FlightSpec spec;
    spec.fixedStepSeconds = 1.0 / 60.0;
    FlightController a;
    FlightController b;
    a.Start(spec);
    b.Start(spec);
    for (int i = 0; i < 600; ++i) {
        // The real delta is ignored while a fixed step is set.
        const FlightPose pa = a.Advance(0.001 * (i % 7), Hills);
        const FlightPose pb = b.Advance(0.2, Hills);
        ASSERT_EQ(pa.x, pb.x);
        ASSERT_EQ(pa.y, pb.y);
        ASSERT_EQ(pa.z, pb.z);
        ASSERT_EQ(pa.yawRadians, pb.yawRadians);
    }
    EXPECT_NEAR(a.Elapsed(), 10.0, 1e-9);
}

TEST(TerrainFlightTests, OrbitStaysOnItsCircleAtTheRequestedSpeed)
{
    FlightSpec spec;
    spec.kind = FlightKind::Orbit;
    spec.centerX = 1000.0;
    spec.centerZ = -2000.0;
    spec.radius = 2500.0;
    spec.speed = 200.0;
    spec.fixedStepSeconds = 0.05;
    FlightController c;
    c.Start(spec);
    double lastX = 0.0;
    double lastZ = 0.0;
    for (int i = 0; i < 400; ++i) {
        const FlightPose p = c.Advance(0.0, Hills);
        EXPECT_NEAR(std::hypot(p.x - spec.centerX, p.z - spec.centerZ), 2500.0, 1e-6);
        if (i > 0) {
            EXPECT_NEAR(std::hypot(p.x - lastX, p.z - lastZ) / 0.05, 200.0, 0.5);
        }
        lastX = p.x;
        lastZ = p.z;
    }
}

TEST(TerrainFlightTests, LineMovesAlongItsHeadingAndTheYawFollowsIt)
{
    FlightSpec spec;
    spec.kind = FlightKind::Line;
    spec.centerX = 0.0;
    spec.centerZ = 0.0;
    spec.headingDegrees = 90.0; // Along +X.
    spec.speed = 100.0;
    spec.fixedStepSeconds = 0.1;
    FlightController c;
    c.Start(spec);
    FlightPose p;
    for (int i = 0; i < 100; ++i) {
        p = c.Advance(0.0, Hills);
    }
    EXPECT_NEAR(p.x, 1000.0, 1e-6);
    EXPECT_NEAR(p.z, 0.0, 1e-6);
    EXPECT_NEAR(p.yawRadians, 3.14159265f * 0.5f, 1e-5f);
}

TEST(TerrainFlightTests, CameraClearsTheGroundAndTheMinimumAltitude)
{
    FlightSpec spec;
    spec.altitudeAboveGround = 120.0;
    spec.minAltitude = 500.0;
    spec.fixedStepSeconds = 1.0 / 30.0;
    FlightController c;
    c.Start(spec);
    for (int i = 0; i < 900; ++i) {
        const FlightPose p = c.Advance(0.0, Hills);
        ASSERT_GE(p.y, 500.0 - 1e-9);
        if (i > 120) {
            // The height filter lags a little behind the ground, but must not dive into it.
            ASSERT_GT(p.y, Hills(p.x, p.z) + 40.0);
        }
    }
}

TEST(TerrainFlightTests, FlightFinishesAfterItsDurationAndStaysStopped)
{
    FlightSpec spec;
    spec.durationSeconds = 2.0;
    spec.fixedStepSeconds = 0.5;
    FlightController c;
    c.Start(spec);
    EXPECT_FALSE(c.Advance(0.0, Hills).finished);
    EXPECT_FALSE(c.Advance(0.0, Hills).finished);
    EXPECT_FALSE(c.Advance(0.0, Hills).finished);
    const FlightPose last = c.Advance(0.0, Hills);
    EXPECT_TRUE(last.finished);
    EXPECT_FALSE(c.Active());
    const FlightPose again = c.Advance(1.0, Hills);
    EXPECT_EQ(again.x, last.x);
    EXPECT_EQ(again.z, last.z);
}

TEST(TerrainFlightTests, SanitizeRejectsNonsense)
{
    FlightSpec spec;
    spec.speed = -5.0;
    spec.radius = std::nan("");
    spec.altitudeAboveGround = 1.0e12;
    spec.fixedStepSeconds = 50.0;
    const FlightSpec s = SanitizeFlight(spec);
    EXPECT_GE(s.speed, 1.0);
    EXPECT_TRUE(std::isfinite(s.radius));
    EXPECT_LE(s.altitudeAboveGround, 20000.0);
    EXPECT_LE(s.fixedStepSeconds, 1.0);
}

TEST(TerrainFlightTests, StoppedControllerDoesNotMove)
{
    FlightController c;
    EXPECT_FALSE(c.Active());
    const FlightPose p = c.Advance(1.0, Hills);
    EXPECT_EQ(p.x, 0.0);
    EXPECT_EQ(c.Elapsed(), 0.0);
}

TEST(TerrainFlightTests, RotationLooksAlongTheHeadingAndTiltsDown)
{
    const float yaws[] = { 0.0f, 0.7f, 1.5707963f, 3.0f, -2.0f };
    for (const float yaw : yaws) {
        for (const float pitch : { 0.0f, 0.2f, 0.6f }) {
            FlightPose pose;
            pose.yawRadians = yaw;
            pose.pitchDownRadians = pitch;
            const Vec3 f = FlightRotation(pose).RotateVector(Vec3::Forward());
            EXPECT_NEAR(f.x, std::cos(pitch) * std::sin(yaw), 1e-5f) << "yaw " << yaw << " pitch " << pitch;
            EXPECT_NEAR(f.y, -std::sin(pitch), 1e-5f) << "pitch " << pitch;
            EXPECT_NEAR(f.z, std::cos(pitch) * std::cos(yaw), 1e-5f);
        }
    }
}

TEST(TerrainControlTests, ProfileNamesRoundTrip)
{
    for (const TerrainProfile p : { TerrainProfile::Full, TerrainProfile::Lean, TerrainProfile::GeometryOnly }) {
        TerrainProfile parsed = TerrainProfile::Full;
        ASSERT_TRUE(TryParseTerrainProfile(TerrainProfileName(p), parsed));
        EXPECT_EQ(parsed, p);
    }
    TerrainProfile untouched = TerrainProfile::Lean;
    EXPECT_FALSE(TryParseTerrainProfile("ultra", untouched));
    EXPECT_EQ(untouched, TerrainProfile::Lean);
}

TEST(TerrainControlTests, PoolSlotsLeaveRoomForThePrefetchRing)
{
    TerrainRuntimeOptions options;
    options.maxVisibleNodes = 400;
    EXPECT_GE(TerrainPoolSlotsFor(options), 600u);
    options.maxVisibleNodes = 0;
    EXPECT_GE(TerrainPoolSlotsFor(options), 512u);
}

TEST(TerrainControlTests, StatsJsonIsFlatEscapedAndBalanced)
{
    TerrainStats stats;
    stats.open = true;
    stats.nodesInView = 42;
    stats.generationMsAverage = 1.5;
    stats.tickCpuMs = std::nan("");
    stats.gpuCullMode = "HiZ";
    stats.lastError = "bad \"quote\"\nline\\slash";
    const std::string json = TerrainStatsToJson(stats);

    ASSERT_FALSE(json.empty());
    EXPECT_EQ(json.front(), '{');
    EXPECT_EQ(json.back(), '}');
    EXPECT_NE(json.find("\"open\":true"), std::string::npos);
    EXPECT_NE(json.find("\"nodesInView\":42"), std::string::npos);
    EXPECT_NE(json.find("\"generationMsAverage\":1.5000"), std::string::npos);
    EXPECT_NE(json.find("\"tickCpuMs\":null"), std::string::npos) << "a NaN must not leak into the JSON";
    EXPECT_NE(json.find("\"gpuCullMode\":\"HiZ\""), std::string::npos);
    EXPECT_NE(json.find("bad \\\"quote\\\"\\nline\\\\slash"), std::string::npos);
    EXPECT_EQ(json.find('\n'), std::string::npos);

    int depth = 0;
    bool inString = false;
    for (std::size_t i = 0; i < json.size(); ++i) {
        const char c = json[i];
        if (inString) {
            if (c == '\\') { ++i; }
            else if (c == '"') { inString = false; }
        } else if (c == '"') {
            inString = true;
        } else if (c == '{') {
            ++depth;
        } else if (c == '}') {
            --depth;
        }
    }
    EXPECT_EQ(depth, 0);
    EXPECT_FALSE(inString);
}

} // namespace gte::terrain
