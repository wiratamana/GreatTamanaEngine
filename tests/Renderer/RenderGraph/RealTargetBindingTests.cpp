// Tier-2 (headless GPU) tests: passes bound to REAL imported RenderTextures, the way a view target, a depth-only
// shadow map and a persistent color texture reach the graph. Declared on a bare builder, never executed.

#include "Renderer/RenderGraph/ComputePass.h"
#include "Renderer/RenderGraph/FullscreenPass.h"
#include "Renderer/RenderGraph/RenderGraph.h"
#include "Renderer/RenderGraph/RenderGraphBuilder.h"
#include "Renderer/RenderGraph/Shader.h"
#include "Renderer/RenderTexture.h"

#include "../../Fakes/HeadlessRenderGraphFixture.h"
#include "../../Fakes/RecordingLogSink.h"

#include <gtest/gtest.h>

#include <array>

namespace gte::rg {
namespace {

using test::CountErrorsByCategory;
using test::RecordingLogSink;
using test::ScopedLogSinkInstall;

constexpr ExecuteTimingMode kSync = ExecuteTimingMode::SynchronousImmediateReadback;

// Three real textures: a view target (color + depth companion), a depth-only map and a color-only texture.
struct RealTargets {
    explicit RealTargets(HeadlessRenderGraphFixture& fixture)
        : viewTarget(fixture.GetRenderer().CreateRenderTexture(64, 64, PixelFormat::RGBA8_UNorm, "ViewTarget", "ViewDepth",
              /*storage=*/false, /*depthSampled=*/true, /*companion=*/true, /*color=*/true))
        , privateTarget(fixture.GetRenderer().CreateRenderTexture(64, 64, PixelFormat::RGBA8_UNorm, "PrivateTarget",
              "PrivateDepth", false, false, /*companion=*/true, /*color=*/true))
        , shadowMap(fixture.GetRenderer().CreateRenderTexture(64, 64, PixelFormat::Undefined, "ShadowMap", "ShadowMapDepth",
              false, /*depthSampled=*/true, /*companion=*/true, /*color=*/false))
        , maskTexture(fixture.GetRenderer().CreateRenderTexture(64, 64, PixelFormat::R8_UNorm, "Mask", nullptr,
              /*storage=*/true, false, /*companion=*/false, /*color=*/true))
    {
        builder.SetRenderer(&fixture.GetRenderer());
        builder.SetPersistentResourceCache(nullptr, kSync, 1);
        view = Import("ViewTarget", viewTarget);
        priv = Import("PrivateTarget", privateTarget);
        shadow = Import("ShadowMap", shadowMap);
        mask = Import("Mask", maskTexture);
    }

    TextureHandle Import(const char* name, RenderTexture& texture)
    {
        return builder.ImportTexture(name, texture.Target(), ImageLayout::Undefined, SamplerHandle::Invalid,
            texture.DepthSamplerObjectHandle());
    }

