// Tier-1 tests for the ImGui-free formatting helpers in src/Renderer/RenderGraph/RenderGraphSnapshotFormatting.h:
// FormatGpuTiming(), ResolvePassNameAtSurvivingIndex(), ToString(ResourceKind), ToString(ViewScope).
// No live VkDevice/Renderer involved.

#include "Renderer/RenderGraph/RenderGraphSnapshotFormatting.h"

#include <gtest/gtest.h>

#include <string>
#include <vector>

namespace gte::rg {
namespace {

// --- FormatGpuTiming() -------------------------------------------------------

TEST(RenderGraphSnapshotFormattingTest, FormatGpuTimingPresentSumsBarrierAndWorkToTwoDecimalPlaces)
{
    GpuPassRecord record;
    record.state = GpuReadState::Present;
    record.barrierMs = 0.14159f;
    record.workMs = 3.0f;

    EXPECT_EQ(FormatGpuTiming(record), "3.14 ms");
}

TEST(RenderGraphSnapshotFormattingTest, FormatGpuTimingPartialIsMarkedAsPartial)
{
    GpuPassRecord record;
    record.state = GpuReadState::Partial;
    record.workMs = 1.5f;

    EXPECT_EQ(FormatGpuTiming(record), "~1.50 ms (partial)");
}

TEST(RenderGraphSnapshotFormattingTest, FormatGpuTimingNeverPrintsANumberForAnUnmeasuredPass)
{
    GpuPassRecord record;
    record.workMs = 9.0f;
    const GpuReadState states[] = { GpuReadState::Absent, GpuReadState::Error, GpuReadState::Unsupported,
        GpuReadState::Inactive };
    const char* words[] = { "Absent", "Error", "Unsupported", "Inactive" };
    for (std::size_t i = 0; i < 4; ++i) {
        record.state = states[i];
        EXPECT_EQ(FormatGpuTiming(record), words[i]);
        EXPECT_STREQ(ToString(states[i]), words[i]);
    }
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
