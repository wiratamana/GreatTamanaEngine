// Tier-1 tests: a (pass name, ViewScope) identity owns at most one binding layout per frame.

#include "Renderer/RenderGraph/RenderGraphBuilder.h"

#include <gtest/gtest.h>

#include <string>

namespace gte::rg {
namespace {

constexpr DescriptorSetLayoutHandle kLayout = static_cast<DescriptorSetLayoutHandle>(1);

void NoOpExecute(PassContext&) { }

void DeclareBound(RenderGraphBuilder& builder, const char* name, ViewScope scope)
{
    builder.AddComputePass(
        name, scope,
        [](RenderGraphBuilder::PassBuilder& pass) {
            pass.BindDescriptorSetLayout(kLayout);
            pass.BindStorageBuffer(0, BufferObjectHandle::Invalid);
        },
        NoOpExecute);
}

TEST(PassIdentityTest, FirstDeclarationKeepsItsLayoutAndRuns)
{
    RenderGraphBuilder builder;
    DeclareBound(builder, "Pass", ViewScope::GameView);

    const CompiledGraphInput input = builder.Finish();
    ASSERT_EQ(input.passes.size(), 1u);
    EXPECT_EQ(input.passes[0].computeBindingLayout, kLayout);
    EXPECT_EQ(input.passes[0].bindings.size(), 1u);
    EXPECT_FALSE(input.passes[0].skipExecute);
}

TEST(PassIdentityTest, SameNameAndScopeTwiceRefusesTheSecond)
{
    RenderGraphBuilder builder;
    DeclareBound(builder, "Pass", ViewScope::GameView);
    DeclareBound(builder, "Pass", ViewScope::GameView);

    const CompiledGraphInput input = builder.Finish();
    ASSERT_EQ(input.passes.size(), 2u);
    EXPECT_FALSE(input.passes[0].skipExecute);
    EXPECT_EQ(input.passes[0].computeBindingLayout, kLayout);

    EXPECT_TRUE(input.passes[1].skipExecute);
    EXPECT_EQ(input.passes[1].computeBindingLayout, DescriptorSetLayoutHandle::Invalid);
    EXPECT_TRUE(input.passes[1].bindings.empty());
}

TEST(PassIdentityTest, SameNameInDifferentViewsIsAllowed)
{
    RenderGraphBuilder builder;
    DeclareBound(builder, "Pass", ViewScope::GameView);
    DeclareBound(builder, "Pass", ViewScope::SceneView);

    const CompiledGraphInput input = builder.Finish();
    ASSERT_EQ(input.passes.size(), 2u);
    EXPECT_FALSE(input.passes[0].skipExecute);
    EXPECT_FALSE(input.passes[1].skipExecute);
}

TEST(PassIdentityTest, DifferentNamesInOneViewAreAllowed)
{
    RenderGraphBuilder builder;
    DeclareBound(builder, "PassA", ViewScope::GameView);
    DeclareBound(builder, "PassB", ViewScope::GameView);

    const CompiledGraphInput input = builder.Finish();
    EXPECT_FALSE(input.passes[0].skipExecute);
    EXPECT_FALSE(input.passes[1].skipExecute);
}

// Names compare by content, not by pointer.
TEST(PassIdentityTest, NamesComparedByContentNotPointer)
{
    static const std::string first = "Pass";
    static const std::string second = "Pass";
    ASSERT_NE(first.c_str(), second.c_str());

    RenderGraphBuilder builder;
    DeclareBound(builder, first.c_str(), ViewScope::Shared);
    DeclareBound(builder, second.c_str(), ViewScope::Shared);

    const CompiledGraphInput input = builder.Finish();
    EXPECT_TRUE(input.passes[1].skipExecute);
}

TEST(PassIdentityTest, PassesWithoutBindingLayoutNeverClaimAnIdentity)
{
    RenderGraphBuilder builder;
    builder.AddPass("Plain", ViewScope::GameView, [](RenderGraphBuilder::PassBuilder&) { }, NoOpExecute);
    builder.AddPass("Plain", ViewScope::GameView, [](RenderGraphBuilder::PassBuilder&) { }, NoOpExecute);
    DeclareBound(builder, "Plain", ViewScope::GameView);

    const CompiledGraphInput input = builder.Finish();
    ASSERT_EQ(input.passes.size(), 3u);
    EXPECT_FALSE(input.passes[0].skipExecute);
    EXPECT_FALSE(input.passes[1].skipExecute);
    EXPECT_FALSE(input.passes[2].skipExecute);
}

TEST(PassIdentityTest, RefusedPassKeepsItsResourceDeclarationsForBarriers)
{
    RenderGraphBuilder builder;
    const TextureHandle texture = builder.CreateTexture("Tex", TextureDesc{ 8, 8, PixelFormat::RGBA8_UNorm, false });
    for (int i = 0; i < 2; ++i) {
        builder.AddComputePass(
            "Pass", ViewScope::GameView,
            [&](RenderGraphBuilder::PassBuilder& pass) {
                pass.BindDescriptorSetLayout(kLayout);
                pass.WriteStorageImage(0, texture);
            },
            NoOpExecute);
    }

    const CompiledGraphInput input = builder.Finish();
    ASSERT_EQ(input.passes.size(), 2u);
    EXPECT_TRUE(input.passes[1].skipExecute);
    ASSERT_EQ(input.passes[1].writes.size(), 1u);
    EXPECT_EQ(input.passes[1].writes[0].texture, texture);
}

void DeclareBoundInstance(RenderGraphBuilder& builder, const char* name, ViewScope scope, std::uint64_t instance)
{
    builder.AddComputePass(
        name, scope,
        [instance](RenderGraphBuilder::PassBuilder& pass) {
            pass.BindDescriptorSetLayout(kLayout);
            pass.SetBindingInstance(instance);
            pass.BindStorageBuffer(0, BufferObjectHandle::Invalid);
        },
        NoOpExecute);
}

TEST(PassIdentityTest, SameNameAndScopeWithDifferentInstancesAreAllAllowed)
{
    RenderGraphBuilder builder;
    for (std::uint64_t instance = 1; instance <= 5; ++instance) {
        DeclareBoundInstance(builder, "Skin", ViewScope::Shared, instance);
    }

    const CompiledGraphInput input = builder.Finish();
    ASSERT_EQ(input.passes.size(), 5u);
    for (std::size_t i = 0; i < input.passes.size(); ++i) {
        EXPECT_FALSE(input.passes[i].skipExecute) << i;
        EXPECT_EQ(input.passes[i].bindingInstance, i + 1);
    }
}

TEST(PassIdentityTest, SameNameScopeAndInstanceTwiceRefusesTheSecond)
{
    RenderGraphBuilder builder;
    DeclareBoundInstance(builder, "Skin", ViewScope::Shared, 7);
    DeclareBoundInstance(builder, "Skin", ViewScope::Shared, 7);

    const CompiledGraphInput input = builder.Finish();
    EXPECT_FALSE(input.passes[0].skipExecute);
    EXPECT_TRUE(input.passes[1].skipExecute);
}

TEST(PassIdentityTest, InstanceZeroKeepsTheOldIdentityRule)
{
    RenderGraphBuilder builder;
    DeclareBound(builder, "Pass", ViewScope::GameView);
    DeclareBoundInstance(builder, "Pass", ViewScope::GameView, 0);

    const CompiledGraphInput input = builder.Finish();
    EXPECT_TRUE(input.passes[1].skipExecute);
}

} // namespace
} // namespace gte::rg
