// Tier 1 tests of the ImGui-free ToString() and name helpers in RenderGraphSnapshotFormatting.h.
// No live VkDevice/Renderer involved.

#include "Renderer/RenderGraph/RenderGraphSnapshotFormatting.h"

#include <gtest/gtest.h>

#include <string>
#include <vector>

namespace gte::rg {
namespace {

// --- ToString(GpuReadState) --------------------------------------------------

TEST(RenderGraphSnapshotFormattingTest, ToStringGpuReadStateReturnsTheStatusWord)
{
    EXPECT_STREQ(ToString(GpuReadState::Present), "Present");
    EXPECT_STREQ(ToString(GpuReadState::Partial), "Partial");
    EXPECT_STREQ(ToString(GpuReadState::Absent), "Absent");
    EXPECT_STREQ(ToString(GpuReadState::Error), "Error");
    EXPECT_STREQ(ToString(GpuReadState::Unsupported), "Unsupported");
    EXPECT_STREQ(ToString(GpuReadState::Inactive), "Inactive");
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
// Each enumerator returns a distinct, non-empty string.

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

} // namespace
} // namespace gte::rg
