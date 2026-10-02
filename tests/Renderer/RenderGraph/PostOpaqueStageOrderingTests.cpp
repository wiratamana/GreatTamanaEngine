// Tier-1 tests for the PostOpaque stage's ordering guarantee and runtime
// safety net.
//
// Part A - compiler-level ordering: a RenderPassEvent::AfterOpaques-tagged
// pass declared at an earlier index (simulating "DrawSkyBackground") always
// executes before a same-tier AfterOpaques pass declared later (simulating a
// PostOpaque feature's own pass), and both execute after an Opaques-tier
// pass (simulating "RenderOpaque") regardless of declaration order.
//
// Part B - runtime safety-net coverage, reusing
// src/Core/Plugins/ProjectScenePassCallback.h's own two pure functions
// directly, mirroring PreOpaqueStageOrderingTests.cpp's own established
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

TEST(PostOpaqueStageOrderingTest, SameTierEarlierDeclaredPassExecutesBeforeALaterDeclaredPostOpaqueFeaturePass)
{
    rg::RenderGraphBuilder builder;
    const rg::TextureHandle colorHandle = builder.CreateTexture("ViewColor", MakeTextureDesc());

    // Declared in a DELIBERATELY ADVERSARIAL order: both AfterOpaques passes
    // declared BEFORE the Opaques pass, to prove tier (not declaration order)
    // decides cross-tier placement.
    builder.AddRenderPass(
        "DrawSkyBackground_Stub", rg::PassKind::Graphics,
        [&](rg::RenderGraphBuilder::PassBuilder& pass) { pass.WriteColorAttachment(colorHandle); }, NoOpExecute,
        rg::RenderPassDrawKind::DrawQuad, rg::RenderPassEvent::AfterOpaques); // index 0.
    builder.AddRenderPass(
        "MyProject.PostOpaqueFeature_Stub", rg::PassKind::Graphics,
        [&](rg::RenderGraphBuilder::PassBuilder& pass) { pass.WriteColorAttachment(colorHandle); }, NoOpExecute,
        rg::RenderPassDrawKind::DrawMesh, rg::RenderPassEvent::AfterOpaques); // index 1.
    builder.AddRenderPass(
        "RenderOpaque_Stub", rg::PassKind::Graphics,
        [&](rg::RenderGraphBuilder::PassBuilder& pass) { pass.WriteColorAttachment(colorHandle); }, NoOpExecute,
        rg::RenderPassDrawKind::DrawMesh, rg::RenderPassEvent::Opaques); // index 2.

    rg::CompiledGraphInput input = builder.Finish();
    const rg::TextureHandle finalOutputs[] = { colorHandle };
    const rg::CompiledGraph compiled = rg::Compile(input, finalOutputs);

    ASSERT_EQ(compiled.executionOrder.size(), 3u);
    // RenderOpaque_Stub (Opaques tier, index 2) executes FIRST despite being
    // declared LAST.
    EXPECT_EQ(compiled.executionOrder[0].index, 2u);
    // Within the shared AfterOpaques tier, the earlier-declared
    // DrawSkyBackground_Stub (index 0) executes before the later-declared
    // PostOpaqueFeature_Stub (index 1).
    EXPECT_EQ(compiled.executionOrder[1].index, 0u);
    EXPECT_EQ(compiled.executionOrder[2].index, 1u);
}

// --- Part B: runtime safety-net coverage, both halves, via the real pure
// functions this stage's own production provider calls -----------------------

TEST(PostOpaqueStageOrderingTest, ACorrectlyTaggedCorrectlyReadDeclaringPassTriggersNeitherSafetyNetHalf)
{
    rg::RenderGraphBuilder builder;
    const rg::TextureHandle colorHandle = builder.CreateTexture("Color", MakeTextureDesc());
    const rg::TextureHandle depthHandle = builder.CreateTexture("Depth", MakeTextureDesc());
    const rg::TextureHandle output = builder.CreateTexture("Output", MakeTextureDesc());

    const std::size_t before = builder.DeclaredPassCount();
    builder.AddRenderPass(
        "MyProject.Correct", rg::PassKind::Graphics,
        [&](rg::RenderGraphBuilder::PassBuilder& pass) {
            pass.ReadTexture(depthHandle, rg::ResourceAccess::ShaderRead, /*isDepthResource=*/true);
            pass.WriteColorAttachment(output);
        },
        NoOpExecute, rg::RenderPassDrawKind::DrawMesh, rg::RenderPassEvent::AfterOpaques);
    const std::size_t after = builder.DeclaredPassCount();

    EXPECT_TRUE(FindPassesNotTaggedScenePass(builder, before, after, rg::RenderPassEvent::AfterOpaques).empty());
    EXPECT_FALSE(FindScenePassCallbackMissingReadDeclaration(builder, before, after, colorHandle, depthHandle));
}

TEST(PostOpaqueStageOrderingTest, APassLeftAtTheImplicitDefaultTagTriggersTheTagCheckHalf)
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
        FindPassesNotTaggedScenePass(builder, before, after, rg::RenderPassEvent::AfterOpaques);
    ASSERT_EQ(violations.size(), 1u);
    EXPECT_EQ(violations[0], before);
}

TEST(PostOpaqueStageOrderingTest, ACorrectlyTaggedPassThatNeverReadsEitherHandleTriggersTheMissingReadHalf)
{
    rg::RenderGraphBuilder builder;
    const rg::TextureHandle colorHandle = builder.CreateTexture("Color", MakeTextureDesc());
    const rg::TextureHandle depthHandle = builder.CreateTexture("Depth", MakeTextureDesc());
    const rg::TextureHandle unrelated = builder.CreateTexture("Unrelated", MakeTextureDesc());

    const std::size_t before = builder.DeclaredPassCount();
    builder.AddRenderPass(
        "MyProject.ForgotTheRead", rg::PassKind::Graphics,
        [&](rg::RenderGraphBuilder::PassBuilder& pass) { pass.WriteColorAttachment(unrelated); }, NoOpExecute,
        rg::RenderPassDrawKind::DrawMesh, rg::RenderPassEvent::AfterOpaques);
    const std::size_t after = builder.DeclaredPassCount();

    EXPECT_TRUE(FindPassesNotTaggedScenePass(builder, before, after, rg::RenderPassEvent::AfterOpaques).empty());
    EXPECT_TRUE(FindScenePassCallbackMissingReadDeclaration(builder, before, after, colorHandle, depthHandle));
}

// False-positive guard - a callback that declares ZERO passes this
// invocation must trigger neither half.
TEST(PostOpaqueStageOrderingTest, ACallbackThatDeclaresZeroPassesTriggersNeitherSafetyNetHalf)
{
    rg::RenderGraphBuilder builder;
    const rg::TextureHandle colorHandle = builder.CreateTexture("Color", MakeTextureDesc());
    const rg::TextureHandle depthHandle = builder.CreateTexture("Depth", MakeTextureDesc());

    const std::size_t before = builder.DeclaredPassCount();
    const std::size_t after = builder.DeclaredPassCount();

    EXPECT_TRUE(FindPassesNotTaggedScenePass(builder, before, after, rg::RenderPassEvent::AfterOpaques).empty());
    EXPECT_FALSE(FindScenePassCallbackMissingReadDeclaration(builder, before, after, colorHandle, depthHandle));
}

} // namespace gte
