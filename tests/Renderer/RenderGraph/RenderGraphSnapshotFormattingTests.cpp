// Unit tests for editor-core-separation-7 campaign, PHASE1
// (PHASE1_EXTRACT_FORMATTING_AND_LABEL_HELPERS.md) - pure, ImGui-free
// presentation-formatting helpers relocated (FormatGpuTiming()/JoinNames()/
// ResolvePassNameAtSurvivingIndex()) or newly added (ToString(ResourceKind)/
// ToString(ViewScope)) to
// src/Renderer/RenderGraph/RenderGraphSnapshotFormatting.h/.cpp. None of
// these had any test coverage before this phase - they were anonymous-
// namespace, static-linkage functions inside RenderGraphPanel.cpp,
// unreachable from any test binary. No live VkDevice/Renderer/RenderGraph
// involved at all - mirrors RenderGraphSnapshotTests.cpp's own
// include/namespace/test-registration style exactly.

#include "Renderer/RenderGraph/RenderGraphSnapshotFormatting.h"

#include <gtest/gtest.h>

#include <string>
#include <vector>

namespace gte::rg {
namespace {

// --- FormatGpuTiming() -------------------------------------------------------

TEST(RenderGraphSnapshotFormattingTest, FormatGpuTimingPresentFormatsMillisecondsToTwoDecimalPlaces)
{
    GpuTimingSample timing;
    timing.status = GpuTimingSample::Status::Present;
    timing.milliseconds = 3.14159;

    EXPECT_EQ(FormatGpuTiming(timing), "3.14 ms");
}

TEST(RenderGraphSnapshotFormattingTest, FormatGpuTimingUnsupportedReportsUnsupported)
{
    GpuTimingSample timing;
    timing.status = GpuTimingSample::Status::Unsupported;

    EXPECT_EQ(FormatGpuTiming(timing), "Unsupported");
}

TEST(RenderGraphSnapshotFormattingTest, FormatGpuTimingAbsentReportsNA)
{
    GpuTimingSample timing;
    timing.status = GpuTimingSample::Status::Absent;

    EXPECT_EQ(FormatGpuTiming(timing), "N/A");
}

// --- JoinNames() -------------------------------------------------------------

TEST(RenderGraphSnapshotFormattingTest, JoinNamesOfEmptyVectorReturnsDash)
{
    EXPECT_EQ(JoinNames({}), "-");
}

TEST(RenderGraphSnapshotFormattingTest, JoinNamesOfOneNameReturnsThatNameUnchanged)
{
    EXPECT_EQ(JoinNames({ "Albedo" }), "Albedo");
}

TEST(RenderGraphSnapshotFormattingTest, JoinNamesOfTwoNamesJoinsWithCommaSpace)
{
    EXPECT_EQ(JoinNames({ "Albedo", "Normal" }), "Albedo, Normal");
}

TEST(RenderGraphSnapshotFormattingTest, JoinNamesTreatsEmptyStringNameAsUnnamed)
{
    EXPECT_EQ(JoinNames({ "Albedo", "" }), "Albedo, (unnamed)");
}

// --- ResolvePassNameAtSurvivingIndex() ---------------------------------------

namespace {

RenderGraphSnapshot MakeSnapshotWithTwoPasses(const std::string& secondPassName)
{
    RenderGraphSnapshot snapshot;

    RenderGraphPassSnapshot first;
    first.name = "RenderOpaque";
    snapshot.passesInExecutionOrder.push_back(first);

    RenderGraphPassSnapshot second;
    second.name = secondPassName;
    snapshot.passesInExecutionOrder.push_back(second);

    return snapshot;
}

} // namespace

TEST(RenderGraphSnapshotFormattingTest, ResolvePassNameAtSurvivingIndexNegativeIndexReturnsQuestionMark)
{
    const RenderGraphSnapshot snapshot = MakeSnapshotWithTwoPasses("DrawSkyBackground");

    EXPECT_STREQ(ResolvePassNameAtSurvivingIndex(snapshot, -1), "?");
}

TEST(RenderGraphSnapshotFormattingTest, ResolvePassNameAtSurvivingIndexOutOfRangeIndexReturnsQuestionMark)
{
    const RenderGraphSnapshot snapshot = MakeSnapshotWithTwoPasses("DrawSkyBackground");

    EXPECT_STREQ(ResolvePassNameAtSurvivingIndex(snapshot, 2), "?");
}

TEST(RenderGraphSnapshotFormattingTest, ResolvePassNameAtSurvivingIndexValidIndexReturnsRealName)
{
    const RenderGraphSnapshot snapshot = MakeSnapshotWithTwoPasses("DrawSkyBackground");

    EXPECT_STREQ(ResolvePassNameAtSurvivingIndex(snapshot, 0), "RenderOpaque");
    EXPECT_STREQ(ResolvePassNameAtSurvivingIndex(snapshot, 1), "DrawSkyBackground");
}

TEST(RenderGraphSnapshotFormattingTest, ResolvePassNameAtSurvivingIndexEmptyNameReturnsUnnamed)
{
    const RenderGraphSnapshot snapshot = MakeSnapshotWithTwoPasses("");

    EXPECT_STREQ(ResolvePassNameAtSurvivingIndex(snapshot, 1), "(unnamed)");
}

// --- ToString(ResourceKind) ---------------------------------------------------
// One assertion per CURRENT enumerator (confirmed by directly reading
// RenderGraphTypes.h before writing this test - Texture/Buffer/VolumeTexture,
// exactly 3 today) - each must return a distinct, non-null, non-empty string.

TEST(RenderGraphSnapshotFormattingTest, ToStringResourceKindReturnsDistinctNonEmptyStringsForEveryEnumerator)
{
    const char* textureStr = ToString(ResourceKind::Texture);
    const char* bufferStr = ToString(ResourceKind::Buffer);
    const char* volumeTextureStr = ToString(ResourceKind::VolumeTexture);

    ASSERT_NE(textureStr, nullptr);
    ASSERT_NE(bufferStr, nullptr);
    ASSERT_NE(volumeTextureStr, nullptr);

    EXPECT_STRNE(textureStr, "");
    EXPECT_STRNE(bufferStr, "");
    EXPECT_STRNE(volumeTextureStr, "");

    EXPECT_STREQ(textureStr, "Texture");
    EXPECT_STREQ(bufferStr, "Buffer");
    EXPECT_STREQ(volumeTextureStr, "VolumeTexture");

    EXPECT_STRNE(textureStr, bufferStr);
    EXPECT_STRNE(textureStr, volumeTextureStr);
    EXPECT_STRNE(bufferStr, volumeTextureStr);
}

// --- ToString(ViewScope) -------------------------------------------------------
// One assertion per CURRENT enumerator (Shared/GameView/SceneView, exactly 3
// today, confirmed the same way) - same shape as the ResourceKind test above.

TEST(RenderGraphSnapshotFormattingTest, ToStringViewScopeReturnsDistinctNonEmptyStringsForEveryEnumerator)
{
    const char* sharedStr = ToString(ViewScope::Shared);
    const char* gameViewStr = ToString(ViewScope::GameView);
    const char* sceneViewStr = ToString(ViewScope::SceneView);

    ASSERT_NE(sharedStr, nullptr);
    ASSERT_NE(gameViewStr, nullptr);
    ASSERT_NE(sceneViewStr, nullptr);

    EXPECT_STRNE(sharedStr, "");
    EXPECT_STRNE(gameViewStr, "");
    EXPECT_STRNE(sceneViewStr, "");

    EXPECT_STREQ(sharedStr, "Shared");
    EXPECT_STREQ(gameViewStr, "GameView");
    EXPECT_STREQ(sceneViewStr, "SceneView");

    EXPECT_STRNE(sharedStr, gameViewStr);
    EXPECT_STRNE(sharedStr, sceneViewStr);
    EXPECT_STRNE(gameViewStr, sceneViewStr);
}

} // namespace
} // namespace gte::rg
