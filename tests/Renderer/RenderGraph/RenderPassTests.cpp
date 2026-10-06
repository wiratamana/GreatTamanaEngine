// Unit tests for RenderGraphBuilder::AddRenderPass() - a thin wrapper around
// AddPass()/AddComputePass() that additionally stamps PassRecord::kind/
// category in one place. No live VkDevice/Renderer/Registry involved at all.
//
// Assertions on pass category/drawKind/tags go through a small, test-local,
// dual-interface fake sink (mirroring RenderGraphSnapshotTests.cpp's own
// FakeMetadataSink) rather than reading PassRecord fields directly, since
// PassRecord itself no longer stores any of the three.

#include "Renderer/RenderGraph/RenderGraphBuilder.h"
#include "Renderer/RenderGraph/RenderGraphDebugMetadataSink.h"

#include <gtest/gtest.h>

namespace gte::rg {
namespace {

// A domain-agnostic tag bit used purely as fixture data for tests below -
// unrelated to any specific render feature.
constexpr RenderPassTag kTestLutPassTag{ 1ull << 0 };

void NoOpExecute(PassContext&) { }

// A minimal, test-local, dual-interface fake sink, mirroring
// RenderGraphSnapshotTests.cpp's own FakeMetadataSink exactly (see that file
// for the full rationale).
class FakeMetadataSink : public IPassDebugMetadataSink, public IPassDebugMetadataProvider {
public:
    void OnPassDeclared(std::size_t declarationIndexThisFrame, RenderPassCategory category, RenderPassDrawKind drawKind,
        RenderPassTagMask tags) override
    {
        ASSERT_EQ(declarationIndexThisFrame, m_table.size());
        m_table.push_back(PassDebugMetadata{ category, drawKind, tags });
    }

    void BeginFrame() override { m_table.clear(); }

