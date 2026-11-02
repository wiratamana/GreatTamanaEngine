// Tier 1 tests for EditorCamera: pan, look, zoom, pivot, projection and SetPose. No ImGui, SDL or Vulkan.

#include "Editor/EditorCamera.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>

namespace gte {
namespace {

const float kHalfFov = DegToRad(EditorCamera::kFovYDegrees) * 0.5f;
constexpr float kAspect = 16.0f / 9.0f;

EditorCameraInput Frame(
    float dx = 0.0f, float dy = 0.0f, float wheel = 0.0f, bool pan = false, bool look = false, float dt = 0.016f)
{
    EditorCameraInput input;
    input.mouseDelta = Vec2{ dx, dy };
    input.scrollNotches = wheel;
    input.deltaSeconds = dt;
    input.viewportHeightPixels = 720.0f;
    input.panning = pan;
    input.looking = look;
    return input;
}

void ExpectPivotOnViewAxis(const EditorCamera& camera)
{
    const Vec4 viewSpace = camera.View() * Vec4(camera.Pivot(), 1.0f);
    EXPECT_TRUE(ApproximatelyEqual(viewSpace.XYZ(), Vec3{ 0.0f, 0.0f, camera.Distance() }, 1.0e-3f));
}

void ExpectSamePose(const EditorCamera& a, const EditorCamera& b)
{
    EXPECT_TRUE(ApproximatelyEqual(a.GetTransform().position, b.GetTransform().position));
    EXPECT_TRUE(ApproximatelyEqual(a.YawDegrees(), b.YawDegrees()));
    EXPECT_TRUE(ApproximatelyEqual(a.PitchDegrees(), b.PitchDegrees()));
    EXPECT_TRUE(ApproximatelyEqual(a.Distance(), b.Distance()));
}

TEST(EditorCameraTest, DefaultsLookAlongPlusZFromMinusFiveWithThePivotAtTheOrigin)
{
    const EditorCamera camera;

    EXPECT_TRUE(ApproximatelyEqual(camera.GetTransform().position, Vec3{ 0.0f, 0.0f, -5.0f }));
    EXPECT_TRUE(RepresentSameRotation(camera.GetTransform().rotation, Quat::Identity()));
    EXPECT_TRUE(ApproximatelyEqual(camera.YawDegrees(), 0.0f));
    EXPECT_TRUE(ApproximatelyEqual(camera.PitchDegrees(), 0.0f));
    EXPECT_TRUE(ApproximatelyEqual(camera.Distance(), 5.0f));
    EXPECT_TRUE(ApproximatelyEqual(camera.Pivot(), Vec3{ 0.0f, 0.0f, 0.0f }));
    EXPECT_EQ(camera.ProjectionMode(), CameraProjectionMode::Perspective);
}

TEST(EditorCameraTest, UpdateWithNoInputDoesNotMoveTheCamera)
{
    EditorCamera camera;
    const EditorCamera before = camera;

    camera.Update(Frame(25.0f, 25.0f));

    ExpectSamePose(camera, before);
}

TEST(EditorCameraTest, MiddleDragPansByTheViewportScale)
{
    EditorCamera camera;
    const Vec3 start = camera.GetTransform().position;
    const float pp = 2.0f * 5.0f * std::tan(kHalfFov) / 720.0f;
    const Vec3 expected = start + camera.Right() * (-10.0f * pp) + camera.Up() * (10.0f * pp);

    camera.Update(Frame(10.0f, 10.0f, 0.0f, true));

    EXPECT_TRUE(ApproximatelyEqual(camera.GetTransform().position, expected, 1.0e-4f));
    EXPECT_TRUE(ApproximatelyEqual(camera.YawDegrees(), 0.0f));
    EXPECT_TRUE(ApproximatelyEqual(camera.PitchDegrees(), 0.0f));
}

TEST(EditorCameraTest, PanSpeedScalesWithTheDistance)
{
    EditorCamera near5;
    const Vec3 near5Start = near5.GetTransform().position;
    near5.Update(Frame(10.0f, 10.0f, 0.0f, true));

    EditorCamera far10;
    ASSERT_TRUE(far10.SetDistance(10.0f));
    const Vec3 far10Start = far10.GetTransform().position;
    far10.Update(Frame(10.0f, 10.0f, 0.0f, true));

    const float shortMove = Length(near5.GetTransform().position - near5Start);
    const float longMove = Length(far10.GetTransform().position - far10Start);
    EXPECT_TRUE(ApproximatelyEqual(longMove, 2.0f * shortMove, 1.0e-4f));
}

// One notch applied in a single long frame: the glide finishes at once.
void ZoomNow(EditorCamera& camera, float wheel)
{
    camera.Update(Frame(0.0f, 0.0f, wheel, false, false, 1.0f));
}

// Idle frames until every pending zoom is applied. Takes longer than the streak gap, so a streak ends.
void Settle(EditorCamera& camera)
{
    for (int i = 0; i < 60; ++i) {
        camera.Update(Frame());
    }
}

TEST(EditorCameraTest, OneWheelNotchClosesTenPercentAndKeepsThePivot)
{
    EditorCamera camera;

    ZoomNow(camera, 1.0f);

    EXPECT_NEAR(camera.Distance(), 4.5f, 1.0e-4f);
    EXPECT_TRUE(ApproximatelyEqual(camera.GetTransform().position, Vec3{ 0.0f, 0.0f, -4.5f }, 1.0e-4f));
    EXPECT_TRUE(ApproximatelyEqual(camera.Pivot(), Vec3{ 0.0f, 0.0f, 0.0f }, 1.0e-4f));
}

TEST(EditorCameraTest, WheelOutMirrorsIt)
{
    EditorCamera camera;

    ZoomNow(camera, -1.0f);

    EXPECT_NEAR(camera.Distance(), 5.0f / 0.9f, 1.0e-4f);
    EXPECT_TRUE(ApproximatelyEqual(camera.Pivot(), Vec3{ 0.0f, 0.0f, 0.0f }, 1.0e-4f));
}

TEST(EditorCameraTest, ANotchGlidesOverSeveralFramesAndEndsAtTheSameDistance)
{
    EditorCamera camera;

    camera.Update(Frame(0.0f, 0.0f, 1.0f));
    const float afterFirstFrame = camera.Distance();
    EXPECT_LT(afterFirstFrame, 5.0f);
    EXPECT_GT(afterFirstFrame, 4.5f);

    camera.Update(Frame());
    EXPECT_LT(camera.Distance(), afterFirstFrame);

    Settle(camera);
    EXPECT_NEAR(camera.Distance(), 4.5f, 1.0e-3f);
    EXPECT_TRUE(ApproximatelyEqual(camera.Pivot(), Vec3{ 0.0f, 0.0f, 0.0f }, 1.0e-3f));
}

TEST(EditorCameraTest, TheGlideDoesNotDependOnTheFrameRate)
{
    EditorCamera slowFrames;
    EditorCamera fastFrames;
    slowFrames.Update(Frame(0.0f, 0.0f, 1.0f, false, false, 0.033f));
    fastFrames.Update(Frame(0.0f, 0.0f, 1.0f, false, false, 0.0165f));
    fastFrames.Update(Frame(0.0f, 0.0f, 0.0f, false, false, 0.0165f));

    EXPECT_NEAR(slowFrames.Distance(), fastFrames.Distance(), 2.0e-2f);
}

TEST(EditorCameraTest, RapidNotchesAccelerateALittle)
{
    EditorCamera fast;
    for (int i = 0; i < 3; ++i) {
        fast.Update(Frame(0.0f, 0.0f, 1.0f, false, false, 0.016f));
    }
    Settle(fast);
    EXPECT_NEAR(fast.Distance(), 5.0f * std::pow(0.9f, 1.0f + 1.05f + 1.1f), 1.0e-3f);

    EditorCamera slow;
    for (int i = 0; i < 3; ++i) {
        slow.Update(Frame(0.0f, 0.0f, 1.0f, false, false, 0.016f));
        Settle(slow);
    }
    EXPECT_NEAR(slow.Distance(), 5.0f * std::pow(0.9f, 3.0f), 1.0e-3f);
    EXPECT_LT(fast.Distance(), slow.Distance());
}

TEST(EditorCameraTest, TheStreakGainIsCapped)
{
    EditorCamera camera;
    ASSERT_TRUE(camera.SetDistance(100000.0f));
    float totalNotches = 0.0f;
    for (int i = 0; i < 40; ++i) {
        totalNotches += std::min(1.0f + 0.05f * static_cast<float>(i), 2.0f);
        camera.Update(Frame(0.0f, 0.0f, 1.0f, false, false, 0.016f));
    }
    Settle(camera);

    const float expected = 100000.0f * std::pow(0.9f, totalNotches);
    EXPECT_NEAR(camera.Distance() / expected, 1.0f, 1.0e-2f);
}

TEST(EditorCameraTest, ABurstInOneFrameIsLimited)
{
    EditorCamera burst;
    ZoomNow(burst, 50.0f);

    EditorCamera limited;
    ZoomNow(limited, 3.0f);

    EXPECT_NEAR(burst.Distance(), limited.Distance(), 1.0e-4f);
}

TEST(EditorCameraTest, AShortPauseKeepsTheStreakAndALongPauseResetsIt)
{
    EditorCamera shortPause;
    shortPause.Update(Frame(0.0f, 0.0f, 1.0f));
    shortPause.Update(Frame(0.0f, 0.0f, 0.0f, false, false, 0.1f));
    shortPause.Update(Frame(0.0f, 0.0f, 1.0f));
    Settle(shortPause);
    EXPECT_NEAR(shortPause.Distance(), 5.0f * std::pow(0.9f, 1.0f + 1.05f), 2.0e-3f);

    EditorCamera longPause;
    longPause.Update(Frame(0.0f, 0.0f, 1.0f));
    longPause.Update(Frame(0.0f, 0.0f, 0.0f, false, false, 0.5f));
    longPause.Update(Frame(0.0f, 0.0f, 1.0f));
    Settle(longPause);
    EXPECT_NEAR(longPause.Distance(), 4.05f, 1.0e-3f);
}

TEST(EditorCameraTest, ALongStreakKeepsTheStateFinite)
{
    EditorCamera camera;
    for (int i = 0; i < 500; ++i) {
        camera.Update(Frame(0.0f, 0.0f, (i % 2 == 0) ? 1.0f : -1.0f));
    }

    const Vec3 p = camera.GetTransform().position;
    for (int axis = 0; axis < 3; ++axis) {
        EXPECT_TRUE(std::isfinite(p[axis]));
        EXPECT_LE(std::fabs(p[axis]), EditorCamera::kMaxPoseUnits);
    }
    EXPECT_TRUE(std::isfinite(camera.Distance()));
    EXPECT_GE(camera.Distance(), EditorCamera::kMinDistance);
    EXPECT_LE(camera.Distance(), EditorCamera::kMaxDistance);
}

TEST(EditorCameraTest, TheEyeStillFliesForwardAtTheMinimumDistance)
{
    EditorCamera camera;
    ASSERT_TRUE(camera.SetDistance(EditorCamera::kMinDistance));
    const Vec3 start = camera.GetTransform().position;

    ZoomNow(camera, 1.0f);

    EXPECT_TRUE(ApproximatelyEqual(camera.GetTransform().position, start + camera.Forward() * 0.1f, 1.0e-4f));
    EXPECT_NEAR(camera.Distance(), EditorCamera::kMinDistance, 1.0e-6f);
}

TEST(EditorCameraTest, DistanceAndPositionAreClamped)
{
    EditorCamera camera;
    for (int i = 0; i < 400; ++i) {
        camera.Update(Frame(0.0f, 0.0f, -5.0f));
    }

    EXPECT_LE(camera.Distance(), EditorCamera::kMaxDistance);
    const Vec3 p = camera.GetTransform().position;
    for (int axis = 0; axis < 3; ++axis) {
        EXPECT_LE(std::fabs(p[axis]), EditorCamera::kMaxPoseUnits);
    }
}

TEST(EditorCameraTest, LookInPerspectiveTurnsAroundTheEye)
{
    EditorCamera camera;
    const Vec3 start = camera.GetTransform().position;
    const Vec3 pivot = camera.Pivot();

    camera.Update(Frame(10.0f, 10.0f, 0.0f, false, true));

    EXPECT_TRUE(ApproximatelyEqual(camera.YawDegrees(), 2.0f));
    EXPECT_TRUE(ApproximatelyEqual(camera.PitchDegrees(), 2.0f));
    EXPECT_TRUE(ApproximatelyEqual(camera.GetTransform().position, start));
    EXPECT_FALSE(ApproximatelyEqual(camera.Pivot(), pivot));
}

TEST(EditorCameraTest, LookInOrthographicOrbitsThePivot)
{
    EditorCamera camera;
    camera.SetProjectionMode(CameraProjectionMode::Orthographic);
    const Vec3 pivot = camera.Pivot();

    camera.Update(Frame(100.0f, 0.0f, 0.0f, false, true));

    EXPECT_TRUE(ApproximatelyEqual(camera.YawDegrees(), 20.0f));
    EXPECT_TRUE(ApproximatelyEqual(camera.Pivot(), pivot, 1.0e-4f));
    EXPECT_TRUE(ApproximatelyEqual(camera.Distance(), 5.0f));
    EXPECT_TRUE(ApproximatelyEqual(camera.GetTransform().position, pivot - camera.Forward() * 5.0f, 1.0e-4f));
}

TEST(EditorCameraTest, PitchIsClampedAtNinetyAndTheTopViewIsValid)
{
    EditorCamera camera;
    for (int i = 0; i < 50; ++i) {
        camera.Update(Frame(0.0f, 50.0f, 0.0f, false, true));
    }
    EXPECT_EQ(camera.PitchDegrees(), 90.0f);

    const Mat4 view = camera.View();
    for (int r = 0; r < 4; ++r) {
        for (int c = 0; c < 4; ++c) {
            EXPECT_TRUE(std::isfinite(view(r, c)));
        }
    }
    ExpectPivotOnViewAxis(camera);

    for (int i = 0; i < 50; ++i) {
        camera.Update(Frame(0.0f, -50.0f, 0.0f, false, true));
    }
    EXPECT_EQ(camera.PitchDegrees(), -90.0f);
}

TEST(EditorCameraTest, ThePivotAlwaysMapsToTheViewAxis)
{
    EditorCamera a;
    a.Update(Frame(30.0f, 10.0f, 0.0f, false, true));
    a.Update(Frame(5.0f, -5.0f, 0.0f, true));
    a.Update(Frame(0.0f, 0.0f, 2.0f));
    ExpectPivotOnViewAxis(a);

    EditorCamera b;
    b.SetProjectionMode(CameraProjectionMode::Orthographic);
    b.Update(Frame(-40.0f, 20.0f, 0.0f, false, true));
    b.Update(Frame(8.0f, 3.0f, -1.0f, true));
    b.AlignTo(SceneViewAxis::PositiveY);
    ExpectPivotOnViewAxis(b);

    EditorCamera c;
    ASSERT_TRUE(c.SetPose(Vec3{ 1.0f, 2.0f, 3.0f }, 45.0f, 30.0f));
    ASSERT_TRUE(c.SetDistance(12.0f));
    c.AlignTo(SceneViewAxis::NegativeX);
    for (int i = 0; i < 3; ++i) {
        c.Update(Frame(0.0f, 0.0f, 1.0f));
    }
    c.SetProjectionMode(CameraProjectionMode::Perspective);
    ExpectPivotOnViewAxis(c);
}

TEST(EditorCameraTest, AlignToTurnsAroundThePivot)
{
    struct Case {
        SceneViewAxis axis;
        Vec3 position;
    };
    const std::array<Case, kSceneViewAxisCount> cases = { {
        { SceneViewAxis::PositiveY, Vec3{ 0.0f, 5.0f, 0.0f } },
        { SceneViewAxis::NegativeY, Vec3{ 0.0f, -5.0f, 0.0f } },
        { SceneViewAxis::PositiveX, Vec3{ 5.0f, 0.0f, 0.0f } },
        { SceneViewAxis::NegativeX, Vec3{ -5.0f, 0.0f, 0.0f } },
        { SceneViewAxis::NegativeZ, Vec3{ 0.0f, 0.0f, -5.0f } },
        { SceneViewAxis::PositiveZ, Vec3{ 0.0f, 0.0f, 5.0f } },
    } };

    for (const Case& test : cases) {
        EditorCamera camera;
        camera.AlignTo(test.axis);

        EXPECT_TRUE(ApproximatelyEqual(camera.Pivot(), Vec3{ 0.0f, 0.0f, 0.0f }, 1.0e-4f)) << SceneViewAxisName(test.axis);
        EXPECT_TRUE(ApproximatelyEqual(camera.Distance(), 5.0f)) << SceneViewAxisName(test.axis);
        EXPECT_TRUE(ApproximatelyEqual(camera.Forward(), SceneViewLookDirection(test.axis), 1.0e-4f))
            << SceneViewAxisName(test.axis);
        EXPECT_TRUE(ApproximatelyEqual(camera.GetTransform().position, test.position, 1.0e-4f))
            << SceneViewAxisName(test.axis);
    }

    EditorCamera moved;
    moved.Update(Frame(12.0f, -7.0f, 0.0f, true));
    moved.Update(Frame(0.0f, 0.0f, 1.0f));
    const Vec3 pivot = moved.Pivot();
    moved.AlignTo(SceneViewAxis::NegativeY);
    EXPECT_TRUE(ApproximatelyEqual(moved.Pivot(), pivot, 1.0e-4f));
}

TEST(EditorCameraTest, ProjectionDoesNotChangeThePivotFraming)
{
    for (const CameraProjectionMode mode : { CameraProjectionMode::Perspective, CameraProjectionMode::Orthographic }) {
        EditorCamera camera;
        camera.Update(Frame(30.0f, 10.0f, 0.0f, false, true));
        camera.SetProjectionMode(mode);

        const Vec3 pivot = camera.Pivot();
        const float half = camera.HalfHeightAtPivot();
        const Mat4 viewProjection = camera.ViewProjection(kAspect);

        const Vec4 top = viewProjection * Vec4(pivot + camera.Up() * half, 1.0f);
        EXPECT_NEAR(top.y / top.w, -1.0f, 1.0e-3f);
        EXPECT_NEAR(top.x / top.w, 0.0f, 1.0e-3f);

        const Vec4 side = viewProjection * Vec4(pivot + camera.Right() * (half * kAspect), 1.0f);
        EXPECT_NEAR(side.x / side.w, 1.0f, 1.0e-3f);
    }
}

TEST(EditorCameraTest, OrthographicMatrixHasNoPerspectiveRow)
{
    EditorCamera camera;
    const Mat4 perspective = camera.Projection(kAspect);
    EXPECT_FLOAT_EQ(perspective(3, 2), 1.0f);
    EXPECT_FLOAT_EQ(perspective(3, 3), 0.0f);

    camera.SetProjectionMode(CameraProjectionMode::Orthographic);
    const Mat4 ortho = camera.Projection(kAspect);
    EXPECT_FLOAT_EQ(ortho(3, 0), 0.0f);
    EXPECT_FLOAT_EQ(ortho(3, 1), 0.0f);
    EXPECT_FLOAT_EQ(ortho(3, 2), 0.0f);
    EXPECT_FLOAT_EQ(ortho(3, 3), 1.0f);
}

TEST(EditorCameraTest, DefaultPerspectiveEqualsTheCameraComponentLens)
{
    const EditorCamera camera;
    const Camera lens{};

    EXPECT_TRUE(ApproximatelyEqual(
        camera.ViewProjection(kAspect), lens.ProjectionMatrix(kAspect) * Camera::ViewMatrix(camera.GetTransform())));
}

TEST(EditorCameraTest, ClipPlanesFollowTheDistance)
{
    EditorCamera camera;
    EXPECT_NEAR(camera.NearClip(), 0.1f, 1.0e-5f);
    EXPECT_NEAR(camera.FarClip(), 1000.0f, 1.0e-3f);

    ASSERT_TRUE(camera.SetDistance(1000.0f));
    EXPECT_NEAR(camera.NearClip(), 10.0f, 1.0e-4f);
    EXPECT_NEAR(camera.FarClip(), 100000.0f, 1.0e-1f);
}

TEST(EditorCameraTest, ToggleProjectionKeepsPoseAndDistance)
{
    EditorCamera camera;
    camera.Update(Frame(20.0f, 8.0f, 1.0f, false, true));
    const EditorCamera original = camera;
    const Mat4 originalProjection = camera.Projection(kAspect);

    camera.ToggleProjectionMode();
    EXPECT_TRUE(camera.IsOrthographic());
    ExpectSamePose(camera, original);

    camera.ToggleProjectionMode();
    EXPECT_FALSE(camera.IsOrthographic());
    EXPECT_TRUE(ApproximatelyEqual(camera.Projection(kAspect), originalProjection));
    EXPECT_TRUE(ApproximatelyEqual(camera.View(), original.View()));
}

TEST(EditorCameraTest, SetDistanceKeepsThePivotAndRejectsBadValues)
{
    EditorCamera camera;
    ASSERT_TRUE(camera.SetDistance(10.0f));
    EXPECT_TRUE(ApproximatelyEqual(camera.Pivot(), Vec3{ 0.0f, 0.0f, 0.0f }, 1.0e-4f));
    EXPECT_TRUE(ApproximatelyEqual(camera.GetTransform().position, Vec3{ 0.0f, 0.0f, -10.0f }, 1.0e-4f));

    const EditorCamera before = camera;
    EXPECT_FALSE(camera.SetDistance(std::numeric_limits<float>::quiet_NaN()));
    EXPECT_FALSE(camera.SetDistance(0.0f));
    EXPECT_FALSE(camera.SetDistance(0.01f));
    EXPECT_FALSE(camera.SetDistance(1.0e6f));
    ExpectSamePose(camera, before);
}

TEST(EditorCameraTest, NonFiniteInputIsIgnored)
{
    const float nan = std::numeric_limits<float>::quiet_NaN();
    const float inf = std::numeric_limits<float>::infinity();

    EditorCamera camera;
    const EditorCamera before = camera;

    camera.Update(Frame(nan, 0.0f, 0.0f, true, true));
    ExpectSamePose(camera, before);

    camera.Update(Frame(0.0f, 0.0f, inf));
    ExpectSamePose(camera, before);

    camera.Update(Frame(0.0f, 0.0f, 1.0f, false, false, nan));
    ExpectSamePose(camera, before);

    EditorCameraInput badHeight = Frame(10.0f, 10.0f, 0.0f, true);
    badHeight.viewportHeightPixels = nan;
    camera.Update(badHeight);
    ExpectSamePose(camera, before);
}

TEST(EditorCameraTest, OrthographicViewProjectionInvertsAtEveryDistanceAndAspect)
{
    constexpr float kAspects[] = { 1.0f, 16.0f / 9.0f, 719.0f / 447.0f, 3.0f };
    constexpr float kDistances[] = { EditorCamera::kMinDistance, 1.0f, 21.0f, 22.0f, 100.0f, 2000.0f, 100000.0f };

    for (const SceneViewAxis axis : { SceneViewAxis::PositiveY, SceneViewAxis::PositiveZ, SceneViewAxis::NegativeX }) {
        for (const float distance : kDistances) {
            EditorCamera camera;
            camera.SetProjectionMode(CameraProjectionMode::Orthographic);
            camera.AlignTo(axis);
            ASSERT_TRUE(camera.SetDistance(distance));

            for (const float aspect : kAspects) {
                const Mat4 viewProjection = camera.ViewProjection(aspect);
                Mat4 inverse;
                ASSERT_TRUE(viewProjection.TryInverse(inverse)) << "distance=" << distance << " aspect=" << aspect;
                // The depth range is -far..+far (far up to 1e7), so float rounding in the depth row is a few 1e-2.
                const Mat4 product = inverse * viewProjection;
                float maxError = 0.0f;
                for (int r = 0; r < 4; ++r) {
                    for (int c = 0; c < 4; ++c) {
                        maxError = std::max(maxError, std::fabs(product(r, c) - (r == c ? 1.0f : 0.0f)));
                    }
                }
                EXPECT_LT(maxError, 0.05f) << "distance=" << distance << " aspect=" << aspect;
            }
        }
    }
}

TEST(EditorCameraSetPoseTest, RoundTripsPositionYawAndPitch)
{
    EditorCamera camera;

    ASSERT_TRUE(camera.SetPose(Vec3{ 1.0f, 2.0f, 3.0f }, 30.0f, 10.0f));

    EXPECT_TRUE(ApproximatelyEqual(camera.GetTransform().position, Vec3{ 1.0f, 2.0f, 3.0f }));
    EXPECT_TRUE(ApproximatelyEqual(camera.YawDegrees(), 30.0f));
    EXPECT_TRUE(ApproximatelyEqual(camera.PitchDegrees(), 10.0f));
    EXPECT_TRUE(RepresentSameRotation(camera.GetTransform().rotation, Quat::FromEulerDegrees(10.0f, 30.0f, 0.0f)));
}

TEST(EditorCameraSetPoseTest, ClampsPitchAndWrapsYaw)
{
    EditorCamera camera;

    ASSERT_TRUE(camera.SetPose(Vec3{}, 0.0f, 100.0f));
    EXPECT_TRUE(ApproximatelyEqual(camera.PitchDegrees(), 90.0f));

    ASSERT_TRUE(camera.SetPose(Vec3{}, 0.0f, -100.0f));
    EXPECT_TRUE(ApproximatelyEqual(camera.PitchDegrees(), -90.0f));

    ASSERT_TRUE(camera.SetPose(Vec3{}, 370.0f, 0.0f));
    EXPECT_TRUE(ApproximatelyEqual(camera.YawDegrees(), 10.0f));

    ASSERT_TRUE(camera.SetPose(Vec3{}, -350.0f, 0.0f));
    EXPECT_TRUE(ApproximatelyEqual(camera.YawDegrees(), 10.0f));
}

TEST(EditorCameraSetPoseTest, RejectsBadInputAndKeepsThePose)
{
    EditorCamera camera;
    ASSERT_TRUE(camera.SetPose(Vec3{ 4.0f, 5.0f, 6.0f }, 20.0f, 15.0f));

    const float nan = std::numeric_limits<float>::quiet_NaN();
    const float inf = std::numeric_limits<float>::infinity();

    EXPECT_FALSE(camera.SetPose(Vec3{ nan, 0.0f, 0.0f }, 0.0f, 0.0f));
    EXPECT_FALSE(camera.SetPose(Vec3{ 0.0f, inf, 0.0f }, 0.0f, 0.0f));
    EXPECT_FALSE(camera.SetPose(Vec3{}, nan, 0.0f));
    EXPECT_FALSE(camera.SetPose(Vec3{}, 0.0f, -inf));
    EXPECT_FALSE(camera.SetPose(Vec3{ 2.0e6f, 0.0f, 0.0f }, 0.0f, 0.0f));
    EXPECT_FALSE(camera.SetPose(Vec3{}, 2.0e5f, 0.0f));

    EXPECT_TRUE(ApproximatelyEqual(camera.GetTransform().position, Vec3{ 4.0f, 5.0f, 6.0f }));
    EXPECT_TRUE(ApproximatelyEqual(camera.YawDegrees(), 20.0f));
    EXPECT_TRUE(ApproximatelyEqual(camera.PitchDegrees(), 15.0f));
}

TEST(EditorCameraSetPoseTest, SetPoseKeepsTheDistance)
{
    EditorCamera camera;
    ASSERT_TRUE(camera.SetDistance(8.0f));

    ASSERT_TRUE(camera.SetPose(Vec3{ 1.0f, 2.0f, 3.0f }, 30.0f, 10.0f));

    EXPECT_TRUE(ApproximatelyEqual(camera.Distance(), 8.0f));
    EXPECT_TRUE(ApproximatelyEqual(camera.Pivot(), camera.GetTransform().position + camera.Forward() * 8.0f));
}

TEST(EditorCameraSetPoseTest, RightDragContinuesFromTheNewYaw)
{
    EditorCamera camera;
    ASSERT_TRUE(camera.SetPose(Vec3{}, 40.0f, 0.0f));

    camera.Update(Frame(10.0f, 0.0f, 0.0f, false, true));

    EXPECT_TRUE(ApproximatelyEqual(camera.YawDegrees(), 42.0f));
}

TEST(EditorCameraSetPoseTest, MatchesAPoseReachedByMouseLook)
{
    EditorCamera dragged;
    dragged.Update(Frame(100.0f, 50.0f, 0.0f, false, true)); // yaw 20, pitch 10

    EditorCamera posed;
    ASSERT_TRUE(posed.SetPose(dragged.GetTransform().position, 20.0f, 10.0f));

    EXPECT_TRUE(ApproximatelyEqual(posed.ViewProjection(kAspect), dragged.ViewProjection(kAspect), 1.0e-4f));
}

} // namespace
} // namespace gte
