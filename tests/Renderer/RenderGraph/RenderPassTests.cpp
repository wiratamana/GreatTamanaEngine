// Unit tests for RenderGraphBuilder::AddRenderPass() - a thin wrapper around
// AddPass()/AddComputePass() that additionally stamps PassRecord::kind/
// category in one place. No live VkDevice/Renderer/Registry involved at all.
//
// Assertions on pass category/drawKind/owningFeatureName go through a small,
// test-local, dual-interface fake sink (mirroring RenderGraphSnapshotTests.cpp's
// own FakeMetadataSink) rather than reading PassRecord fields directly, since
// PassRecord itself no longer stores any of the three.

#include "Renderer/RenderGraph/RenderGraphBuilder.h"
#include "Renderer/RenderGraph/RenderGraphDebugMetadataSink.h"
#include "Renderer/RenderGraph/RenderFeatureScope.h"

#include <gtest/gtest.h>

namespace gte::rg {
namespace {

void NoOpExecute(PassContext&) { }

// A minimal, test-local, dual-interface fake sink, mirroring
// RenderGraphSnapshotTests.cpp's own FakeMetadataSink exactly (see that file
// for the full rationale).
class FakeMetadataSink : public IPassDebugMetadataSink, public IPassDebugMetadataProvider {
public:
    void OnPassDeclared(std::size_t declarationIndexThisFrame, RenderPassCategory category, RenderPassDrawKind drawKind,
        std::string_view owningFeatureName) override
    {
        ASSERT_EQ(declarationIndexThisFrame, m_table.size());
        m_table.push_back(PassDebugMetadata{ category, drawKind, std::string(owningFeatureName) });
    }

    void BeginFrame() override { m_table.clear(); }

    // Test-local stub - no test in this file exercises barrier labels.
    void OnResourceBarrierApplied(std::size_t, const std::string&, VkImageLayout, VkImageLayout) override {}

    bool QueryPassDebugMetadata(std::size_t declarationIndex, PassDebugMetadata& outMetadata) const override
    {
        if (declarationIndex >= m_table.size()) {
            return false;
        }
        outMetadata = m_table[declarationIndex];
        return true;
    }

    bool QueryBarrierTransitionLabel(std::size_t, const std::string&, std::string&) const override { return false; }

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

TEST(RenderPassTest, AddRenderPassFourArgumentOverloadStampsViewScopeCategoryAndOwner)
{
    RenderGraphBuilder builder;
    FakeMetadataSink sink;
    builder.SetDebugMetadataSink(&sink);

    const RenderFeatureScope scope(builder, "AtmosphereLut");
    builder.AddRenderPass(
        "AtmosphereSkyViewLutPass", PassKind::Compute, ViewScope::GameView, RenderPassCategory::General,
        [](RenderGraphBuilder::PassBuilder&) { }, NoOpExecute, RenderPassDrawKind::DrawMesh,
        RenderPassEvent::Opaques);

    const CompiledGraphInput input = builder.Finish();
    ASSERT_EQ(input.passes.size(), 1u);
    EXPECT_STREQ(input.passes[0].name, "AtmosphereSkyViewLutPass");
    EXPECT_EQ(input.passes[0].kind, PassKind::Compute);
    EXPECT_EQ(input.passes[0].viewScope, ViewScope::GameView);

    PassDebugMetadata metadata;
    ASSERT_TRUE(sink.QueryPassDebugMetadata(0, metadata));
    EXPECT_EQ(metadata.category, RenderPassCategory::General);
    EXPECT_EQ(metadata.owningFeatureName, "AtmosphereLut");
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

// --- Pass ownership: derived from an open RenderFeatureScope, never passed
// as an argument ------------------------------------------------------------

// The full (now 8-argument) overload picks up whichever RenderFeatureScope
// is open at the moment it is called, alongside every other field this
// overload already stamps.
TEST(RenderPassTest, AddRenderPassEightArgumentOverloadStampsOwnerFromOpenScope)
{
    RenderGraphBuilder builder;
    FakeMetadataSink sink;
    builder.SetDebugMetadataSink(&sink);

    const RenderFeatureScope scope(builder, "Shadow");
    builder.AddRenderPass(
        "AtmosphereSkyViewLutPass", PassKind::Compute, ViewScope::GameView, RenderPassCategory::General,
        [](RenderGraphBuilder::PassBuilder&) { }, NoOpExecute, RenderPassDrawKind::DrawQuad,
        RenderPassEvent::PreOpaques);

    const CompiledGraphInput input = builder.Finish();
    ASSERT_EQ(input.passes.size(), 1u);
    EXPECT_EQ(input.passes[0].viewScope, ViewScope::GameView);
    EXPECT_EQ(input.passes[0].renderPassEvent, RenderPassEvent::PreOpaques);

    PassDebugMetadata metadata;
    ASSERT_TRUE(sink.QueryPassDebugMetadata(0, metadata));
    EXPECT_EQ(metadata.category, RenderPassCategory::General);
    EXPECT_EQ(metadata.drawKind, RenderPassDrawKind::DrawQuad);
    EXPECT_EQ(metadata.owningFeatureName, "Shadow");
}

// The full (now 8-argument) overload stamps the loud "ENGINE_UNOWNED"
// sentinel when no RenderFeatureScope is open at all.
TEST(RenderPassTest, AddRenderPassEightArgumentOverloadDefaultsToEngineUnownedWhenNoScopeOpen)
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
    EXPECT_EQ(metadata.owningFeatureName, "ENGINE_UNOWNED");
}

// The convenience (now 6-argument) overload also picks up whichever
// RenderFeatureScope is open at the moment it is called - proving it
// genuinely reads the SAME scope stack the full overload does.
TEST(RenderPassTest, AddRenderPassSixArgumentConvenienceOverloadStampsOwnerFromOpenScope)
{
    RenderGraphBuilder builder;
    FakeMetadataSink sink;
    builder.SetDebugMetadataSink(&sink);

    const RenderFeatureScope scope(builder, "GPU Skinning");
    builder.AddRenderPass(
        "TestPass", PassKind::Graphics, [](RenderGraphBuilder::PassBuilder&) { }, NoOpExecute,
        RenderPassDrawKind::DrawMesh, RenderPassEvent::Opaques);

    const CompiledGraphInput input = builder.Finish();
    ASSERT_EQ(input.passes.size(), 1u);

    PassDebugMetadata metadata;
    ASSERT_TRUE(sink.QueryPassDebugMetadata(0, metadata));
    EXPECT_EQ(metadata.owningFeatureName, "GPU Skinning");
}

// The convenience (now 6-argument) overload also stamps "ENGINE_UNOWNED"
// when no RenderFeatureScope is open.
TEST(RenderPassTest, AddRenderPassSixArgumentConvenienceOverloadDefaultsToEngineUnownedWhenNoScopeOpen)
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
    EXPECT_EQ(metadata.owningFeatureName, "ENGINE_UNOWNED");
}

} // namespace
} // namespace gte::rg
