// Tests for the Furina showcase layout: framing of the hat and hips, camera orientation and sun side. No device.

#include "Editor/FurinaShowcaseLayout.h"
#include "Math/Quat.h"

#include <gtest/gtest.h>

#include <cmath>
#include <limits>

namespace gte {
namespace {

// Screen row of a world point, from the top edge (0) to the bottom edge (1), through the layout's camera.
double ScreenRowFromTop(const FurinaShowcaseLayout& layout, double y)
{
    const Quat rotation = Quat::FromEulerDegrees(layout.cameraPitchEulerDegrees, 0.0f, 0.0f);
    const Vec3 local = rotation.Conjugate().RotateVector(Vec3{ 0.0f, static_cast<float>(y), 0.0f } - layout.cameraPosition);
    const double tanHalfFov = std::tan(static_cast<double>(layout.fovYDegrees) * 3.14159265358979323846 / 360.0);
    const double ndcY = (static_cast<double>(local.y) / local.z) / tanHalfFov;
    return 0.5 - 0.5 * ndcY;
}

FurinaShowcaseModel Model(float minY, float maxY, float hipY)
{
    FurinaShowcaseModel model;
    model.minY = minY;
    model.maxY = maxY;
    model.hipY = hipY;
    return model;
}

TEST(FurinaShowcaseLayoutTest, HatTopAndHipsLandOnTheirScreenRows)
{
    for (const float scale : { 1.0f, 12.5f, 0.01f }) {
        const FurinaShowcaseLayout layout = ComputeFurinaShowcaseLayout(Model(0.0f, 1.7f * scale, 0.85f * scale));
        ASSERT_TRUE(layout.valid);
        EXPECT_NEAR(ScreenRowFromTop(layout, 1.7 * scale), kFurinaShowcaseHatTopFromTop, 1e-3) << scale;
        EXPECT_NEAR(ScreenRowFromTop(layout, 0.85 * scale), kFurinaShowcaseHipsFromTop, 1e-3) << scale;
    }
}

TEST(FurinaShowcaseLayoutTest, TheCameraIsInFrontOfTheModelAndLooksUp)
{
    const FurinaShowcaseLayout layout = ComputeFurinaShowcaseLayout(Model(0.0f, 20.0f, 10.0f));
    ASSERT_TRUE(layout.valid);
    EXPECT_LT(layout.cameraPosition.z, 0.0f);
    EXPECT_FLOAT_EQ(layout.cameraPosition.x, 0.0f);
    EXPECT_FLOAT_EQ(layout.cameraPosition.z, -layout.cameraDistance);
    EXPECT_LT(layout.cameraPitchEulerDegrees, 0.0f);

    const Vec3 forward = Quat::FromEulerDegrees(layout.cameraPitchEulerDegrees, 0.0f, 0.0f).RotateVector(Vec3::Forward());
    EXPECT_GT(forward.y, 0.0f);
    EXPECT_GT(forward.z, 0.0f);
}

TEST(FurinaShowcaseLayoutTest, TheCameraStaysBelowTheHatAndTheClipPlanesBracketTheModel)
{
    const FurinaShowcaseLayout layout = ComputeFurinaShowcaseLayout(Model(0.0f, 20.0f, 10.0f));
    ASSERT_TRUE(layout.valid);
    EXPECT_LT(layout.cameraPosition.y, 20.0f);
    EXPECT_GT(layout.cameraPosition.y, 0.0f);
    EXPECT_LT(layout.nearZ, layout.cameraDistance - 10.0f);
    EXPECT_GT(layout.farZ, layout.cameraDistance + 10.0f);
}

TEST(FurinaShowcaseLayoutTest, TheSunIsHighInFrontOfTheModelOnTheCameraLeft)
{
    const FurinaShowcaseLayout layout = ComputeFurinaShowcaseLayout(Model(0.0f, 20.0f, 10.0f));
    ASSERT_TRUE(layout.valid);
    const Vec3 lightTravel = Quat::FromEulerDegrees(layout.sunPitchEulerDegrees, layout.sunYawEulerDegrees, 0.0f)
                                 .RotateVector(Vec3::Forward());
    const Vec3 towardSun = -lightTravel;
    EXPECT_NEAR(towardSun.y, std::sin(kFurinaShowcaseSunElevationDegrees * 3.14159265f / 180.0f), 1e-4f);
    EXPECT_LT(towardSun.x, 0.0f); // Camera left is -X.
    EXPECT_LT(towardSun.z, 0.0f); // Between the model and the camera.
    EXPECT_FLOAT_EQ(layout.sunColor.x, 1.0f);
    EXPECT_FLOAT_EQ(layout.sunColor.y, 0.96f);
    EXPECT_FLOAT_EQ(layout.sunColor.z, 0.90f);
}

TEST(FurinaShowcaseLayoutTest, HipHeightFallsBackWhenTheBoneIsMissingOrOutsideTheBounds)
{
    EXPECT_FLOAT_EQ(ResolveFurinaHipHeight(0.0f, 20.0f, 9.0f), 9.0f);
    EXPECT_FLOAT_EQ(ResolveFurinaHipHeight(0.0f, 20.0f, 0.0f), 20.0f * kFurinaShowcaseFallbackHipFraction);
    EXPECT_FLOAT_EQ(ResolveFurinaHipHeight(0.0f, 20.0f, 25.0f), 20.0f * kFurinaShowcaseFallbackHipFraction);
    EXPECT_FLOAT_EQ(ResolveFurinaHipHeight(2.0f, 22.0f, std::numeric_limits<float>::quiet_NaN()),
        2.0f + 20.0f * kFurinaShowcaseFallbackHipFraction);
}

TEST(FurinaShowcaseLayoutTest, EmptyOrNonFiniteBoundsAreInvalid)
{
    EXPECT_FALSE(ComputeFurinaShowcaseLayout(Model(0.0f, 0.0f, 0.0f)).valid);
    EXPECT_FALSE(ComputeFurinaShowcaseLayout(Model(5.0f, 1.0f, 3.0f)).valid);
    EXPECT_FALSE(ComputeFurinaShowcaseLayout(Model(0.0f, std::numeric_limits<float>::infinity(), 1.0f)).valid);
    EXPECT_FALSE(ComputeFurinaShowcaseLayout(Model(std::numeric_limits<float>::quiet_NaN(), 1.0f, 0.5f)).valid);
}

} // namespace
} // namespace gte
