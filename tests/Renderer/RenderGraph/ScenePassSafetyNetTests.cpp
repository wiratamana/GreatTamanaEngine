// Tier-1 tests for the two pure, side-effect-free runtime safety-net
// functions in src/Core/Plugins/ProjectScenePassCallback.h -
// FindPassesNotTaggedScenePass() and FindScenePassCallbackMissingReadDeclaration().
// No Vulkan device/Core/RenderFeatureCompositor involved anywhere in this
// file - pure RenderGraphBuilder usage, mirroring
// PreOpaqueStageOrderingTests.cpp's own established convention.

#include "Core/Plugins/ProjectScenePassCallback.h"

#include <gtest/gtest.h>

namespace gte {
namespace {

void NoOpExecute(rg::PassContext&) { }

rg::TextureDesc MakeTextureDesc()
{
    return rg::TextureDesc{ 64, 64, VK_FORMAT_R8G8B8A8_UNORM, false };
}

} // namespace

// --- FindPassesNotTaggedScenePass() -----------------------------------------

TEST(ScenePassSafetyNetTest, FindPassesNotTaggedScenePassReportsEmptyWhenEveryPassIsTaggedAfterOpaques)
{
    rg::RenderGraphBuilder builder;
    const rg::TextureHandle t0 = builder.CreateTexture("T0", MakeTextureDesc());

    const std::size_t before = builder.DeclaredPassCount();
    builder.AddRenderPass(
        "MyProject.PostOpaquePass", rg::PassKind::Graphics,
        [&](rg::RenderGraphBuilder::PassBuilder& pass) { pass.WriteColorAttachment(t0); }, NoOpExecute,
        rg::RenderPassDrawKind::DrawMesh, rg::RenderPassEvent::AfterOpaques);
    const std::size_t after = builder.DeclaredPassCount();

    EXPECT_TRUE(FindPassesNotTaggedScenePass(builder, before, after, rg::RenderPassEvent::AfterOpaques).empty());
}

TEST(ScenePassSafetyNetTest, FindPassesNotTaggedScenePassCatchesTheImplicitDefaultTagMistake)
{
    rg::RenderGraphBuilder builder;
    const rg::TextureHandle t0 = builder.CreateTexture("T0", MakeTextureDesc());

    const std::size_t before = builder.DeclaredPassCount();
    // Forgot to tag - falls back to the implicit default (Opaques), not
    // AfterOpaques.
    builder.AddRenderPass(
        "MyProject.ForgotToTag", rg::PassKind::Graphics,
        [&](rg::RenderGraphBuilder::PassBuilder& pass) { pass.WriteColorAttachment(t0); }, NoOpExecute);
    const std::size_t after = builder.DeclaredPassCount();

    const std::vector<std::size_t> violations =
        FindPassesNotTaggedScenePass(builder, before, after, rg::RenderPassEvent::AfterOpaques);
    ASSERT_EQ(violations.size(), 1u);
    EXPECT_EQ(violations[0], before);
}

// Proves the function is genuinely parameterized, not hard-coded to one
// tag - the same two cases, independently, for AfterTransparents.
TEST(ScenePassSafetyNetTest, FindPassesNotTaggedScenePassReportsEmptyWhenEveryPassIsTaggedAfterTransparents)
{
    rg::RenderGraphBuilder builder;
    const rg::TextureHandle t0 = builder.CreateTexture("T0", MakeTextureDesc());

    const std::size_t before = builder.DeclaredPassCount();
    builder.AddRenderPass(
        "MyProject.PostTransparentPass", rg::PassKind::Graphics,
        [&](rg::RenderGraphBuilder::PassBuilder& pass) { pass.WriteColorAttachment(t0); }, NoOpExecute,
        rg::RenderPassDrawKind::DrawMesh, rg::RenderPassEvent::AfterTransparents);
    const std::size_t after = builder.DeclaredPassCount();

    EXPECT_TRUE(FindPassesNotTaggedScenePass(builder, before, after, rg::RenderPassEvent::AfterTransparents).empty());
}

TEST(ScenePassSafetyNetTest, FindPassesNotTaggedScenePassCatchesAMistaggedAfterTransparentsPass)
{
    rg::RenderGraphBuilder builder;
    const rg::TextureHandle t0 = builder.CreateTexture("T0", MakeTextureDesc());

    const std::size_t before = builder.DeclaredPassCount();
    builder.AddRenderPass(
        "MyProject.Mistagged", rg::PassKind::Graphics,
        [&](rg::RenderGraphBuilder::PassBuilder& pass) { pass.WriteColorAttachment(t0); }, NoOpExecute,
        rg::RenderPassDrawKind::DrawMesh, rg::RenderPassEvent::AfterOpaques);
    const std::size_t after = builder.DeclaredPassCount();

    const std::vector<std::size_t> violations =
        FindPassesNotTaggedScenePass(builder, before, after, rg::RenderPassEvent::AfterTransparents);
    ASSERT_EQ(violations.size(), 1u);
    EXPECT_EQ(violations[0], before);
}

// --- FindScenePassCallbackMissingReadDeclaration() --------------------------

TEST(ScenePassSafetyNetTest, FindScenePassCallbackMissingReadDeclarationReturnsFalseWhenRangeIsEmpty)
{
    rg::RenderGraphBuilder builder;
    const rg::TextureHandle colorHandle = builder.CreateTexture("Color", MakeTextureDesc());
    const rg::TextureHandle depthHandle = builder.CreateTexture("Depth", MakeTextureDesc());

    const std::size_t before = builder.DeclaredPassCount();
    const std::size_t after = builder.DeclaredPassCount();

    EXPECT_FALSE(FindScenePassCallbackMissingReadDeclaration(builder, before, after, colorHandle, depthHandle));
}

TEST(ScenePassSafetyNetTest, FindScenePassCallbackMissingReadDeclarationReturnsTrueWhenNoPassReadsEitherHandle)
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

