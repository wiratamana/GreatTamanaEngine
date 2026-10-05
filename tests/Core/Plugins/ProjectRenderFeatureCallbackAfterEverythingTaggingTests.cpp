// Tier-1 tests for FindPassesNotTaggedAfterEverything()
// (src/Core/Plugins/ProjectRenderFeatureCallback.h) - mirrors
// PreOpaqueStageOrderingTests.cpp's own established shape for the PreOpaque
// sibling of this exact safety-net pattern. Pure RenderGraphBuilder usage,
// no live VkDevice/Core/RenderFeatureCompositor involved.

#include "Core/Plugins/ProjectRenderFeatureCallback.h"

#include <gtest/gtest.h>

namespace gte {
namespace {

void NoOpExecute(rg::PassContext&) { }

rg::TextureDesc MakeTextureDesc()
{
    return rg::TextureDesc{ 64, 64, VK_FORMAT_R8G8B8A8_UNORM, false };
}

} // namespace

TEST(FindPassesNotTaggedAfterEverythingTest, ReportsEmptyForACorrectlyTaggedPass)
{
    rg::RenderGraphBuilder builder;
    const rg::TextureHandle t0 = builder.CreateTexture("T0", MakeTextureDesc());

    const std::size_t before = builder.DeclaredPassCount();
    builder.AddRenderPass(
        "MyFeature.Composite", rg::PassKind::Graphics,
        [&](rg::RenderGraphBuilder::PassBuilder& pass) { pass.WriteColorAttachment(t0); }, NoOpExecute,
        rg::RenderPassDrawKind::DrawMesh, rg::RenderPassEvent::AfterEverything);
    const std::size_t after = builder.DeclaredPassCount();

    EXPECT_TRUE(FindPassesNotTaggedAfterEverything(builder, before, after).empty());
}

// Proves the mistagged-but-falls-back-to-implicit-default mistake is caught:
// AddRenderPass()'s own implicit default (no trailing RenderPassEvent
// argument) is RenderPassEvent::Opaques, never AfterEverything.
TEST(FindPassesNotTaggedAfterEverythingTest, CatchesTheForgottenDefaultTagMistake)
{
    rg::RenderGraphBuilder builder;
    const rg::TextureHandle t0 = builder.CreateTexture("T0", MakeTextureDesc());

    const std::size_t before = builder.DeclaredPassCount();
    builder.AddRenderPass(
        "MyFeature.ForgotToTag", rg::PassKind::Graphics,
        [&](rg::RenderGraphBuilder::PassBuilder& pass) { pass.WriteColorAttachment(t0); }, NoOpExecute);
    const std::size_t after = builder.DeclaredPassCount();

    const std::vector<std::size_t> violations = FindPassesNotTaggedAfterEverything(builder, before, after);
    ASSERT_EQ(violations.size(), 1u);
    EXPECT_EQ(violations[0], before);
    EXPECT_EQ(builder.PassEventAt(violations[0]), rg::RenderPassEvent::Opaques);
}

// A mixed case - one correctly-tagged pass alongside one mistagged pass
// within the SAME [before, after) range - only the mistagged one is
// reported.
TEST(FindPassesNotTaggedAfterEverythingTest, ReportsOnlyTheOffendingIndexInAMixedRange)
{
    rg::RenderGraphBuilder builder;
    const rg::TextureHandle t0 = builder.CreateTexture("T0", MakeTextureDesc());
    const rg::TextureHandle t1 = builder.CreateTexture("T1", MakeTextureDesc());

    const std::size_t before = builder.DeclaredPassCount();
    builder.AddRenderPass(
        "MyFeature.CorrectlyTagged", rg::PassKind::Graphics,
        [&](rg::RenderGraphBuilder::PassBuilder& pass) { pass.WriteColorAttachment(t0); }, NoOpExecute,
        rg::RenderPassDrawKind::DrawMesh, rg::RenderPassEvent::AfterEverything); // index `before` - correct.
    builder.AddRenderPass(
        "MyFeature.CopyToPrivateTargetMistaggedEarlier", rg::PassKind::Graphics,
        [&](rg::RenderGraphBuilder::PassBuilder& pass) { pass.WriteColorAttachment(t1); }, NoOpExecute,
        rg::RenderPassDrawKind::DrawMesh, rg::RenderPassEvent::AfterTransparents); // index `before` + 1 - wrong.
    const std::size_t after = builder.DeclaredPassCount();

    const std::vector<std::size_t> violations = FindPassesNotTaggedAfterEverything(builder, before, after);
    ASSERT_EQ(violations.size(), 1u);
    EXPECT_EQ(violations[0], before + 1);
}

// Passes OUTSIDE the [before, after) range must never be inspected, even if
// mistagged themselves.
TEST(FindPassesNotTaggedAfterEverythingTest, PassesOutsideTheGivenRangeAreNeverInspected)
{
    rg::RenderGraphBuilder builder;
    const rg::TextureHandle tOutside = builder.CreateTexture("TOutside", MakeTextureDesc());
    const rg::TextureHandle tInside = builder.CreateTexture("TInside", MakeTextureDesc());

    builder.AddRenderPass(
        "UnrelatedMistaggedPass", rg::PassKind::Graphics,
        [&](rg::RenderGraphBuilder::PassBuilder& pass) { pass.WriteColorAttachment(tOutside); }, NoOpExecute);

    const std::size_t before = builder.DeclaredPassCount();
    builder.AddRenderPass(
        "MyFeature.CorrectlyTagged", rg::PassKind::Graphics,
        [&](rg::RenderGraphBuilder::PassBuilder& pass) { pass.WriteColorAttachment(tInside); }, NoOpExecute,
        rg::RenderPassDrawKind::DrawMesh, rg::RenderPassEvent::AfterEverything);
    const std::size_t after = builder.DeclaredPassCount();

    EXPECT_TRUE(FindPassesNotTaggedAfterEverything(builder, before, after).empty());
}

// An empty [before, after) range (a callback that legitimately declared
// nothing) is never a violation.
TEST(FindPassesNotTaggedAfterEverythingTest, EmptyRangeIsNeverAViolation)
{
    rg::RenderGraphBuilder builder;
    const std::size_t before = builder.DeclaredPassCount();
    const std::size_t after = builder.DeclaredPassCount();

    EXPECT_TRUE(FindPassesNotTaggedAfterEverything(builder, before, after).empty());
}

} // namespace gte
