// A camera far from the world origin: the eye-relative double-precision path against the single-precision one.

#include "Core/FeatureKit/ScreenSpaceView.h"
#include "ECS/Components/Camera.h"
#include "Math/EyeRelativeCamera.h"
#include "Math/Mat4d.h"
#include "Math/Quat.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <vector>

namespace gte {
namespace {

constexpr float kNear = 0.1f;
constexpr float kFar = 1000.0f;

struct EngineCamera {
    Vec3 eye;
    Mat4 projection;
    Mat4 viewProjection; // What the scene is drawn with: P * V in float.
};

// Built exactly like RenderSystem::ResolveActiveCameraViewProjection.
EngineCamera MakeEngineCamera(const Vec3& eye, float pitchDegrees, float yawDegrees, Extent2D extent)
{
    Camera camera;
    camera.nearZ = kNear;
    camera.farZ = kFar;
    Transform transform;
    transform.position = eye;
    transform.rotation = Quat::FromEulerDegrees(pitchDegrees, yawDegrees, 0.0f);

    EngineCamera out;
    out.eye = eye;
    out.projection = camera.ProjectionMatrix(static_cast<float>(extent.width) / static_cast<float>(extent.height));
    out.viewProjection = out.projection * Camera::ViewMatrix(transform);
    return out;
}

struct Pose {
    float pitch;
    float yaw;
};

// Deterministic spread of orientations: pitch -60..60, yaw 0..360.
std::vector<Pose> MakePoses(int count)
{
    std::vector<Pose> poses;
    std::uint32_t state = 12345u;
    const auto next = [&state]() {
        state = state * 1664525u + 1013904223u;
        return static_cast<float>(state >> 8) / static_cast<float>(1u << 24);
    };
    for (int i = 0; i < count; ++i) {
        const float pitch = -60.0f + 120.0f * next();
        const float yaw = 360.0f * next();
        poses.push_back({ pitch, yaw });
    }
    return poses;
}

constexpr Extent2D kExtents[] = { Extent2D{ 884, 432 }, Extent2D{ 1280, 720 }, Extent2D{ 1600, 867 } };

// Fraction of poses whose camera the screen-space features reject.
struct Rejection {
    int tested = 0;
    int relative = 0;
    int absolute = 0;
};

Rejection CountRejections(const Vec3& eye)
{
    Rejection out;
    for (const Extent2D extent : kExtents) {
        for (const Pose& pose : MakePoses(120)) {
            const EngineCamera cam = MakeEngineCamera(eye, pose.pitch, pose.yaw, extent);
            ++out.tested;

            const EyeRelativeCamera relative =
                MakeEyeRelativeCameraFromMatrix(MakeEyeRelativeViewProjection(cam.viewProjection, cam.projection));
            if (!fx::DeriveCameraBasisRelative(eye, relative, extent).valid) {
                ++out.relative;
            }

            Mat4 inverse;
            if (!cam.viewProjection.TryInverse(inverse) || !fx::DeriveCameraBasis(eye, inverse, extent).valid) {
                ++out.absolute;
            }
        }
    }
    return out;
}

TEST(EyeRelativeCameraTest, FarCameraIsAcceptedEverywhereAndTheAbsolutePathIsNot)
{
    // 5.4 km from the origin: the absolute float path rejects many orientations, the relative one none.
    const Rejection near5km = CountRejections(Vec3(2000.0f, 2.0f, -5000.0f));
    EXPECT_EQ(near5km.relative, 0);
    // Positive control: without this the test could not see the bug it guards.
    EXPECT_GT(near5km.absolute, near5km.tested / 5);
}

TEST(EyeRelativeCameraTest, VeryFarCamerasStayAccepted)
{
    EXPECT_EQ(CountRejections(Vec3(20000.0f, 2.0f, -50000.0f)).relative, 0);
    EXPECT_EQ(CountRejections(Vec3(54000.0f, 40.0f, -120000.0f)).relative, 0);
}

TEST(EyeRelativeCameraTest, CameraAtTheOriginIsAcceptedByBothPaths)
{
    const Rejection origin = CountRejections(Vec3(0.0f, 2.0f, 0.0f));
    EXPECT_EQ(origin.relative, 0);
    EXPECT_EQ(origin.absolute, 0);
}

// Pixel distance between two NDC points.
float PixelDistance(const double (&a)[4], const double (&b)[4], Extent2D extent)
{
    const double dx = (a[0] / a[3] - b[0] / b[3]) * 0.5 * extent.width;
    const double dy = (a[1] / a[3] - b[1] / b[3]) * 0.5 * extent.height;
    return static_cast<float>(std::sqrt(dx * dx + dy * dy));
}

// Unprojects a pixel at a view depth through `inverse` in float, projects the result with the double `projector`
// and reports how many pixels it lands from where it started.
float RoundTripPixels(const Mat4& inverse, const Mat4d& projector, const Vec3& worldOffset, Extent2D extent,
    float ndcX, float ndcY, float viewZ)
{
    const double a = static_cast<double>(kFar) / (kFar - kNear);
    const double b = -static_cast<double>(kNear) * kFar / (kFar - kNear);
    const float depth = static_cast<float>(a + b / viewZ);
    const Vec3 world = fx::UnprojectDepth(inverse, ndcX, ndcY, depth);

    const double input[4] = { world.x + worldOffset.x, world.y + worldOffset.y, world.z + worldOffset.z, 1.0 };
    double clip[4] = {};
    projector.Transform(input, clip);
    const double expected[4] = { ndcX, ndcY, depth, 1.0 };
    return PixelDistance(clip, expected, extent);
}

struct RoundTrip {
    float median = 0.0f;
    float maximum = 0.0f;
};

RoundTrip MeasureRoundTrip(const Vec3& eye, bool relativePath)
{
    const Extent2D extent{ 1280, 720 };
    std::vector<float> errors;
    for (const Pose& pose : MakePoses(12)) {
        const EngineCamera cam = MakeEngineCamera(eye, pose.pitch, pose.yaw, extent);
        const Mat4 relativeMatrix = MakeEyeRelativeViewProjection(cam.viewProjection, cam.projection);
        const EyeRelativeCamera relative = MakeEyeRelativeCameraFromMatrix(relativeMatrix);
        EXPECT_TRUE(relative.valid);
        Mat4 absolute;
        if (!relativePath) {
            EXPECT_TRUE(cam.viewProjection.TryInverse(absolute));
        }

        const Mat4d projector = Mat4d::FromMat4(relativePath ? relativeMatrix : cam.viewProjection);
        const Mat4& inverse = relativePath ? relative.inverse : absolute;
        // The relative inverse gives (world - eye); the double projector already expects that space.
        const Vec3 offset = Vec3::Zero();
        for (const float ndcX : { -0.9f, 0.0f, 0.9f }) {
            for (const float ndcY : { -0.9f, 0.0f, 0.9f }) {
                for (const float viewZ : { 2.0f, 20.0f, 200.0f, 900.0f }) {
                    errors.push_back(RoundTripPixels(inverse, projector, offset, extent, ndcX, ndcY, viewZ));
                }
            }
        }
    }
    std::sort(errors.begin(), errors.end());
    return RoundTrip{ errors[errors.size() / 2], errors.back() };
}

TEST(EyeRelativeCameraTest, RoundTripStaysUnderAHundredthOfAPixelFarFromTheOrigin)
{
    for (const Vec3 eye : { Vec3(2000.0f, 2.0f, -5000.0f), Vec3(20000.0f, 2.0f, -50000.0f) }) {
        const RoundTrip relative = MeasureRoundTrip(eye, true);
        EXPECT_LT(relative.maximum, 0.02f) << eye.x;
    }
}

TEST(EyeRelativeCameraTest, AbsoluteFloatInverseIsOffByPixelsFarFromTheOrigin)
{
    // Positive control for the round-trip measurement above.
    const RoundTrip absolute = MeasureRoundTrip(Vec3(2000.0f, 2.0f, -5000.0f), false);
    EXPECT_GT(absolute.median, 0.2f);
}

TEST(EyeRelativeCameraTest, ViewProjectionTimesTranslationFallbackMatchesTheDrawnCamera)
{
    const Extent2D extent{ 1280, 720 };
    const EngineCamera cam = MakeEngineCamera(Vec3(2000.0f, 2.0f, -5000.0f), -8.0f, 37.0f, extent);
    const EyeRelativeCamera fallback = MakeEyeRelativeCamera(cam.viewProjection, cam.eye);
    ASSERT_TRUE(fallback.valid);

    // A point 10 m in front of the camera lands on the same pixel through the absolute and the relative matrix.
    const Vec3 ahead = Quat::FromEulerDegrees(-8.0f, 37.0f, 0.0f).RotateVector(Vec3::Forward()) * 10.0f;
    const Vec4 absoluteClip = cam.viewProjection * Vec4(cam.eye.x + ahead.x, cam.eye.y + ahead.y, cam.eye.z + ahead.z, 1.0f);
    const Vec4 relativeClip = fallback.viewProjection * Vec4(ahead.x, ahead.y, ahead.z, 1.0f);
    EXPECT_NEAR(absoluteClip.x / absoluteClip.w, relativeClip.x / relativeClip.w, 5e-4f);
    EXPECT_NEAR(absoluteClip.y / absoluteClip.w, relativeClip.y / relativeClip.w, 5e-4f);
}

TEST(EyeRelativeCameraTest, PartsMatrixKeepsTheProjectionTranslationColumn)
{
    const Extent2D extent{ 1280, 720 };
    const EngineCamera cam = MakeEngineCamera(Vec3(2000.0f, 2.0f, -5000.0f), 10.0f, 200.0f, extent);
    const Mat4 relative = MakeEyeRelativeViewProjection(cam.viewProjection, cam.projection);
    for (int r = 0; r < 4; ++r) {
        EXPECT_EQ(relative(r, 3), cam.projection(r, 3));
        for (int c = 0; c < 3; ++c) {
            EXPECT_EQ(relative(r, c), cam.viewProjection(r, c));
        }
    }
}

TEST(EyeRelativeCameraTest, SingularMatrixGivesAnInvalidCamera)
{
    EXPECT_FALSE(MakeEyeRelativeCameraFromMatrix(Mat4{}).valid);
    EXPECT_FALSE(MakeEyeRelativeCamera(Mat4{}, Vec3(1.0f, 2.0f, 3.0f)).valid);
}

TEST(EyeRelativeCameraTest, RelativeBasisKeepsTheRealEyeAndReportsAnOriginShaderEye)
{
    const Extent2D extent{ 1280, 720 };
    const Vec3 eye(2000.0f, 2.0f, -5000.0f);
    const EngineCamera cam = MakeEngineCamera(eye, -8.0f, 37.0f, extent);
    const EyeRelativeCamera relative =
        MakeEyeRelativeCameraFromMatrix(MakeEyeRelativeViewProjection(cam.viewProjection, cam.projection));

    const fx::CameraBasis basis = fx::DeriveCameraBasisRelative(eye, relative, extent);
    ASSERT_TRUE(basis.valid);
    EXPECT_TRUE(basis.eyeRelative);
    EXPECT_EQ(basis.eye.x, eye.x);
    EXPECT_EQ(basis.ShaderEye().x, 0.0f);
    EXPECT_EQ(basis.ShaderEye().y, 0.0f);
    EXPECT_EQ(basis.ShaderEye().z, 0.0f);

    const Vec3 expectedForward = Quat::FromEulerDegrees(-8.0f, 37.0f, 0.0f).RotateVector(Vec3::Forward());
    EXPECT_GT(Dot(basis.forward, expectedForward), 0.9999f);

    // The legacy basis hands the real eye to the shaders.
    const EngineCamera atOrigin = MakeEngineCamera(Vec3(1.0f, 2.0f, 3.0f), -8.0f, 37.0f, extent);
    Mat4 originInverse;
    ASSERT_TRUE(atOrigin.viewProjection.TryInverse(originInverse));
    const fx::CameraBasis legacy = fx::DeriveCameraBasis(atOrigin.eye, originInverse, extent);
    ASSERT_TRUE(legacy.valid);
    EXPECT_FALSE(legacy.eyeRelative);
    EXPECT_EQ(legacy.ShaderEye().x, 1.0f);
}

TEST(EyeRelativeCameraTest, ViewBasisIgnoresTheJitter)
{
    const Extent2D extent{ 1280, 720 };
    const Vec3 eye(2000.0f, 2.0f, -5000.0f);
    const EngineCamera cam = MakeEngineCamera(eye, -8.0f, 37.0f, extent);

    fx::View view;
    view.extent = extent;
    view.eye = eye;
    view.viewProjection = cam.viewProjection;
    view.relative = MakeEyeRelativeCameraFromMatrix(MakeEyeRelativeViewProjection(cam.viewProjection, cam.projection));
    const fx::CameraBasis still = fx::DeriveViewCameraBasis(view);
    view.jitterNdc = Vec2(0.0007f, -0.0011f);
    const fx::CameraBasis jittered = fx::DeriveViewCameraBasis(view);

    ASSERT_TRUE(still.valid);
    ASSERT_TRUE(jittered.valid);
    EXPECT_EQ(still.forward.x, jittered.forward.x);
    EXPECT_EQ(still.pixelScale, jittered.pixelScale);
}

TEST(EyeRelativeCameraTest, ViewWithoutARelativeCameraHasNoBasis)
{
    EXPECT_FALSE(fx::DeriveViewCameraBasis(fx::View{}).valid);
}

// What the previous-frame shader matrix does with a world point seen from the current eye.
Vec4 ProjectWithPrevious(const fx::CameraBasis& basis, const fx::TemporalHistory& previous, const Vec3& worldMinusCurrentEye)
{
    const Mat4 matrix = fx::PreviousViewProjectionForShader(basis, previous);
    return matrix * Vec4(worldMinusCurrentEye.x, worldMinusCurrentEye.y, worldMinusCurrentEye.z, 1.0f);
}

TEST(EyeRelativeCameraTest, PreviousMatrixOfAStillCameraEqualsTheCurrentOne)
{
    const Extent2D extent{ 1280, 720 };
    const Vec3 eye(2000.0f, 2.0f, -5000.0f);
    const EngineCamera cam = MakeEngineCamera(eye, -8.0f, 37.0f, extent);
    const EyeRelativeCamera relative =
        MakeEyeRelativeCameraFromMatrix(MakeEyeRelativeViewProjection(cam.viewProjection, cam.projection));
    const fx::CameraBasis basis = fx::DeriveCameraBasisRelative(eye, relative, extent);
    ASSERT_TRUE(basis.valid);
    const fx::TemporalHistory previous = fx::MakeHistory(basis, cam.viewProjection, 4, extent, 1, fx::TemporalHistory{});
    ASSERT_TRUE(previous.relativeValid);

    const Vec3 point(1.0f, 0.5f, 30.0f);
    const Vec4 now = relative.viewProjection * Vec4(point.x, point.y, point.z, 1.0f);
    const Vec4 before = ProjectWithPrevious(basis, previous, point);
    EXPECT_NEAR(now.x / now.w, before.x / before.w, 1e-6f);
    EXPECT_NEAR(now.y / now.w, before.y / before.w, 1e-6f);

    // Without the stored relative matrix the absolute one is shifted instead; the result agrees to the rounding of that matrix.
    fx::TemporalHistory absoluteOnly = previous;
    absoluteOnly.relativeValid = false;
    const Vec4 fallback = ProjectWithPrevious(basis, absoluteOnly, point);
    EXPECT_NEAR(now.x / now.w, fallback.x / fallback.w, 5e-4f);
    EXPECT_NEAR(now.y / now.w, fallback.y / fallback.w, 5e-4f);
}

TEST(EyeRelativeCameraTest, PreviousMatrixFollowsAMovingEye)
{
    const Extent2D extent{ 1280, 720 };
    const Vec3 previousEye(2000.0f, 2.0f, -5000.0f);
    const Vec3 currentEye(2000.3f, 2.1f, -4999.6f);
    const EngineCamera before = MakeEngineCamera(previousEye, -8.0f, 37.0f, extent);
    const EngineCamera now = MakeEngineCamera(currentEye, -8.0f, 37.0f, extent);

    const auto basisOf = [&](const EngineCamera& cam) {
        return fx::DeriveCameraBasisRelative(cam.eye,
            MakeEyeRelativeCameraFromMatrix(MakeEyeRelativeViewProjection(cam.viewProjection, cam.projection)), extent);
    };
    const fx::CameraBasis previousBasis = basisOf(before);
    const fx::CameraBasis currentBasis = basisOf(now);
    ASSERT_TRUE(previousBasis.valid);
    ASSERT_TRUE(currentBasis.valid);
    const fx::TemporalHistory previous = fx::MakeHistory(previousBasis, before.viewProjection, 8, extent, 1, fx::TemporalHistory{});

    // A world point 25 m ahead of the old eye, expressed relative to the new eye.
    const Vec3 ahead = Quat::FromEulerDegrees(-8.0f, 37.0f, 0.0f).RotateVector(Vec3::Forward()) * 25.0f;
    const Vec3 worldMinusPreviousEye = ahead;
    const Vec3 worldMinusCurrentEye = ahead - (currentEye - previousEye);

    const Vec4 viaShader = ProjectWithPrevious(currentBasis, previous, worldMinusCurrentEye);
    const Vec4 direct = previous.viewProjectionRelative
        * Vec4(worldMinusPreviousEye.x, worldMinusPreviousEye.y, worldMinusPreviousEye.z, 1.0f);
    EXPECT_NEAR(viaShader.x / viaShader.w, direct.x / direct.w, 2e-5f);
    EXPECT_NEAR(viaShader.y / viaShader.w, direct.y / direct.w, 2e-5f);
}

TEST(EyeRelativeCameraTest, ResolveBlockCarriesAnOriginEyeAndTheEyeStep)
{
    const Extent2D extent{ 1280, 720 };
    const Vec3 previousEye(2000.0f, 2.0f, -5000.0f);
    const Vec3 currentEye(2000.5f, 2.0f, -4999.0f);
    const EngineCamera before = MakeEngineCamera(previousEye, -8.0f, 37.0f, extent);
    const EngineCamera now = MakeEngineCamera(currentEye, -8.0f, 37.0f, extent);
    const auto basisOf = [&](const EngineCamera& cam) {
        return fx::DeriveCameraBasisRelative(cam.eye,
            MakeEyeRelativeCameraFromMatrix(MakeEyeRelativeViewProjection(cam.viewProjection, cam.projection)), extent);
    };
    const fx::CameraBasis previousBasis = basisOf(before);
    const fx::CameraBasis currentBasis = basisOf(now);
    ASSERT_TRUE(previousBasis.valid && currentBasis.valid);
    const fx::TemporalHistory previous = fx::MakeHistory(previousBasis, before.viewProjection, 8, extent, 1, fx::TemporalHistory{});

    const fx::TemporalResolveParamsGpu params = fx::MakeTemporalResolveParams(
        currentBasis, previous, fx::TemporalResolveSettings{}, extent, currentBasis.shaderInverse, true);
    EXPECT_EQ(params.eye[0], 0.0f);
    EXPECT_EQ(params.eye[1], 0.0f);
    EXPECT_EQ(params.eye[2], 0.0f);
    EXPECT_FLOAT_EQ(params.prevEye[0], -0.5f);
    EXPECT_FLOAT_EQ(params.prevEye[1], 0.0f);
    EXPECT_FLOAT_EQ(params.prevEye[2], -1.0f);
    EXPECT_EQ(sizeof(fx::TemporalResolveParamsGpu), 224u);
}

TEST(EyeRelativeCameraTest, AbsoluteBasisKeepsTheOldBlockContents)
{
    const Extent2D extent{ 1280, 720 };
    const Vec3 eye(3.0f, 1.7f, -4.0f);
    const Mat4 viewProjection = Mat4::PerspectiveFovLH_ZO(1.0f, 16.0f / 9.0f, 0.1f, 200.0f, true)
        * Mat4::LookAtLH(eye, Vec3(1.0f, 1.0f, 2.0f), Vec3::Up());
    Mat4 inverse;
    ASSERT_TRUE(viewProjection.TryInverse(inverse));
    const fx::CameraBasis basis = fx::DeriveCameraBasis(eye, inverse, extent);
    ASSERT_TRUE(basis.valid);

    fx::TemporalHistory previous;
    previous.viewProjection = viewProjection;
    previous.eye = Vec3(2.0f, 1.0f, -3.0f);
    const fx::TemporalResolveParamsGpu params =
        fx::MakeTemporalResolveParams(basis, previous, fx::TemporalResolveSettings{}, extent, inverse, true);
    EXPECT_EQ(params.eye[0], eye.x);
    EXPECT_EQ(params.prevEye[0], 2.0f);
    EXPECT_TRUE(ApproximatelyEqual(params.prevViewProjection, viewProjection));
}

} // namespace
} // namespace gte