    bool QueryPassDebugMetadata(std::size_t declarationIndex, PassDebugMetadata& outMetadata) const override
    {
        if (declarationIndex >= m_table.size()) {
            return false;
        }
        outMetadata = m_table[declarationIndex];
        return true;
    }

private:
    std::vector<PassDebugMetadata> m_table;
};

// --- AddRenderPass() with PassKind::Graphics ------------------------------

TEST(RenderPassTest, AddRenderPassWithGraphicsKindRunsSetupAndStampsGraphicsKind)
{
    RenderGraphBuilder builder;
    FakeMetadataSink sink;
    builder.SetDebugMetadataSink(&sink);
    int setupCallCount = 0;

    builder.AddRenderPass(
        "RenderOpaque", PassKind::Graphics,
        [&](RenderGraphBuilder::PassBuilder&) { ++setupCallCount; },
        NoOpExecute);

    EXPECT_EQ(setupCallCount, 1);

    const CompiledGraphInput input = builder.Finish();
    ASSERT_EQ(input.passes.size(), 1u);
    EXPECT_STREQ(input.passes[0].name, "RenderOpaque");
    EXPECT_EQ(input.passes[0].kind, PassKind::Graphics);
    EXPECT_EQ(input.passes[0].viewScope, ViewScope::Shared);

    PassDebugMetadata metadata;
    ASSERT_TRUE(sink.QueryPassDebugMetadata(0, metadata));
    EXPECT_EQ(metadata.category, RenderPassCategory::General);
}

// --- AddRenderPass() with PassKind::Compute -------------------------------

TEST(RenderPassTest, AddRenderPassWithComputeKindRunsSetupAndStampsComputeKind)
{
    RenderGraphBuilder builder;
    const TextureHandle handle = builder.CreateTexture("Output", TextureDesc{ 64, 64, VK_FORMAT_R8G8B8A8_UNORM, false });
    int setupCallCount = 0;

    builder.AddRenderPass(
        "AtmosphereTransmittanceLutPass", PassKind::Compute,
        [&](RenderGraphBuilder::PassBuilder& pass) {
            ++setupCallCount;
            pass.WriteTexture(handle);
        },
        NoOpExecute);

    EXPECT_EQ(setupCallCount, 1);

    const CompiledGraphInput input = builder.Finish();
    ASSERT_EQ(input.passes.size(), 1u);
    EXPECT_STREQ(input.passes[0].name, "AtmosphereTransmittanceLutPass");
    EXPECT_EQ(input.passes[0].kind, PassKind::Compute);
    ASSERT_EQ(input.passes[0].writes.size(), 1u);
    EXPECT_EQ(input.passes[0].writes[0].access, ResourceAccess::ComputeShaderWrite);
}

// --- 4-argument AddRenderPass() overload: explicit ViewScope + category ---

TEST(RenderPassTest, AddRenderPassFourArgumentOverloadStampsViewScopeCategoryAndTags)
{
    RenderGraphBuilder builder;
    FakeMetadataSink sink;
    builder.SetDebugMetadataSink(&sink);

    builder.AddRenderPass(
        "AtmosphereSkyViewLutPass", PassKind::Compute, ViewScope::GameView, RenderPassCategory::General,
        [](RenderGraphBuilder::PassBuilder&) { }, NoOpExecute, RenderPassDrawKind::DrawMesh,
        RenderPassEvent::Opaques, kTestLutPassTag.bit);

    const CompiledGraphInput input = builder.Finish();
    ASSERT_EQ(input.passes.size(), 1u);
    EXPECT_STREQ(input.passes[0].name, "AtmosphereSkyViewLutPass");
    EXPECT_EQ(input.passes[0].kind, PassKind::Compute);
    EXPECT_EQ(input.passes[0].viewScope, ViewScope::GameView);

    PassDebugMetadata metadata;
    ASSERT_TRUE(sink.QueryPassDebugMetadata(0, metadata));
    EXPECT_EQ(metadata.category, RenderPassCategory::General);
    EXPECT_EQ(metadata.tags, kTestLutPassTag.bit);
}

TEST(RenderPassTest, AddRenderPassFourArgumentOverloadWorksForGraphicsKindToo)
{
    RenderGraphBuilder builder;
    FakeMetadataSink sink;
    builder.SetDebugMetadataSink(&sink);

    builder.AddRenderPass(
        "DrawSkyBackground", PassKind::Graphics, ViewScope::GameView, RenderPassCategory::General,
        [](RenderGraphBuilder::PassBuilder&) { }, NoOpExecute);

    const CompiledGraphInput input = builder.Finish();
    ASSERT_EQ(input.passes.size(), 1u);
    EXPECT_EQ(input.passes[0].kind, PassKind::Graphics);
    EXPECT_EQ(input.passes[0].viewScope, ViewScope::GameView);

    PassDebugMetadata metadata;
    ASSERT_TRUE(sink.QueryPassDebugMetadata(0, metadata));
    EXPECT_EQ(metadata.category, RenderPassCategory::General);
}

// execute is captured but never invoked by AddRenderPass()/Finish() -
// mirrors AddPass()'s own equivalent guarantee (RenderGraphBuilderTests.cpp).
TEST(RenderPassTest, AddRenderPassExecuteIsNeverInvokedByAddRenderPassOrFinish)
{
    RenderGraphBuilder builder;
    int executeCallCount = 0;

    builder.AddRenderPass(
        "TestPass", PassKind::Graphics,
        [](RenderGraphBuilder::PassBuilder&) { },
        [&](PassContext&) { ++executeCallCount; });

    EXPECT_EQ(executeCallCount, 0);

    const CompiledGraphInput input = builder.Finish();
    EXPECT_EQ(executeCallCount, 0);
    ASSERT_EQ(input.passes.size(), 1u);
    EXPECT_TRUE(static_cast<bool>(input.passes[0].execute));
}

// --- Frame Debugger Pass-Ownership campaign (task_manager/render-pass-2),
// PHASE1 - new trailing, defaulted RenderPassDrawKind parameter -----------

// The 6-argument overload defaults its new trailing drawKind parameter to
// RenderPassDrawKind::DrawMesh when the caller omits it entirely - every
// pre-existing 6-argument call site across src/ relies on exactly this.
TEST(RenderPassTest, AddRenderPassSixArgumentOverloadDefaultsDrawKindToDrawMeshWhenOmitted)
{
    RenderGraphBuilder builder;
    FakeMetadataSink sink;
    builder.SetDebugMetadataSink(&sink);

    builder.AddRenderPass(
        "RenderOpaque", PassKind::Graphics, ViewScope::GameView, RenderPassCategory::General,
        [](RenderGraphBuilder::PassBuilder&) { }, NoOpExecute);

    const CompiledGraphInput input = builder.Finish();
    ASSERT_EQ(input.passes.size(), 1u);

    PassDebugMetadata metadata;
    ASSERT_TRUE(sink.QueryPassDebugMetadata(0, metadata));
    EXPECT_EQ(metadata.drawKind, RenderPassDrawKind::DrawMesh);
}

// The 6-argument overload stores exactly the drawKind value explicitly
// passed as its new trailing argument - mirrors AddDrawSkyBackgroundPass()'s
// own real RenderPassDrawKind::DrawQuad call site (src/Application/RenderPasses.cpp).
TEST(RenderPassTest, AddRenderPassSixArgumentOverloadStoresExplicitDrawKind)
{
    RenderGraphBuilder builder;
    FakeMetadataSink sink;
    builder.SetDebugMetadataSink(&sink);

    builder.AddRenderPass(
        "DrawSkyBackground", PassKind::Graphics, ViewScope::GameView, RenderPassCategory::General,
        [](RenderGraphBuilder::PassBuilder&) { }, NoOpExecute, RenderPassDrawKind::DrawQuad);

    const CompiledGraphInput input = builder.Finish();
    ASSERT_EQ(input.passes.size(), 1u);

    PassDebugMetadata metadata;
    ASSERT_TRUE(sink.QueryPassDebugMetadata(0, metadata));
    EXPECT_EQ(metadata.drawKind, RenderPassDrawKind::DrawQuad);
}

// The 4-argument convenience overload also defaults its new trailing
// drawKind parameter to RenderPassDrawKind::DrawMesh when omitted.
TEST(RenderPassTest, AddRenderPassFourArgumentOverloadDefaultsDrawKindToDrawMeshWhenOmitted)
{
    RenderGraphBuilder builder;
    FakeMetadataSink sink;
    builder.SetDebugMetadataSink(&sink);

    builder.AddRenderPass(
        "TestPass", PassKind::Graphics, [](RenderGraphBuilder::PassBuilder&) { }, NoOpExecute);

    const CompiledGraphInput input = builder.Finish();
    ASSERT_EQ(input.passes.size(), 1u);

    PassDebugMetadata metadata;
    ASSERT_TRUE(sink.QueryPassDebugMetadata(0, metadata));
    EXPECT_EQ(metadata.drawKind, RenderPassDrawKind::DrawMesh);
}

// The 4-argument convenience overload stores exactly the drawKind value
// explicitly passed as its new trailing argument.
TEST(RenderPassTest, AddRenderPassFourArgumentOverloadStoresExplicitDrawKind)
{
    RenderGraphBuilder builder;
    FakeMetadataSink sink;
    builder.SetDebugMetadataSink(&sink);

    builder.AddRenderPass(
        "TestPass", PassKind::Graphics, [](RenderGraphBuilder::PassBuilder&) { }, NoOpExecute,
        RenderPassDrawKind::DrawQuad);

    const CompiledGraphInput input = builder.Finish();
    ASSERT_EQ(input.passes.size(), 1u);

    PassDebugMetadata metadata;
    ASSERT_TRUE(sink.QueryPassDebugMetadata(0, metadata));
    EXPECT_EQ(metadata.drawKind, RenderPassDrawKind::DrawQuad);
}

// --- render-pass-7 campaign (task_manager/render-pass-7), PHASE1 - new
// trailing, defaulted RenderPassTagMask parameter -------------------------

// The full (now 9-argument) overload stamps an explicit, non-zero `tags`
// argument onto the resulting PassRecord alongside every other field this
// overload already stamps - mirrors
// AddRenderPassFourArgumentOverloadStampsViewScopeAndCategory's own fixture
// shape.
TEST(RenderPassTest, AddRenderPassNineArgumentOverloadStampsTagsAlongsideEveryOtherField)
{
    RenderGraphBuilder builder;
    FakeMetadataSink sink;
    builder.SetDebugMetadataSink(&sink);

    builder.AddRenderPass(
        "AtmosphereSkyViewLutPass", PassKind::Compute, ViewScope::GameView, RenderPassCategory::General,
        [](RenderGraphBuilder::PassBuilder&) { }, NoOpExecute, RenderPassDrawKind::DrawQuad,
        RenderPassEvent::PreOpaques, RenderPassTagMask{ 0x4u });

    const CompiledGraphInput input = builder.Finish();
    ASSERT_EQ(input.passes.size(), 1u);
    EXPECT_EQ(input.passes[0].viewScope, ViewScope::GameView);
    EXPECT_EQ(input.passes[0].renderPassEvent, RenderPassEvent::PreOpaques);

    PassDebugMetadata metadata;
    ASSERT_TRUE(sink.QueryPassDebugMetadata(0, metadata));
    EXPECT_EQ(metadata.category, RenderPassCategory::General);
    EXPECT_EQ(metadata.drawKind, RenderPassDrawKind::DrawQuad);
    EXPECT_EQ(metadata.tags, RenderPassTagMask{ 0x4u });
}

// The full (now 9-argument) overload defaults its new trailing `tags`
// parameter to 0 when the caller omits it entirely - every pre-existing
// call site of this overload relies on exactly this.
TEST(RenderPassTest, AddRenderPassNineArgumentOverloadDefaultsTagsToZeroWhenOmitted)
{
    RenderGraphBuilder builder;
    FakeMetadataSink sink;
    builder.SetDebugMetadataSink(&sink);

    builder.AddRenderPass(
        "RenderOpaque", PassKind::Graphics, ViewScope::GameView, RenderPassCategory::General,
        [](RenderGraphBuilder::PassBuilder&) { }, NoOpExecute);

    const CompiledGraphInput input = builder.Finish();
    ASSERT_EQ(input.passes.size(), 1u);

    PassDebugMetadata metadata;
    ASSERT_TRUE(sink.QueryPassDebugMetadata(0, metadata));
    EXPECT_EQ(metadata.tags, RenderPassTagMask{ 0 });
}

// The convenience (now 7-argument) overload forwards an explicit `tags`
// value all the way through into the underlying PassRecord - proving it
// genuinely FORWARDS the argument, not just defaults it.
TEST(RenderPassTest, AddRenderPassSevenArgumentOverloadStoresExplicitTags)
{
    RenderGraphBuilder builder;
    FakeMetadataSink sink;
    builder.SetDebugMetadataSink(&sink);

    builder.AddRenderPass(
        "TestPass", PassKind::Graphics, [](RenderGraphBuilder::PassBuilder&) { }, NoOpExecute,
        RenderPassDrawKind::DrawMesh, RenderPassEvent::Opaques, RenderPassTagMask{ 0x8u });

    const CompiledGraphInput input = builder.Finish();
    ASSERT_EQ(input.passes.size(), 1u);

    PassDebugMetadata metadata;
    ASSERT_TRUE(sink.QueryPassDebugMetadata(0, metadata));
    EXPECT_EQ(metadata.tags, RenderPassTagMask{ 0x8u });
}

// The convenience (now 7-argument) overload also defaults its new trailing
// `tags` parameter to 0 when omitted.
TEST(RenderPassTest, AddRenderPassSevenArgumentOverloadDefaultsTagsToZeroWhenOmitted)
{
    RenderGraphBuilder builder;
    FakeMetadataSink sink;
    builder.SetDebugMetadataSink(&sink);

    builder.AddRenderPass(
        "TestPass", PassKind::Graphics, [](RenderGraphBuilder::PassBuilder&) { }, NoOpExecute);

    const CompiledGraphInput input = builder.Finish();
    ASSERT_EQ(input.passes.size(), 1u);

    PassDebugMetadata metadata;
    ASSERT_TRUE(sink.QueryPassDebugMetadata(0, metadata));
    EXPECT_EQ(metadata.tags, RenderPassTagMask{ 0 });
}

} // namespace
} // namespace gte::rg
