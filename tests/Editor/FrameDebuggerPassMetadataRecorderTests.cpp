// Unit tests for FrameDebuggerPassMetadataRecorder
// (src/Editor/FrameDebuggerPassMetadataRecorder.h). Pure, hand-fabricated
// calls against one recorder instance - no RenderGraphBuilder/RenderGraph/
// live device involved.

#include "Editor/FrameDebuggerPassMetadataRecorder.h"

#include <gtest/gtest.h>

namespace gte {
namespace {

using rg::PassDebugMetadata;
using rg::RenderPassCategory;
using rg::RenderPassDrawKind;

// A known sentinel value, distinct from every enum default, so a test can
// confirm an "out" parameter was never written to by a failed query.
PassDebugMetadata SentinelMetadata()
{
    return PassDebugMetadata{ RenderPassCategory::FrameDebuggerInternal, RenderPassDrawKind::Blit,
        /*tags=*/0xDEADBEEFULL };
}

TEST(FrameDebuggerPassMetadataRecorderTest, FreshInstanceStartsEmpty)
{
    FrameDebuggerPassMetadataRecorder recorder;

    EXPECT_EQ(recorder.EntryCountForTesting(), 0u);

    PassDebugMetadata out = SentinelMetadata();
    EXPECT_FALSE(recorder.QueryPassDebugMetadata(0, out));
}

TEST(FrameDebuggerPassMetadataRecorderTest, OnPassDeclaredAppendsInDeclarationOrder)
{
    FrameDebuggerPassMetadataRecorder recorder;

    recorder.OnPassDeclared(0, RenderPassCategory::General, RenderPassDrawKind::DrawMesh, /*tags=*/1ULL);
    recorder.OnPassDeclared(1, RenderPassCategory::Debug, RenderPassDrawKind::DrawQuad, /*tags=*/2ULL);
    recorder.OnPassDeclared(2, RenderPassCategory::FrameDebuggerInternal, RenderPassDrawKind::Blit, /*tags=*/4ULL);

    ASSERT_EQ(recorder.EntryCountForTesting(), 3u);

    PassDebugMetadata out0 = SentinelMetadata();
    ASSERT_TRUE(recorder.QueryPassDebugMetadata(0, out0));
    EXPECT_EQ(out0.category, RenderPassCategory::General);
    EXPECT_EQ(out0.drawKind, RenderPassDrawKind::DrawMesh);
    EXPECT_EQ(out0.tags, 1ULL);

    PassDebugMetadata out1 = SentinelMetadata();
    ASSERT_TRUE(recorder.QueryPassDebugMetadata(1, out1));
    EXPECT_EQ(out1.category, RenderPassCategory::Debug);
    EXPECT_EQ(out1.drawKind, RenderPassDrawKind::DrawQuad);
    EXPECT_EQ(out1.tags, 2ULL);

    PassDebugMetadata out2 = SentinelMetadata();
    ASSERT_TRUE(recorder.QueryPassDebugMetadata(2, out2));
    EXPECT_EQ(out2.category, RenderPassCategory::FrameDebuggerInternal);
    EXPECT_EQ(out2.drawKind, RenderPassDrawKind::Blit);
    EXPECT_EQ(out2.tags, 4ULL);
}

TEST(FrameDebuggerPassMetadataRecorderTest, QueryOutOfRangeIndexReturnsFalseAndLeavesOutputUntouched)
{
    FrameDebuggerPassMetadataRecorder recorder;
    recorder.OnPassDeclared(0, RenderPassCategory::General, RenderPassDrawKind::DrawMesh, /*tags=*/1ULL);
    recorder.OnPassDeclared(1, RenderPassCategory::Debug, RenderPassDrawKind::DrawQuad, /*tags=*/2ULL);

    const PassDebugMetadata sentinel = SentinelMetadata();

    PassDebugMetadata outAtSize = sentinel;
    EXPECT_FALSE(recorder.QueryPassDebugMetadata(2, outAtSize));
    EXPECT_EQ(outAtSize.category, sentinel.category);
    EXPECT_EQ(outAtSize.drawKind, sentinel.drawKind);
    EXPECT_EQ(outAtSize.tags, sentinel.tags);

    PassDebugMetadata outFarOutOfRange = sentinel;
    EXPECT_FALSE(recorder.QueryPassDebugMetadata(999, outFarOutOfRange));
    EXPECT_EQ(outFarOutOfRange.category, sentinel.category);
    EXPECT_EQ(outFarOutOfRange.drawKind, sentinel.drawKind);
    EXPECT_EQ(outFarOutOfRange.tags, sentinel.tags);
}

TEST(FrameDebuggerPassMetadataRecorderTest, BeginFrameClearsPreviousEntries)
{
    FrameDebuggerPassMetadataRecorder recorder;
    recorder.OnPassDeclared(0, RenderPassCategory::General, RenderPassDrawKind::DrawMesh, /*tags=*/1ULL);
    recorder.OnPassDeclared(1, RenderPassCategory::Debug, RenderPassDrawKind::DrawQuad, /*tags=*/2ULL);

    recorder.BeginFrame();

    EXPECT_EQ(recorder.EntryCountForTesting(), 0u);

    PassDebugMetadata out = SentinelMetadata();
    EXPECT_FALSE(recorder.QueryPassDebugMetadata(0, out));
}

TEST(FrameDebuggerPassMetadataRecorderTest, BeginFrameThenRedeclareStartsIndicesFreshAtZero)
{
    FrameDebuggerPassMetadataRecorder recorder;
    recorder.OnPassDeclared(0, RenderPassCategory::General, RenderPassDrawKind::DrawMesh, /*tags=*/1ULL);
    recorder.OnPassDeclared(1, RenderPassCategory::Debug, RenderPassDrawKind::DrawQuad, /*tags=*/2ULL);

    recorder.BeginFrame();

    recorder.OnPassDeclared(0, RenderPassCategory::FrameDebuggerInternal, RenderPassDrawKind::Blit, /*tags=*/99ULL);

    ASSERT_EQ(recorder.EntryCountForTesting(), 1u);

    PassDebugMetadata out = SentinelMetadata();
    ASSERT_TRUE(recorder.QueryPassDebugMetadata(0, out));
    EXPECT_EQ(out.category, RenderPassCategory::FrameDebuggerInternal);
    EXPECT_EQ(out.drawKind, RenderPassDrawKind::Blit);
    EXPECT_EQ(out.tags, 99ULL);
}

TEST(FrameDebuggerPassMetadataRecorderTest, MultipleBeginFrameCyclesNeverLeakBetweenCycles)
{
    FrameDebuggerPassMetadataRecorder recorder;

    // Cycle 1.
    recorder.OnPassDeclared(0, RenderPassCategory::General, RenderPassDrawKind::DrawMesh, /*tags=*/10ULL);
    recorder.OnPassDeclared(1, RenderPassCategory::General, RenderPassDrawKind::DrawMesh, /*tags=*/11ULL);
    ASSERT_EQ(recorder.EntryCountForTesting(), 2u);
    {
        PassDebugMetadata out = SentinelMetadata();
        ASSERT_TRUE(recorder.QueryPassDebugMetadata(0, out));
        EXPECT_EQ(out.tags, 10ULL);
        ASSERT_TRUE(recorder.QueryPassDebugMetadata(1, out));
        EXPECT_EQ(out.tags, 11ULL);
    }

    // Cycle 2.
    recorder.BeginFrame();
    recorder.OnPassDeclared(0, RenderPassCategory::Debug, RenderPassDrawKind::DrawQuad, /*tags=*/20ULL);
    ASSERT_EQ(recorder.EntryCountForTesting(), 1u);
    {
        PassDebugMetadata out = SentinelMetadata();
        ASSERT_TRUE(recorder.QueryPassDebugMetadata(0, out));
        EXPECT_EQ(out.tags, 20ULL);
        // The stale, cycle-1 second entry must never leak through.
        EXPECT_FALSE(recorder.QueryPassDebugMetadata(1, out));
    }

    // Cycle 3.
    recorder.BeginFrame();
    recorder.OnPassDeclared(0, RenderPassCategory::FrameDebuggerInternal, RenderPassDrawKind::Blit, /*tags=*/30ULL);
    recorder.OnPassDeclared(1, RenderPassCategory::FrameDebuggerInternal, RenderPassDrawKind::Blit, /*tags=*/31ULL);
    recorder.OnPassDeclared(2, RenderPassCategory::FrameDebuggerInternal, RenderPassDrawKind::Blit, /*tags=*/32ULL);
    ASSERT_EQ(recorder.EntryCountForTesting(), 3u);
    {
        PassDebugMetadata out = SentinelMetadata();
        ASSERT_TRUE(recorder.QueryPassDebugMetadata(0, out));
        EXPECT_EQ(out.tags, 30ULL);
        ASSERT_TRUE(recorder.QueryPassDebugMetadata(1, out));
        EXPECT_EQ(out.tags, 31ULL);
        ASSERT_TRUE(recorder.QueryPassDebugMetadata(2, out));
        EXPECT_EQ(out.tags, 32ULL);
    }
}

TEST(FrameDebuggerPassMetadataRecorderTest, BarrierLabelRoundTripsForSingleWrite)
{
    FrameDebuggerPassMetadataRecorder recorder;
    recorder.OnPassDeclared(0, RenderPassCategory::General, RenderPassDrawKind::DrawMesh, /*tags=*/0ULL);

    recorder.OnResourceBarrierApplied(0, "HDR_Scene", VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);

    std::string label;
    ASSERT_TRUE(recorder.QueryBarrierTransitionLabel(0, "HDR_Scene", label));
    EXPECT_EQ(label, "Undefined -> RT");
}

TEST(FrameDebuggerPassMetadataRecorderTest, QueryWithNoBarrierAppliedReturnsFalse)
{
    FrameDebuggerPassMetadataRecorder recorder;
    recorder.OnPassDeclared(0, RenderPassCategory::General, RenderPassDrawKind::DrawMesh, /*tags=*/0ULL);

    std::string label = "untouched";
    EXPECT_FALSE(recorder.QueryBarrierTransitionLabel(0, "G_Color", label));
    EXPECT_EQ(label, "untouched");
}

// Reproduces the real bug: one pass writes the SAME resource name twice
// (color attachment + depth-stencil attachment of one combined render
// target). Each write must keep its own, independently correct label -
// never both rows reporting whichever barrier was recorded last.
TEST(FrameDebuggerPassMetadataRecorderTest, DuplicateResourceNameWritesKeepIndependentLabels)
{
    FrameDebuggerPassMetadataRecorder recorder;
    recorder.OnPassDeclared(0, RenderPassCategory::General, RenderPassDrawKind::DrawMesh, /*tags=*/0ULL);

    // Color attachment write, applied first.
    recorder.OnResourceBarrierApplied(0, "SceneView", VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);
    // Depth-stencil attachment write to the SAME resolved name, applied second.
    recorder.OnResourceBarrierApplied(
        0, "SceneView", VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL);

    // Queried in the same order RenderGraphSnapshot.cpp's write loop queries them.
    std::string colorLabel;
    ASSERT_TRUE(recorder.QueryBarrierTransitionLabel(0, "SceneView", colorLabel));
    EXPECT_EQ(colorLabel, "Undefined -> RT");

    std::string depthLabel;
    ASSERT_TRUE(recorder.QueryBarrierTransitionLabel(0, "SceneView", depthLabel));
    EXPECT_EQ(depthLabel, "Undefined -> Depth Test");

    // A third query for the same name, with nothing left queued, is a clean miss.
    std::string thirdLabel = "untouched";
    EXPECT_FALSE(recorder.QueryBarrierTransitionLabel(0, "SceneView", thirdLabel));
    EXPECT_EQ(thirdLabel, "untouched");
}

TEST(FrameDebuggerPassMetadataRecorderTest, BarrierLabelsDoNotLeakAcrossDeclarationIndices)
{
    FrameDebuggerPassMetadataRecorder recorder;
    recorder.OnPassDeclared(0, RenderPassCategory::General, RenderPassDrawKind::DrawMesh, /*tags=*/0ULL);
    recorder.OnPassDeclared(1, RenderPassCategory::General, RenderPassDrawKind::DrawMesh, /*tags=*/0ULL);

    recorder.OnResourceBarrierApplied(0, "SceneView", VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);

    std::string label;
    EXPECT_FALSE(recorder.QueryBarrierTransitionLabel(1, "SceneView", label));
}

} // namespace
} // namespace gte
