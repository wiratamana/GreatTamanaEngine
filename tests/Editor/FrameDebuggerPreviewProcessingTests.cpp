// Unit tests for the Frame Debugger's Channels + Levels preview-compositing
// pure CPU oracle (src/Editor/FrameDebuggerPreviewProcessing.h) -
// ApplyFrameDebuggerPreviewTransform() only. FrameDebuggerPreviewRenderer
// itself is inherently Tier 2 (a live VkDevice-owning GPU dispatcher, exactly
// like VolumeTexturePreviewRenderer - see AGENTS.md, "Testability &
// Regression Safety") and is NOT exercised here. Only built when
// GTE_ENABLE_EDITOR is ON, since FrameDebuggerPreviewProcessing.h/.cpp are
// only compiled into gte_core then.
//
// task_manager/frame-debugger-3 campaign, PHASE6
// (PHASE6_CHANNELS_AND_LEVELS_REAL_PREVIEW.md).

#include "Editor/FrameDebuggerPreviewProcessing.h"

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

} // namespace
} // namespace gte