    RenderTexture viewTarget;
    RenderTexture privateTarget;
    RenderTexture shadowMap;
    RenderTexture maskTexture;
    RenderGraphBuilder builder;
    TextureHandle view;
    TextureHandle priv;
    TextureHandle shadow;
    TextureHandle mask;
};

TEST(RealTargetBindingTest, ImportedTexturesDescribeWhatTheyReallyAre)
{
    HeadlessRenderGraphFixture fixture;
    if (!fixture.IsUsable()) {
        GTEST_SKIP() << fixture.SkipReason();
    }
    RealTargets t(fixture);

    const std::optional<TextureDesc> view = t.builder.DescOf(t.view);
    const std::optional<TextureDesc> shadow = t.builder.DescOf(t.shadow);
    const std::optional<TextureDesc> mask = t.builder.DescOf(t.mask);
    ASSERT_TRUE(view && shadow && mask);
    EXPECT_TRUE(view->hasColor);
    EXPECT_TRUE(view->hasDepth);
    EXPECT_FALSE(shadow->hasColor);
    EXPECT_TRUE(shadow->hasDepth);
    EXPECT_TRUE(mask->hasColor);
    EXPECT_FALSE(mask->hasDepth);
}

// The three passes of a shadow feature: depth sampled from a view target and a depth-only map, a color
// view sampled as color, a composite drawn into a private target that carries a depth companion.
TEST(RealTargetBindingTest, ShadowStyleBindingsOnRealTexturesAreAccepted)
{
    HeadlessRenderGraphFixture fixture;
    if (!fixture.IsUsable()) {
        GTEST_SKIP() << fixture.SkipReason();
    }
    RecordingLogSink sink;
    const ScopedLogSinkInstall logGuard(&sink);
    RealTargets t(fixture);
    Shader compute("ReflectFixture", ShaderKind::Compute);
    Shader graphics("ReflectFixture", ShaderKind::Graphics);
    GpuParams<std::array<float, 16>> params;

    const bool dispatched =
        ComputePass(t.builder, compute, "Test.Mask", ViewScope::GameView, RenderPassEvent::AfterOpaques)
            .Params(params, std::array<float, 16>{})
            .Bind("inputDepthA", Depth(t.shadow))
            .Bind("inputDepthB", Depth(t.view))
            .Bind("maskOutput", t.mask)
            .Dispatch(Extent2D{ 64, 64 });
    EXPECT_TRUE(dispatched);

    const bool colorAndDepthView = FullscreenPass(t.builder, graphics, "Test.CompositeA", ViewScope::GameView)
                                       .Bind("sourceColor", t.view)
                                       .Bind("sourceMask", Depth(t.view))
                                       .Push(0.5f)
                                       .Target(t.priv)
                                       .Draw();
    EXPECT_TRUE(colorAndDepthView);

    const bool depthOnlyMap = FullscreenPass(t.builder, graphics, "Test.CompositeB", ViewScope::GameView)
                                  .Bind("sourceColor", Depth(t.shadow))
                                  .Bind("sourceMask", t.mask)
                                  .Push(0.5f)
                                  .Target(t.priv)
                                  .Draw();
    EXPECT_TRUE(depthOnlyMap);

    EXPECT_EQ(CountErrorsByCategory(sink, "PassChain"), 0);
    EXPECT_EQ(t.builder.DeclaredPassCount(), 3u);
}

TEST(RealTargetBindingTest, DepthOnlyTextureBoundWithoutDepthIsRefused)
{
    HeadlessRenderGraphFixture fixture;
    if (!fixture.IsUsable()) {
        GTEST_SKIP() << fixture.SkipReason();
    }
    RealTargets t(fixture);
    Shader graphics("ReflectFixture", ShaderKind::Graphics);

    const bool drawn = FullscreenPass(t.builder, graphics, "Test.Composite", ViewScope::GameView)
                           .Bind("sourceColor", t.shadow)
                           .Bind("sourceMask", t.mask)
                           .Push(0.5f)
                           .Target(t.priv)
                           .Draw();
    EXPECT_FALSE(drawn);
    EXPECT_EQ(t.builder.DeclaredPassCount(), 0u);
}

TEST(RealTargetBindingTest, DepthOnlyTextureAsColorTargetIsRefused)
{
    HeadlessRenderGraphFixture fixture;
    if (!fixture.IsUsable()) {
        GTEST_SKIP() << fixture.SkipReason();
    }
    RealTargets t(fixture);
    Shader graphics("ReflectFixture", ShaderKind::Graphics);

    const bool drawn = FullscreenPass(t.builder, graphics, "Test.Composite", ViewScope::GameView)
                           .Bind("sourceColor", t.view)
                           .Bind("sourceMask", t.mask)
                           .Push(0.5f)
                           .Target(t.shadow)
                           .Draw();
    EXPECT_FALSE(drawn);
    EXPECT_EQ(t.builder.DeclaredPassCount(), 0u);
}

TEST(RealTargetBindingTest, DepthOnlyTextureAsStorageImageIsRefused)
{
    HeadlessRenderGraphFixture fixture;
    if (!fixture.IsUsable()) {
        GTEST_SKIP() << fixture.SkipReason();
    }
    RealTargets t(fixture);
    Shader compute("ReflectFixture", ShaderKind::Compute);
    GpuParams<std::array<float, 16>> params;

    const bool dispatched =
        ComputePass(t.builder, compute, "Test.Mask", ViewScope::GameView, RenderPassEvent::AfterOpaques)
            .Params(params, std::array<float, 16>{})
            .Bind("inputDepthA", Depth(t.shadow))
            .Bind("inputDepthB", Depth(t.view))
            .Bind("maskOutput", t.shadow)
            .Dispatch(Extent2D{ 64, 64 });
    EXPECT_FALSE(dispatched);
}

TEST(RealTargetBindingTest, DepthViewOfAColorOnlyTextureIsRefused)
{
    HeadlessRenderGraphFixture fixture;
    if (!fixture.IsUsable()) {
        GTEST_SKIP() << fixture.SkipReason();
    }
    RealTargets t(fixture);
    Shader graphics("ReflectFixture", ShaderKind::Graphics);

    const bool drawn = FullscreenPass(t.builder, graphics, "Test.Composite", ViewScope::GameView)
                           .Bind("sourceColor", Depth(t.mask))
                           .Bind("sourceMask", t.mask)
                           .Push(0.5f)
                           .Target(t.priv)
                           .Draw();
    EXPECT_FALSE(drawn);
}

} // namespace
} // namespace gte::rg
