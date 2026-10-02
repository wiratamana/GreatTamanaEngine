// Tier-1 tests for the PostTransparent stage's ordering guarantee and
// runtime safety net.
//
// Part A - compiler-level ordering: a RenderPassEvent::AfterTransparents-
// tagged pass declared at an earlier index (simulating "AtmosphereComposite")
// always executes before a same-tier AfterTransparents pass declared later
// (simulating a PostTransparent feature's own pass), and both execute after
// a Transparents-tier pass (simulating "RenderTransparent") regardless of
// declaration order.
//
// Part B - runtime safety-net coverage, reusing
// src/Core/Plugins/ProjectScenePassCallback.h's own two pure functions
// directly, mirroring PostOpaqueStageOrderingTests.cpp's own established
// shape. No Vulkan device/Core/RenderFeatureCompositor involved anywhere in
// this file - pure RenderGraphBuilder/RenderGraphCompiler usage.

#include "Core/Plugins/ProjectScenePassCallback.h"
#include "Renderer/RenderGraph/RenderGraphCompiler.h"

#include <gtest/gtest.h>

namespace gte {
namespace {

void NoOpExecute(rg::PassContext&) { }

rg::TextureDesc MakeTextureDesc()
{
    return rg::TextureDesc{ 64, 64, VK_FORMAT_R8G8B8A8_UNORM, false };
}

} // namespace

// --- Part A: compiler-level ordering ----------------------------------------

TEST(PostTransparentStageOrderingTest, SameTierEarlierDeclaredPassExecutesBeforeALaterDeclaredPostTransparentFeaturePass)
{
    rg::RenderGraphBuilder builder;
    const rg::TextureHandle colorHandle = builder.CreateTexture("ViewColor", MakeTextureDesc());

    // Declared in a DELIBERATELY ADVERSARIAL order: both AfterTransparents
    // passes declared BEFORE the Transparents pass, to prove tier (not
    // declaration order) decides cross-tier placement.
    builder.AddRenderPass(
        "AtmosphereComposite_Stub", rg::PassKind::Compute,
        [&](rg::RenderGraphBuilder::PassBuilder& pass) { pass.WriteColorAttachment(colorHandle); }, NoOpExecute,
        rg::RenderPassDrawKind::Blit, rg::RenderPassEvent::AfterTransparents); // index 0.
    builder.AddRenderPass(
        "MyProject.PostTransparentFeature_Stub", rg::PassKind::Graphics,
        [&](rg::RenderGraphBuilder::PassBuilder& pass) { pass.WriteColorAttachment(colorHandle); }, NoOpExecute,
        rg::RenderPassDrawKind::DrawMesh, rg::RenderPassEvent::AfterTransparents); // index 1.
    builder.AddRenderPass(
        "RenderTransparent_Stub", rg::PassKind::Graphics,
        [&](rg::RenderGraphBuilder::PassBuilder& pass) { pass.WriteColorAttachment(colorHandle); }, NoOpExecute,
        rg::RenderPassDrawKind::DrawMesh, rg::RenderPassEvent::Transparents); // index 2.

    rg::CompiledGraphInput input = builder.Finish();
    const rg::TextureHandle finalOutputs[] = { colorHandle };
    const rg::CompiledGraph compiled = rg::Compile(input, finalOutputs);

    ASSERT_EQ(compiled.executionOrder.size(), 3u);
    // RenderTransparent_Stub (Transparents tier, index 2) executes FIRST
    // despite being declared LAST.
    EXPECT_EQ(compiled.executionOrder[0].index, 2u);
    // Within the shared AfterTransparents tier, the earlier-declared
    // AtmosphereComposite_Stub (index 0) executes before the later-declared
    // PostTransparentFeature_Stub (index 1).
    EXPECT_EQ(compiled.executionOrder[1].index, 0u);
    EXPECT_EQ(compiled.executionOrder[2].index, 1u);
}

// --- Part B: runtime safety-net coverage, both halves, via the real pure
// functions this stage's own production provider calls -----------------------

TEST(PostTransparentStageOrderingTest, ACorrectlyTaggedCorrectlyReadDeclaringPassTriggersNeitherSafetyNetHalf)
{
    rg::RenderGraphBuilder builder;
    const rg::TextureHandle colorHandle = builder.CreateTexture("Color", MakeTextureDesc());
    const rg::TextureHandle depthHandle = builder.CreateTexture("Depth", MakeTextureDesc());
    const rg::TextureHandle output = builder.CreateTexture("Output", MakeTextureDesc());

    const std::size_t before = builder.DeclaredPassCount();
    builder.AddRenderPass(
        "MyProject.Correct", rg::PassKind::Graphics,
        [&](rg::RenderGraphBuilder::PassBuilder& pass) {
            pass.ReadTexture(colorHandle, rg::ResourceAccess::ShaderRead);
            pass.WriteColorAttachment(output);
        },
        NoOpExecute, rg::RenderPassDrawKind::DrawMesh, rg::RenderPassEvent::AfterTransparents);
    const std::size_t after = builder.DeclaredPassCount();

    EXPECT_TRUE(FindPassesNotTaggedScenePass(builder, before, after, rg::RenderPassEvent::AfterTransparents).empty());
    EXPECT_FALSE(FindScenePassCallbackMissingReadDeclaration(builder, before, after, colorHandle, depthHandle));
}

TEST(PostTransparentStageOrderingTest, APassLeftAtTheImplicitDefaultTagTriggersTheTagCheckHalf)
{
    rg::RenderGraphBuilder builder;
    const rg::TextureHandle colorHandle = builder.CreateTexture("Color", MakeTextureDesc());
    const rg::TextureHandle depthHandle = builder.CreateTexture("Depth", MakeTextureDesc());
    const rg::TextureHandle output = builder.CreateTexture("Output", MakeTextureDesc());

    const std::size_t before = builder.DeclaredPassCount();
    builder.AddRenderPass(
        "MyProject.ForgotToTag", rg::PassKind::Graphics,
        [&](rg::RenderGraphBuilder::PassBuilder& pass) {
            pass.ReadTexture(colorHandle, rg::ResourceAccess::ShaderRead);
            pass.WriteColorAttachment(output);
        },
        NoOpExecute); // No RenderPassEvent argument at all - implicit default is Opaques.
    const std::size_t after = builder.DeclaredPassCount();

    const std::vector<std::size_t> violations =
        FindPassesNotTaggedScenePass(builder, before, after, rg::RenderPassEvent::AfterTransparents);
    ASSERT_EQ(violations.size(), 1u);
    EXPECT_EQ(violations[0], before);
}

TEST(PostTransparentStageOrderingTest, ACorrectlyTaggedPassThatNeverReadsEitherHandleTriggersTheMissingReadHalf)
{
    rg::RenderGraphBuilder builder;
    const rg::TextureHandle colorHandle = builder.CreateTexture("Color", MakeTextureDesc());
    const rg::TextureHandle depthHandle = builder.CreateTexture("Depth", MakeTextureDesc());
    const rg::TextureHandle unrelated = builder.CreateTexture("Unrelated", MakeTextureDesc());

    const std::size_t before = builder.DeclaredPassCount();
    builder.AddRenderPass(
        "MyProject.ForgotTheRead", rg::PassKind::Graphics,
        [&](rg::RenderGraphBuilder::PassBuilder& pass) { pass.WriteColorAttachment(unrelated); }, NoOpExecute,
        rg::RenderPassDrawKind::DrawMesh, rg::RenderPassEvent::AfterTransparents);
    const std::size_t after = builder.DeclaredPassCount();

    EXPECT_TRUE(FindPassesNotTaggedScenePass(builder, before, after, rg::RenderPassEvent::AfterTransparents).empty());
    EXPECT_TRUE(FindScenePassCallbackMissingReadDeclaration(builder, before, after, colorHandle, depthHandle));
}

// False-positive guard - a callback that declares ZERO passes this
// invocation must trigger neither half.
TEST(PostTransparentStageOrderingTest, ACallbackThatDeclaresZeroPassesTriggersNeitherSafetyNetHalf)
{
    rg::RenderGraphBuilder builder;
    const rg::TextureHandle colorHandle = builder.CreateTexture("Color", MakeTextureDesc());
    const rg::TextureHandle depthHandle = builder.CreateTexture("Depth", MakeTextureDesc());

    const std::size_t before = builder.DeclaredPassCount();
    const std::size_t after = builder.DeclaredPassCount();

    EXPECT_TRUE(FindPassesNotTaggedScenePass(builder, before, after, rg::RenderPassEvent::AfterTransparents).empty());
    EXPECT_FALSE(FindScenePassCallbackMissingReadDeclaration(builder, before, after, colorHandle, depthHandle));
}

} // namespace gte