    EXPECT_TRUE(FindScenePassCallbackMissingReadDeclaration(builder, before, after, colorHandle, depthHandle));
}

TEST(ScenePassSafetyNetTest, FindScenePassCallbackMissingReadDeclarationReturnsFalseWhenAPassReadsColorHandle)
{
    rg::RenderGraphBuilder builder;
    const rg::TextureHandle colorHandle = builder.CreateTexture("Color", MakeTextureDesc());
    const rg::TextureHandle depthHandle = builder.CreateTexture("Depth", MakeTextureDesc());
    const rg::TextureHandle output = builder.CreateTexture("Output", MakeTextureDesc());

    const std::size_t before = builder.DeclaredPassCount();
    builder.AddRenderPass(
        "MyProject.ReadsColor", rg::PassKind::Graphics,
        [&](rg::RenderGraphBuilder::PassBuilder& pass) {
            pass.ReadTexture(colorHandle, rg::ResourceAccess::ShaderRead);
            pass.WriteColorAttachment(output);
        },
        NoOpExecute, rg::RenderPassDrawKind::DrawMesh, rg::RenderPassEvent::AfterOpaques);
    const std::size_t after = builder.DeclaredPassCount();

    EXPECT_FALSE(FindScenePassCallbackMissingReadDeclaration(builder, before, after, colorHandle, depthHandle));
}

// Proves the "OR" logic works both ways - a pass reading ONLY depthHandle
// is also enough to clear the violation.
TEST(ScenePassSafetyNetTest, FindScenePassCallbackMissingReadDeclarationReturnsFalseWhenAPassReadsDepthHandle)
{
    rg::RenderGraphBuilder builder;
    const rg::TextureHandle colorHandle = builder.CreateTexture("Color", MakeTextureDesc());
    const rg::TextureHandle depthHandle = builder.CreateTexture("Depth", MakeTextureDesc());
    const rg::TextureHandle output = builder.CreateTexture("Output", MakeTextureDesc());

    const std::size_t before = builder.DeclaredPassCount();
    builder.AddRenderPass(
        "MyProject.ReadsDepth", rg::PassKind::Graphics,
        [&](rg::RenderGraphBuilder::PassBuilder& pass) {
            pass.ReadTexture(depthHandle, rg::ResourceAccess::ShaderRead);
            pass.WriteColorAttachment(output);
        },
        NoOpExecute, rg::RenderPassDrawKind::DrawMesh, rg::RenderPassEvent::AfterOpaques);
    const std::size_t after = builder.DeclaredPassCount();

    EXPECT_FALSE(FindScenePassCallbackMissingReadDeclaration(builder, before, after, colorHandle, depthHandle));
}

// Exercises PassReadsTexture()'s own isDepthResource-blind behavior
// transitively - still worth asserting at this layer too, since this is
// the function production code actually calls.
TEST(ScenePassSafetyNetTest, FindScenePassCallbackMissingReadDeclarationReturnsFalseForADepthAspectRead)
{
    rg::RenderGraphBuilder builder;
    const rg::TextureHandle colorHandle = builder.CreateTexture("Color", MakeTextureDesc());
    const rg::TextureHandle depthHandle = builder.CreateTexture("Depth", MakeTextureDesc());
    const rg::TextureHandle output = builder.CreateTexture("Output", MakeTextureDesc());

    const std::size_t before = builder.DeclaredPassCount();
    builder.AddRenderPass(
        "MyProject.ReadsDepthAspect", rg::PassKind::Graphics,
        [&](rg::RenderGraphBuilder::PassBuilder& pass) {
            pass.ReadTexture(colorHandle, rg::ResourceAccess::ShaderRead, /*isDepthResource=*/true);
            pass.WriteColorAttachment(output);
        },
        NoOpExecute, rg::RenderPassDrawKind::DrawMesh, rg::RenderPassEvent::AfterOpaques);
    const std::size_t after = builder.DeclaredPassCount();

    EXPECT_FALSE(FindScenePassCallbackMissingReadDeclaration(builder, before, after, colorHandle, depthHandle));
}

} // namespace gte
