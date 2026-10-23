#include "Editor/ClusterViewerSettings.h"

#include <gtest/gtest.h>

#include <cmath>
#include <vector>

namespace gte {
namespace {

using Pairs = std::vector<ClusterViewerKeyValue>;

ClusterViewerSetResult Apply(const Pairs& pairs, ClusterViewerSettings& s, DebugCameraState& c)
{
    return ParseClusterViewerSet(pairs, s, c);
}

TEST(ClusterViewerSettingsTest, ValidKeysAreApplied)
{
    ClusterViewerSettings s;
    DebugCameraState c;
    EXPECT_FALSE(s.showPlanes);
    const auto r = Apply({ { "drawMode", "cull" }, { "frustum", "false" }, { "cone", "1" }, { "culledStyle", "hidden" },
                             { "debugCamera", "true" }, { "planes", "true" }, { "camX", "5" }, { "camFov", "90" },
                             { "encodeWidth", "1920" } },
        s, c);
    ASSERT_TRUE(r.Ok()) << r.error;
    EXPECT_EQ(s.drawMode, ClusterViewerDrawMode::Cull);
    EXPECT_FALSE(s.frustum);
    EXPECT_TRUE(s.cone);
    EXPECT_EQ(s.culledStyle, ClusterViewerCulledStyle::Hidden);
    EXPECT_TRUE(c.show);
    EXPECT_TRUE(s.showPlanes);
    EXPECT_FLOAT_EQ(c.transform.position.x, 5.0f);
    EXPECT_FLOAT_EQ(c.fovYDegrees, 90.0f);
    EXPECT_EQ(s.encodeWidth, 1920u);
}

TEST(ClusterViewerSettingsTest, EulerKeysBuildTheRotation)
{
    ClusterViewerSettings s;
    DebugCameraState c;
    ASSERT_TRUE(Apply({ { "camYaw", "45" } }, s, c).Ok());
    const Vec3 euler = c.transform.rotation.ToEulerDegrees();
    EXPECT_NEAR(euler.x, 0.0f, 1e-3f);
    EXPECT_NEAR(euler.y, 45.0f, 1e-3f);
    EXPECT_NEAR(euler.z, 0.0f, 1e-3f);
}

TEST(ClusterViewerSettingsTest, OutOfRangeValueIsRejectedWithKeyAndRange)
{
    ClusterViewerSettings s;
    DebugCameraState c;
    const auto r = Apply({ { "camFov", "171" } }, s, c);
    EXPECT_EQ(r.status, ClusterViewerSetStatus::BadValue);
    EXPECT_NE(r.error.find("camFov"), std::string::npos);
    EXPECT_NE(r.error.find("1..170"), std::string::npos);
    EXPECT_FLOAT_EQ(c.fovYDegrees, 60.0f);

    EXPECT_EQ(Apply({ { "encodeWidth", "800" } }, s, c).status, ClusterViewerSetStatus::BadValue);
    EXPECT_EQ(Apply({ { "camX", "nan" } }, s, c).status, ClusterViewerSetStatus::BadValue);
    EXPECT_EQ(Apply({ { "frustum", "maybe" } }, s, c).status, ClusterViewerSetStatus::BadValue);
    EXPECT_EQ(Apply({ { "orbitPitch", "100" } }, s, c).status, ClusterViewerSetStatus::BadValue);
}

TEST(ClusterViewerSettingsTest, UnknownKeyListsTheValidKeys)
{
    ClusterViewerSettings s;
    DebugCameraState c;
    const auto r = Apply({ { "bogus", "1" } }, s, c);
    EXPECT_EQ(r.status, ClusterViewerSetStatus::UnknownKey);
    EXPECT_NE(r.error.find("'bogus'"), std::string::npos);
    EXPECT_NE(r.error.find("drawMode"), std::string::npos);
}

TEST(ClusterViewerSettingsTest, OneBadPairAppliesNothing)
{
    ClusterViewerSettings s;
    DebugCameraState c;
    const auto r = Apply({ { "drawMode", "cull" }, { "camX", "7" }, { "camFov", "0" } }, s, c);
    EXPECT_FALSE(r.Ok());
    EXPECT_EQ(s.drawMode, ClusterViewerDrawMode::Normal);
    EXPECT_FLOAT_EQ(c.transform.position.x, 0.0f);
}

TEST(ClusterViewerSettingsTest, NearAboveFarIsRejectedAsAWhole)
{
    ClusterViewerSettings s;
    DebugCameraState c; // far 2000
    const auto bad = Apply({ { "camNear", "3000" } }, s, c);
    EXPECT_EQ(bad.status, ClusterViewerSetStatus::BadValue);
    EXPECT_NE(bad.error.find("camFar"), std::string::npos);
    EXPECT_FLOAT_EQ(c.nearZ, 1.0f);

    EXPECT_TRUE(Apply({ { "camNear", "3000" }, { "camFar", "9000" } }, s, c).Ok());
    EXPECT_FLOAT_EQ(c.nearZ, 3000.0f);
}

TEST(ClusterViewerSettingsTest, OrbitAndCopyRequestsAreReturnedNotApplied)
{
    ClusterViewerSettings s;
    DebugCameraState c;
    const auto r = Apply({ { "orbitYaw", "10" }, { "orbitDistance", "250" }, { "copySceneCamera", "true" } }, s, c);
    ASSERT_TRUE(r.Ok());
    EXPECT_TRUE(r.copySceneCamera);
    ASSERT_TRUE(r.orbit.yawDeg.has_value());
    EXPECT_FLOAT_EQ(*r.orbit.yawDeg, 10.0f);
    EXPECT_FLOAT_EQ(*r.orbit.distance, 250.0f);
    EXPECT_FALSE(r.orbit.pitchDeg.has_value());
}

TEST(ClusterViewerSettingsTest, DefaultCameraIsValidForAnyBounds)
{
    for (const float radius : { 0.001f, 1.0f, 5000.0f, 1.0e6f, std::nanf(""), -3.0f }) {
        const DebugCameraState cam = DefaultDebugCamera(Vec3::Zero(), radius);
        std::string why;
        EXPECT_TRUE(DebugCameraIsValid(cam, &why)) << radius << ": " << why;
        EXPECT_FALSE(cam.show);
    }
}

TEST(ClusterViewerSettingsTest, ClampRepairsBadCameraValues)
{
    DebugCameraState bad;
    bad.fovYDegrees = std::nanf("");
    bad.nearZ = -5.0f;
    bad.farZ = 0.0f;
    bad.aspect = 1000.0f;
    bad.transform.position = Vec3(1.0e12f, std::nanf(""), 0.0f);
    EXPECT_FALSE(DebugCameraIsValid(bad));
    EXPECT_TRUE(DebugCameraIsValid(ClampDebugCamera(bad)));
}

TEST(ClusterViewerSettingsTest, LookRotationPointsForwardAtTheTarget)
{
    const Vec3 ahead = ClusterViewerLookRotation(Vec3::Zero(), Vec3(0, 0, 10)).RotateVector(Vec3::Forward());
    EXPECT_TRUE(ApproximatelyEqual(ahead, Vec3(0, 0, 1), 1e-4f));
    const Vec3 side = ClusterViewerLookRotation(Vec3::Zero(), Vec3(10, 0, 0)).RotateVector(Vec3::Forward());
    EXPECT_TRUE(ApproximatelyEqual(side, Vec3(1, 0, 0), 1e-4f));
}

TEST(ClusterViewerSettingsTest, ViewProjectionOfValidCameraIsFinite)
{
    const Mat4 vp = DebugCameraViewProjection(DefaultDebugCamera(Vec3::Zero(), 10.0f));
    for (int i = 0; i < 16; ++i) {
        EXPECT_TRUE(std::isfinite(vp.Data()[i]));
    }
}

} // namespace
} // namespace gte
