// Tests for the CPU reference of the Frame Debugger channels/levels preview
// (ApplyFrameDebuggerPreviewTransform). The GPU dispatcher is not exercised here.

#include "Editor/FrameDebuggerPreviewProcessing.h"
#include "Math/ToneMapCurves.h"

#include <gtest/gtest.h>

namespace gte {
namespace {

TEST(FrameDebuggerPreviewProcessingTest, AllChannelWithNeutralLevelsIsANoOp)
{
    const std::array<float, 4> src{ 0.2f, 0.4f, 0.6f, 0.8f };
    const std::array<float, 4> result =
        ApplyFrameDebuggerPreviewTransform(src, FrameDebuggerPreviewChannel::All, 0.0f, 1.0f);

    EXPECT_FLOAT_EQ(result[0], 0.2f);
    EXPECT_FLOAT_EQ(result[1], 0.4f);
    EXPECT_FLOAT_EQ(result[2], 0.6f);
    EXPECT_FLOAT_EQ(result[3], 0.8f);
}

TEST(FrameDebuggerPreviewProcessingTest, IsolatingRedReplicatesRedChannelAndForcesOpaqueAlpha)
{
    const std::array<float, 4> src{ 0.3f, 0.5f, 0.7f, 0.9f };
    const std::array<float, 4> result =
        ApplyFrameDebuggerPreviewTransform(src, FrameDebuggerPreviewChannel::R, 0.0f, 1.0f);

    EXPECT_FLOAT_EQ(result[0], 0.3f);
    EXPECT_FLOAT_EQ(result[1], 0.3f);
    EXPECT_FLOAT_EQ(result[2], 0.3f);
    EXPECT_FLOAT_EQ(result[3], 1.0f);
}

TEST(FrameDebuggerPreviewProcessingTest, IsolatingGreenReplicatesGreenChannelAndForcesOpaqueAlpha)
{
    const std::array<float, 4> src{ 0.3f, 0.5f, 0.7f, 0.9f };
    const std::array<float, 4> result =
        ApplyFrameDebuggerPreviewTransform(src, FrameDebuggerPreviewChannel::G, 0.0f, 1.0f);

    EXPECT_FLOAT_EQ(result[0], 0.5f);
    EXPECT_FLOAT_EQ(result[1], 0.5f);
    EXPECT_FLOAT_EQ(result[2], 0.5f);
    EXPECT_FLOAT_EQ(result[3], 1.0f);
}

TEST(FrameDebuggerPreviewProcessingTest, IsolatingBlueReplicatesBlueChannelAndForcesOpaqueAlpha)
{
    const std::array<float, 4> src{ 0.3f, 0.5f, 0.7f, 0.9f };
    const std::array<float, 4> result =
        ApplyFrameDebuggerPreviewTransform(src, FrameDebuggerPreviewChannel::B, 0.0f, 1.0f);

    EXPECT_FLOAT_EQ(result[0], 0.7f);
    EXPECT_FLOAT_EQ(result[1], 0.7f);
    EXPECT_FLOAT_EQ(result[2], 0.7f);
    EXPECT_FLOAT_EQ(result[3], 1.0f);
}

TEST(FrameDebuggerPreviewProcessingTest, IsolatingAlphaReplicatesAlphaChannelAndForcesOpaqueAlpha)
{
    const std::array<float, 4> src{ 0.3f, 0.5f, 0.7f, 0.9f };
    const std::array<float, 4> result =
        ApplyFrameDebuggerPreviewTransform(src, FrameDebuggerPreviewChannel::A, 0.0f, 1.0f);

    EXPECT_FLOAT_EQ(result[0], 0.9f);
    EXPECT_FLOAT_EQ(result[1], 0.9f);
    EXPECT_FLOAT_EQ(result[2], 0.9f);
    EXPECT_FLOAT_EQ(result[3], 1.0f);
}

TEST(FrameDebuggerPreviewProcessingTest, LevelsRemapClampsBelowBlackToZero)
{
    const std::array<float, 4> src{ 0.1f, 0.1f, 0.1f, 0.1f }; // Below black=0.25.
    const std::array<float, 4> result =
        ApplyFrameDebuggerPreviewTransform(src, FrameDebuggerPreviewChannel::All, 0.25f, 0.75f);

    EXPECT_FLOAT_EQ(result[0], 0.0f);
    EXPECT_FLOAT_EQ(result[1], 0.0f);
    EXPECT_FLOAT_EQ(result[2], 0.0f);
    EXPECT_FLOAT_EQ(result[3], 0.0f);
}

TEST(FrameDebuggerPreviewProcessingTest, LevelsRemapClampsAboveWhiteToOne)
{
    const std::array<float, 4> src{ 0.9f, 0.9f, 0.9f, 0.9f }; // Above white=0.75.
    const std::array<float, 4> result =
        ApplyFrameDebuggerPreviewTransform(src, FrameDebuggerPreviewChannel::All, 0.25f, 0.75f);

    EXPECT_FLOAT_EQ(result[0], 1.0f);
    EXPECT_FLOAT_EQ(result[1], 1.0f);
    EXPECT_FLOAT_EQ(result[2], 1.0f);
    EXPECT_FLOAT_EQ(result[3], 1.0f);
}

TEST(FrameDebuggerPreviewProcessingTest, LevelsRemapMapsExactMidpointToOneHalf)
{
    const std::array<float, 4> src{ 0.5f, 0.5f, 0.5f, 0.5f }; // Exactly halfway between 0.25 and 0.75.
    const std::array<float, 4> result =
        ApplyFrameDebuggerPreviewTransform(src, FrameDebuggerPreviewChannel::All, 0.25f, 0.75f);

    EXPECT_FLOAT_EQ(result[0], 0.5f);
    EXPECT_FLOAT_EQ(result[1], 0.5f);
    EXPECT_FLOAT_EQ(result[2], 0.5f);
    EXPECT_FLOAT_EQ(result[3], 0.5f);
}

TEST(FrameDebuggerPreviewProcessingTest, LevelsAndChannelIsolationCombineInDocumentedOrder)
{
    // Levels are applied FIRST, then channel isolation - so isolating Green
    // here should show the LEVELED green value (0.5 -> remapped to 0.5
    // exactly, since 0.5 is the midpoint of [0.25, 0.75]), not the raw one.
    const std::array<float, 4> src{ 0.1f, 0.5f, 0.9f, 1.0f };
    const std::array<float, 4> result =
        ApplyFrameDebuggerPreviewTransform(src, FrameDebuggerPreviewChannel::G, 0.25f, 0.75f);

    EXPECT_FLOAT_EQ(result[0], 0.5f);
    EXPECT_FLOAT_EQ(result[1], 0.5f);
    EXPECT_FLOAT_EQ(result[2], 0.5f);
    EXPECT_FLOAT_EQ(result[3], 1.0f);
}

TEST(FrameDebuggerPreviewProcessingTest, SceneReferredToneMapsRgbAndLeavesAlpha)
{
    const std::array<float, 4> src{ 1.0f, 1.0f, 1.0f, 0.5f };
    const std::array<float, 4> result =
        ApplyFrameDebuggerPreviewTransform(src, FrameDebuggerPreviewChannel::All, 0.0f, 1.0f, true);

    const Vec3 expected = tonemap::ToneMapLinearToDisplay(
        Vec3{ 1.0f, 1.0f, 1.0f }, tonemap::kDefaultExposure, tonemap::kDefaultMode, tonemap::kDefaultSaturation);
    EXPECT_FLOAT_EQ(result[0], expected.x);
    EXPECT_FLOAT_EQ(result[1], expected.y);
    EXPECT_FLOAT_EQ(result[2], expected.z);
    EXPECT_NEAR(result[0], 0.88f, 0.01f);
    EXPECT_FLOAT_EQ(result[3], 0.5f);
}

TEST(FrameDebuggerPreviewProcessingTest, SceneReferredMapsBlackToBlackAndIsMonotonic)
{
    const auto lum = [](float v) {
        return ApplyFrameDebuggerPreviewTransform({ v, v, v, 1.0f }, FrameDebuggerPreviewChannel::R, 0.0f, 1.0f, true)[0];
    };
    EXPECT_FLOAT_EQ(lum(0.0f), 0.0f);
    EXPECT_LT(lum(0.05f), lum(0.2f));
    EXPECT_LT(lum(0.2f), lum(1.0f));
    EXPECT_LE(lum(1.0f), lum(50.0f));
    EXPECT_LE(lum(50.0f), 1.0f);
}

TEST(FrameDebuggerPreviewProcessingTest, WithoutSceneReferredOnlyLevelsApply)
{
    const std::array<float, 4> src{ 0.25f, 0.5f, 2.0f, 1.0f };
    const std::array<float, 4> result =
        ApplyFrameDebuggerPreviewTransform(src, FrameDebuggerPreviewChannel::All, 0.0f, 1.0f, false);

    EXPECT_FLOAT_EQ(result[0], 0.25f);
    EXPECT_FLOAT_EQ(result[1], 0.5f);
    EXPECT_FLOAT_EQ(result[2], 1.0f); // Levels clamp only.
}

} // namespace
} // namespace gte
