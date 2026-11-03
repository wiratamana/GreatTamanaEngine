// Tier-1 tests for the pure CPU side of the TemporalAa feature. No Core, no GPU.

#include "Features/TemporalAa/TemporalAaMath.h"

#include <gtest/gtest.h>

#include <cmath>
#include <cstddef>
#include <limits>
#include <set>
#include <utility>

namespace gte {
namespace {

constexpr float kNaN = std::numeric_limits<float>::quiet_NaN();
constexpr float kInf = std::numeric_limits<float>::infinity();

// --- Sanitize ---------------------------------------------------------------------

TEST(TemporalAaMathTest, SanitizeReplacesNonFiniteWithDefaults)
{
    TemporalAaSettings bad;
    bad.jitterScale = kNaN;
    bad.feedbackMin = kInf;
    bad.feedbackMax = -kInf;
    bad.clipGamma = kNaN;
    bad.depthTolerance = kInf;

    const TemporalAaSettings safe = SanitizeTemporalAaSettings(bad);
    const TemporalAaSettings d{};
    EXPECT_EQ(safe.jitterScale, d.jitterScale);
    EXPECT_EQ(safe.feedbackMin, d.feedbackMin);
    EXPECT_EQ(safe.feedbackMax, d.feedbackMax);
    EXPECT_EQ(safe.clipGamma, d.clipGamma);
    EXPECT_EQ(safe.depthTolerance, d.depthTolerance);
}

TEST(TemporalAaMathTest, SanitizeClampsEveryRange)
{
    TemporalAaSettings low;
    low.jitterScale = -1.0f;
    low.feedbackMin = -1.0f;
    low.feedbackMax = -1.0f;
    low.clipGamma = 0.0f;
    low.depthTolerance = 0.0f;
    const TemporalAaSettings a = SanitizeTemporalAaSettings(low);
    EXPECT_EQ(a.jitterScale, 0.0f);
    EXPECT_EQ(a.feedbackMin, 0.0f);
    EXPECT_EQ(a.feedbackMax, 0.0f);
    EXPECT_EQ(a.clipGamma, 0.25f);
    EXPECT_EQ(a.depthTolerance, 0.001f);

    TemporalAaSettings high;
    high.jitterScale = 99.0f;
    high.feedbackMin = 5.0f;
    high.feedbackMax = 5.0f;
    high.clipGamma = 99.0f;
    high.depthTolerance = 99.0f;
    const TemporalAaSettings b = SanitizeTemporalAaSettings(high);
    EXPECT_EQ(b.jitterScale, 8.0f);
    EXPECT_EQ(b.feedbackMin, 0.99f);
    EXPECT_EQ(b.feedbackMax, 0.99f);
    EXPECT_EQ(b.clipGamma, 4.0f);
    EXPECT_EQ(b.depthTolerance, 0.5f);
}

TEST(TemporalAaMathTest, SanitizeLiftsFeedbackMaxToFeedbackMin)
{
    TemporalAaSettings s;
    s.feedbackMin = 0.9f;
    s.feedbackMax = 0.5f;
    const TemporalAaSettings safe = SanitizeTemporalAaSettings(s);
    EXPECT_EQ(safe.feedbackMin, 0.9f);
    EXPECT_EQ(safe.feedbackMax, 0.9f);
}

TEST(TemporalAaMathTest, DefaultsAreInsideTheSanitizerRanges)
{
    const TemporalAaSettings d{};
    EXPECT_EQ(d.feedbackMin, 0.7f);
    EXPECT_EQ(d.feedbackMax, 0.9f);
    EXPECT_EQ(d.clipGamma, 0.75f);
    const TemporalAaSettings safe = SanitizeTemporalAaSettings(d);
    EXPECT_EQ(safe.feedbackMin, d.feedbackMin);
    EXPECT_EQ(safe.feedbackMax, d.feedbackMax);
    EXPECT_EQ(safe.clipGamma, d.clipGamma);
}

TEST(TemporalAaMathTest, SanitizeClampsSampleCountAndDebugView)
{
    TemporalAaSettings s;
    s.jitterSampleCount = 0;
    EXPECT_EQ(SanitizeTemporalAaSettings(s).jitterSampleCount, 2u);
    s.jitterSampleCount = 99;
    EXPECT_EQ(SanitizeTemporalAaSettings(s).jitterSampleCount, 16u);

    s.debugView = static_cast<TemporalAaDebugView>(77);
    EXPECT_EQ(SanitizeTemporalAaSettings(s).debugView, TemporalAaDebugView::Off);
    s.debugView = TemporalAaDebugView::JitterOnly;
    EXPECT_EQ(SanitizeTemporalAaSettings(s).debugView, TemporalAaDebugView::JitterOnly);
}

// --- Halton and jitter ------------------------------------------------------------

TEST(TemporalAaMathTest, HaltonMatchesKnownValues)
{
    EXPECT_FLOAT_EQ(TemporalAaHalton(1, 2), 0.5f);
    EXPECT_FLOAT_EQ(TemporalAaHalton(2, 2), 0.25f);
    EXPECT_FLOAT_EQ(TemporalAaHalton(3, 2), 0.75f);
    EXPECT_FLOAT_EQ(TemporalAaHalton(1, 3), 1.0f / 3.0f);
    EXPECT_FLOAT_EQ(TemporalAaHalton(2, 3), 2.0f / 3.0f);
    EXPECT_FLOAT_EQ(TemporalAaHalton(3, 3), 1.0f / 9.0f);
}

TEST(TemporalAaMathTest, HaltonStaysInUnitRange)
{
    for (std::uint32_t i = 0; i < 256; ++i) {
        for (const std::uint32_t base : { 2u, 3u }) {
            const float h = TemporalAaHalton(i, base);
            EXPECT_GE(h, 0.0f);
            EXPECT_LT(h, 1.0f);
        }
    }
}

TEST(TemporalAaMathTest, JitterPixelsIsExactlyZeroWithScaleZero)
{
    TemporalAaSettings s;
    s.jitterScale = 0.0f;
    for (std::uint64_t frame = 0; frame < 20; ++frame) {
        const Vec2 j = TemporalAaJitterPixels(frame, s);
        EXPECT_TRUE(j.x == 0.0f && j.y == 0.0f);
    }
}

TEST(TemporalAaMathTest, JitterPixelsStayInsideHalfAPixelAndRepeatEveryCycle)
{
    TemporalAaSettings s;
    s.jitterSampleCount = 8;
    for (std::uint64_t frame = 0; frame < 40; ++frame) {
        const Vec2 j = TemporalAaJitterPixels(frame, s);
        EXPECT_GE(j.x, -0.5f);
        EXPECT_LE(j.x, 0.5f);
        EXPECT_GE(j.y, -0.5f);
        EXPECT_LE(j.y, 0.5f);
        EXPECT_EQ(j, TemporalAaJitterPixels(frame + s.jitterSampleCount, s));
    }
}

TEST(TemporalAaMathTest, JitterPixelsOfOneCycleAreAllDifferent)
{
    TemporalAaSettings s;
    s.jitterSampleCount = 16;
    std::set<std::pair<float, float>> seen;
    for (std::uint64_t frame = 0; frame < s.jitterSampleCount; ++frame) {
        const Vec2 j = TemporalAaJitterPixels(frame, s);
        seen.insert({ j.x, j.y });
    }
    EXPECT_EQ(seen.size(), 16u);
}

TEST(TemporalAaMathTest, JitterPixelsScaleIsLinear)
{
    TemporalAaSettings one;
    TemporalAaSettings two;
    two.jitterScale = 2.0f;
    for (std::uint64_t frame = 0; frame < 8; ++frame) {
        const Vec2 a = TemporalAaJitterPixels(frame, one);
        const Vec2 b = TemporalAaJitterPixels(frame, two);
        EXPECT_FLOAT_EQ(b.x, 2.0f * a.x);
        EXPECT_FLOAT_EQ(b.y, 2.0f * a.y);
    }
}

TEST(TemporalAaMathTest, JitterNdcScalesByTheExtent)
{
    const Vec2 ndc = TemporalAaJitterNdc(Vec2{ 0.5f, -0.25f }, Extent2D{ 1920, 1080 });
    EXPECT_FLOAT_EQ(ndc.x, 2.0f * 0.5f / 1920.0f);
    EXPECT_FLOAT_EQ(ndc.y, 2.0f * -0.25f / 1080.0f);

    EXPECT_EQ(TemporalAaJitterNdc(Vec2{ 0.5f, 0.5f }, Extent2D{ 0, 1080 }), Vec2::Zero());
    EXPECT_EQ(TemporalAaJitterNdc(Vec2{ 0.5f, 0.5f }, Extent2D{ 1920, 0 }), Vec2::Zero());
}

// --- Decision ---------------------------------------------------------------------

TemporalAaJitterInputs AllGood()
{
    TemporalAaJitterInputs in;
    in.primaryView = true;
    in.enabled = true;
    in.captureArmed = false;
    in.passesEnabled = true;
    in.cameraValid = true;
    in.extent = Extent2D{ 1280, 720 };
    in.shaderReady = true;
    in.resolvedLastFrame = true;
    return in;
}

TEST(TemporalAaMathTest, DecideAppliesWhenEveryInputIsGood)
{
    EXPECT_EQ(DecideTemporalAaJitter(AllGood()), TemporalAaJitterDecision::Apply);
}

TEST(TemporalAaMathTest, DecideNamesEachFailingInput)
{
    using D = TemporalAaJitterDecision;
    TemporalAaJitterInputs in = AllGood();
    in.primaryView = false;
    EXPECT_EQ(DecideTemporalAaJitter(in), D::NotPrimaryView);

    in = AllGood();
    in.enabled = false;
    EXPECT_EQ(DecideTemporalAaJitter(in), D::Disabled);

    in = AllGood();
    in.captureArmed = true;
    EXPECT_EQ(DecideTemporalAaJitter(in), D::CaptureArmed);

    in = AllGood();
    in.passesEnabled = false;
    EXPECT_EQ(DecideTemporalAaJitter(in), D::PassDisabled);

    in = AllGood();
    in.cameraValid = false;
    EXPECT_EQ(DecideTemporalAaJitter(in), D::NoValidCamera);

    in = AllGood();
    in.extent = Extent2D{ kTemporalAaMinViewSide - 1, 720 };
    EXPECT_EQ(DecideTemporalAaJitter(in), D::ExtentTooSmall);
    in.extent = Extent2D{ 1280, kTemporalAaMinViewSide - 1 };
    EXPECT_EQ(DecideTemporalAaJitter(in), D::ExtentTooSmall);

    in = AllGood();
    in.shaderReady = false;
    EXPECT_EQ(DecideTemporalAaJitter(in), D::ShadersNotReady);

    in = AllGood();
    in.resolvedLastFrame = false;
    EXPECT_EQ(DecideTemporalAaJitter(in), D::NoHeartbeat);
}

TEST(TemporalAaMathTest, DecideChecksRunInTheDocumentedOrder)
{
    using D = TemporalAaJitterDecision;
    TemporalAaJitterInputs in = AllGood();
    in.primaryView = false;
    in.enabled = false;
    EXPECT_EQ(DecideTemporalAaJitter(in), D::NotPrimaryView);

    in = AllGood();
    in.enabled = false;
    in.captureArmed = true;
    EXPECT_EQ(DecideTemporalAaJitter(in), D::Disabled);

    in = AllGood();
    in.captureArmed = true;
    in.passesEnabled = false;
    EXPECT_EQ(DecideTemporalAaJitter(in), D::CaptureArmed);

    in = AllGood();
    in.passesEnabled = false;
    in.cameraValid = false;
    EXPECT_EQ(DecideTemporalAaJitter(in), D::PassDisabled);

    in = AllGood();
    in.cameraValid = false;
    in.extent = Extent2D{ 1, 1 };
    EXPECT_EQ(DecideTemporalAaJitter(in), D::NoValidCamera);

    in = AllGood();
    in.extent = Extent2D{ 1, 1 };
    in.shaderReady = false;
    EXPECT_EQ(DecideTemporalAaJitter(in), D::ExtentTooSmall);

    in = AllGood();
    in.shaderReady = false;
    in.resolvedLastFrame = false;
    EXPECT_EQ(DecideTemporalAaJitter(in), D::ShadersNotReady);
}

// --- Feedback ---------------------------------------------------------------------

TEST(TemporalAaMathTest, FeedbackIsMaxWhenLumasAgree)
{
    EXPECT_FLOAT_EQ(TemporalAaFeedback(0.7f, 0.7f, 0.85f, 0.96f), 0.96f);
}

TEST(TemporalAaMathTest, FeedbackFallsTowardMinWhenLumasDiffer)
{
    EXPECT_NEAR(TemporalAaFeedback(10.0f, 0.0f, 0.85f, 0.96f), 0.85f, 1e-5f);
}

TEST(TemporalAaMathTest, FeedbackStaysNearMaxInTheDark)
{
    // Both lumas are below the floor, so the gap is divided by the floor and stays small.
    EXPECT_GT(TemporalAaFeedback(0.01f, 0.0f, 0.85f, 0.96f), 0.94f);
}

TEST(TemporalAaMathTest, FeedbackStaysInsideItsRange)
{
    for (const float a : { 0.0f, 0.05f, 0.2f, 1.0f, 50.0f }) {
        for (const float b : { 0.0f, 0.05f, 0.2f, 1.0f, 50.0f }) {
            const float f = TemporalAaFeedback(a, b, 0.85f, 0.96f);
            EXPECT_GE(f, 0.85f);
            EXPECT_LE(f, 0.96f);
        }
    }
    EXPECT_EQ(TemporalAaFeedback(kNaN, 1.0f, 0.85f, 0.96f), 0.85f);
}

// --- GPU params -------------------------------------------------------------------

TEST(TemporalAaMathTest, ParamsCarryHistoryExtentAndTunables)
{
    fx::CameraBasis basis;
    basis.valid = true;
    basis.eye = Vec3{ 1.0f, 2.0f, 3.0f };
    basis.forward = Vec3{ 0.0f, 0.0f, 1.0f };

    fx::TemporalHistory previous;
    previous.eye = Vec3{ 4.0f, 5.0f, 6.0f };
    previous.forward = Vec3{ 1.0f, 0.0f, 0.0f };
    previous.viewProjection = Mat4::Identity();
    previous.viewProjection(0, 3) = 7.0f;

    TemporalAaSettings safe;
    safe.feedbackMin = 0.5f;
    safe.feedbackMax = 0.75f;
    safe.depthTolerance = 0.125f;
    safe.clipGamma = 2.0f;

    const Extent2D extent{ 640, 360 };
    TemporalAaParamsGpu p = MakeTemporalAaParams(basis, previous, true, safe, Mat4::Identity(), extent);
    EXPECT_EQ(p.counts[0], 1u);
    EXPECT_EQ(p.counts[1], 640u);
    EXPECT_EQ(p.counts[2], 360u);
    EXPECT_EQ(p.blend[0], 0.5f);
    EXPECT_EQ(p.blend[1], 0.75f);
    EXPECT_EQ(p.blend[2], 0.125f);
    EXPECT_EQ(p.blend[3], 2.0f);
    EXPECT_EQ(p.look[0], 0.0f);
    EXPECT_EQ(p.prevViewProjection(0, 3), 7.0f);
    EXPECT_EQ(p.eye[0], 1.0f);
    EXPECT_EQ(p.eye[2], 3.0f);
    EXPECT_EQ(p.prevEye[0], 4.0f);
    EXPECT_EQ(p.prevForward[0], 1.0f);

    p = MakeTemporalAaParams(basis, previous, false, safe, Mat4::Identity(), extent);
    EXPECT_EQ(p.counts[0], 0u);

    safe.debugView = TemporalAaDebugView::JitterOnly;
    p = MakeTemporalAaParams(basis, previous, true, safe, Mat4::Identity(), extent);
    EXPECT_EQ(p.look[0], 1.0f);
}

TEST(TemporalAaMathTest, GpuBlockOffsetsMatchTheShader)
{
    EXPECT_EQ(offsetof(TemporalAaParamsGpu, blend), 16u * 12u);
    EXPECT_EQ(offsetof(TemporalAaParamsGpu, look), 16u * 13u);
    EXPECT_EQ(offsetof(TemporalAaParamsGpu, counts), 16u * 14u);
}

} // namespace
} // namespace gte
