// Tier 1 tests of the Render Graph panel display text: GPU timing text, view labels and pass grouping.

#include "Application/ViewScopeLabel.h"
#include "Editor/RenderGraphDisplayText.h"

#include "Renderer/RenderGraph/RenderGraphSnapshotFormatting.h"

#include <gtest/gtest.h>

#include <string>
#include <vector>

namespace gte {
namespace {

// --- FormatGpuTiming() -------------------------------------------------------

TEST(RenderGraphDisplayTextTest, FormatGpuTimingPresentSumsBarrierAndWorkToTwoDecimalPlaces)
{
    GpuPassRecord record;
    record.state = GpuReadState::Present;
    record.barrierMs = 0.14159f;
    record.workMs = 3.0f;

    EXPECT_EQ(FormatGpuTiming(record), "3.14 ms");
}

TEST(RenderGraphDisplayTextTest, FormatGpuTimingPartialIsMarkedAsPartial)
{
    GpuPassRecord record;
    record.state = GpuReadState::Partial;
    record.workMs = 1.5f;

    EXPECT_EQ(FormatGpuTiming(record), "~1.50 ms (partial)");
}

TEST(RenderGraphDisplayTextTest, FormatGpuTimingNeverPrintsANumberForAnUnmeasuredPass)
{
    GpuPassRecord record;
    record.workMs = 9.0f;
    const GpuReadState states[] = { GpuReadState::Absent, GpuReadState::Error,
        GpuReadState::Unsupported, GpuReadState::Inactive };
    const char* words[] = { "Absent", "Error", "Unsupported", "Inactive" };
    for (std::size_t i = 0; i < 4; ++i) {
        record.state = states[i];
        EXPECT_EQ(FormatGpuTiming(record), words[i]);
    }
}

TEST(RenderGraphDisplayTextTest, ViewScopeLabelMapsScopeToPanelLabels)
{
    EXPECT_STREQ(ViewScopeLabel(rg::ViewScope::Primary), "Game");
    EXPECT_STREQ(ViewScopeLabel(rg::ViewScope::Secondary), "Scene");
    EXPECT_STREQ(ViewScopeLabel(rg::ViewScope::Shared), "Shared");
}

// --- GroupPassMetadataByName() -----------------------------------------------

rg::RenderGraphPassMetadata MakePass(const std::string& name, rg::ViewScope scope, bool isCulled = false)
{
    rg::RenderGraphPassMetadata pass;
    pass.name = name;
    pass.viewScope = scope;
    pass.isCulled = isCulled;
    return pass;
}

void SetPresentTiming(rg::RenderGraphPassMetadata& pass, double milliseconds)
{
    pass.gpuTimingState = GpuReadState::Present;
    pass.gpuRecordedMilliseconds = milliseconds;
    pass.gpuTimingMilliseconds = milliseconds;
}

TEST(RenderGraphGroupedPassTest, EmptyInputProducesEmptyResult)
{
    EXPECT_TRUE(GroupPassMetadataByName({}).empty());
}

TEST(RenderGraphGroupedPassTest, TwoNonCulledInstancesGroupIntoOneRowWithSummedStatsAndBothViewsLabel)
{
    rg::RenderGraphPassMetadata primary = MakePass("RenderOpaque", rg::ViewScope::Primary);
    primary.drawCallCount = 10;
    primary.triangleCount = 100;
    SetPresentTiming(primary, 0.12);
    rg::RenderGraphPassMetadata secondary = MakePass("RenderOpaque", rg::ViewScope::Secondary);
    secondary.drawCallCount = 5;
    secondary.triangleCount = 50;
    SetPresentTiming(secondary, 0.08);

    const std::vector<RenderGraphGroupedPass> grouped = GroupPassMetadataByName({ primary, secondary });

    ASSERT_EQ(grouped.size(), 1u);
    const RenderGraphGroupedPass& group = grouped[0];
    EXPECT_EQ(group.name, "RenderOpaque");
    EXPECT_FALSE(group.isCulled);
    EXPECT_EQ(group.viewLabel, "Game+Scene");
    EXPECT_EQ(group.drawCallCount, 15u);
    EXPECT_EQ(group.triangleCount, 150u);
    EXPECT_EQ(group.gpuTimingText, "Game: 0.12 ms, Scene: 0.08 ms");
    ASSERT_EQ(group.instances.size(), 2u);
}

TEST(RenderGraphGroupedPassTest, OneCulledOneSurvivingInstanceGroupsToNonCulledWithSurvivingViewLabel)
{
    const rg::RenderGraphPassMetadata primary = MakePass("SkyLut", rg::ViewScope::Primary, /*isCulled=*/true);
    rg::RenderGraphPassMetadata secondary = MakePass("SkyLut", rg::ViewScope::Secondary);
    secondary.drawCallCount = 3;

    const std::vector<RenderGraphGroupedPass> grouped = GroupPassMetadataByName({ primary, secondary });

    ASSERT_EQ(grouped.size(), 1u);
    EXPECT_FALSE(grouped[0].isCulled);
    EXPECT_EQ(grouped[0].viewLabel, "Scene only");
    EXPECT_EQ(grouped[0].drawCallCount, 3u);
}

TEST(RenderGraphGroupedPassTest, BothInstancesCulledGroupsToCulledTrue)
{
    const rg::RenderGraphPassMetadata primary = MakePass("Unused", rg::ViewScope::Primary, /*isCulled=*/true);
    const rg::RenderGraphPassMetadata secondary = MakePass("Unused", rg::ViewScope::Secondary, /*isCulled=*/true);

    const std::vector<RenderGraphGroupedPass> grouped = GroupPassMetadataByName({ primary, secondary });

    ASSERT_EQ(grouped.size(), 1u);
    EXPECT_TRUE(grouped[0].isCulled);
    EXPECT_EQ(grouped[0].viewLabel, "Game+Scene");
}

TEST(RenderGraphGroupedPassTest, SingleInstanceKeepsItsOwnTimingTextAndSharedHasNoOnlySuffix)
{
    rg::RenderGraphPassMetadata primary = MakePass("Clear", rg::ViewScope::Primary);
    SetPresentTiming(primary, 0.05);
    const rg::RenderGraphPassMetadata shared = MakePass("Skinning", rg::ViewScope::Shared);

    const std::vector<RenderGraphGroupedPass> grouped = GroupPassMetadataByName({ primary, shared });

    ASSERT_EQ(grouped.size(), 2u);
    EXPECT_EQ(grouped[0].name, "Clear");
    EXPECT_EQ(grouped[0].viewLabel, "Game only");
    EXPECT_EQ(grouped[0].gpuTimingText, "0.05 ms");
    EXPECT_EQ(grouped[1].name, "Skinning");
    EXPECT_EQ(grouped[1].viewLabel, "Shared");
}

TEST(RenderGraphGroupedPassTest, GroupingIsDeterministicRegardlessOfInputOrderAndCombinesResourceNames)
{
    rg::RenderGraphPassMetadata alphaPrimary = MakePass("Alpha", rg::ViewScope::Primary);
    alphaPrimary.reads = { rg::RenderGraphResourceRefMetadata{ "Depth", "Texture" },
        rg::RenderGraphResourceRefMetadata{ "Common", "Buffer" } };
    rg::RenderGraphPassMetadata alphaSecondary = MakePass("Alpha", rg::ViewScope::Secondary);
    alphaSecondary.reads = { rg::RenderGraphResourceRefMetadata{ "Common", "Buffer" },
        rg::RenderGraphResourceRefMetadata{ "DepthOther", "Texture" } };
    const rg::RenderGraphPassMetadata betaPrimary = MakePass("Beta", rg::ViewScope::Primary);

    const std::vector<RenderGraphGroupedPass> one = GroupPassMetadataByName({ betaPrimary, alphaSecondary, alphaPrimary });
    const std::vector<RenderGraphGroupedPass> two = GroupPassMetadataByName({ alphaPrimary, betaPrimary, alphaSecondary });

    ASSERT_EQ(one.size(), 2u);
    ASSERT_EQ(two.size(), 2u);
    EXPECT_EQ(one[0].name, "Alpha");
    EXPECT_EQ(two[0].name, "Alpha");
    EXPECT_EQ(one[1].name, "Beta");
    EXPECT_EQ(one[0].viewLabel, two[0].viewLabel);
    ASSERT_EQ(two[0].reads.size(), 3u);
    EXPECT_EQ(two[0].reads[0], "Depth");
    EXPECT_EQ(two[0].reads[1], "Common");
    EXPECT_EQ(two[0].reads[2], "DepthOther");
}

} // namespace
} // namespace gte
