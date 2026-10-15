// Tier-2 (headless GPU) tests: kit compute and fullscreen chains over real shaders and real RenderTextures.
// Declared on a bare builder with a renderer, never executed. Skipped when the headless fixture is unusable.

#include "Core/FeatureKit/FeatureKit.h"
#include "Core/FeatureKit/FrameContext.h"
#include "ECS/Registry.h"
#include "Renderer/RenderGraph/RenderGraph.h"
#include "Renderer/RenderGraph/RenderGraphBuilder.h"
#include "Renderer/RenderGraph/RenderPassToggleRegistry.h"
#include "Renderer/RenderTexture.h"

#include "../../Fakes/HeadlessRenderGraphFixture.h"
#include "../../Fakes/RecordingLogSink.h"

#include <gtest/gtest.h>

#include <array>

namespace gte::fx {
namespace {

constexpr rg::ExecuteTimingMode kSync = rg::ExecuteTimingMode::SynchronousImmediateReadback;

// A view target (color + depth), a private target and a color-only storage mask, all real and imported.
struct Scene {
    explicit Scene(HeadlessRenderGraphFixture& fixture)
        : view(fixture.GetRenderer().CreateRenderTexture(64, 64, PixelFormat::RGBA8_UNorm, "View", "ViewDepth", false, true))
        , priv(fixture.GetRenderer().CreateRenderTexture(64, 64, PixelFormat::RGBA8_UNorm, "Private", "PrivateDepth"))
        , mask(fixture.GetRenderer().CreateRenderTexture(64, 64, PixelFormat::R8_UNorm, "Mask", nullptr, true, false, false))
    {
        builder.SetRenderer(&fixture.GetRenderer());
        builder.SetPersistentResourceCache(nullptr, kSync, 1);
        viewHandle = Import("View", view);
        privHandle = Import("Private", priv);
        maskHandle = Import("Mask", mask);
        context.builder = &builder;
        context.scope = rg::ViewScope::GameView;
        context.event = rg::RenderPassEvent::AfterEverything;
        context.serial = 1;
    }

    rg::TextureHandle Import(const char* name, RenderTexture& texture)
    {
        return builder.ImportTexture(name, texture.Target(), ImageLayout::Undefined, SamplerHandle::Invalid,
            texture.DepthSamplerObjectHandle());
    }

    CompositeFrame MakeFrame() { return CompositeFrame(fx::View{}, registry, 1, viewHandle, viewHandle, privHandle, Extent2D{ 64, 64 }, &context); }

    RenderTexture view;
    RenderTexture priv;
    RenderTexture mask;
    Registry registry;
    rg::RenderGraphBuilder builder;
    detail::FrameContext context;
    rg::TextureHandle viewHandle;
    rg::TextureHandle privHandle;
    rg::TextureHandle maskHandle;
};

TEST(KitChainTest, ComputeAndFullscreenDeclareOnePassEachWithTheFramesScopeAndEvent)
{
    HeadlessRenderGraphFixture fixture;
    if (!fixture.IsUsable()) {
        GTEST_SKIP() << fixture.SkipReason();
    }
    Scene scene(fixture);
    ComputeShader compute("ReflectFixture");
    FullscreenShader fullscreen("ReflectFixture");
    GpuParams<std::array<float, 16>> params;
    CompositeFrame frame = scene.MakeFrame();

    const bool dispatched = frame.Compute(compute, "Test.Dispatch")
                                .Params(params, std::array<float, 16>{})
                                .Bind("inputDepthA", Depth(frame.SceneDepth()))
                                .Bind("inputDepthB", Depth(frame.SceneDepth()))
                                .Bind("maskOutput", frame.Stamp(scene.maskHandle))
                                .Dispatch(Extent2D{ 64, 64 });
    const bool drawn = frame.Fullscreen(fullscreen, "Test.Draw")
                           .Bind("sourceColor", frame.SceneColor())
                           .Bind("sourceMask", frame.Stamp(scene.maskHandle))
                           .Push(0.5f)
                           .Target(frame.PrivateTarget())
                           .Draw();
    EXPECT_TRUE(dispatched);
    EXPECT_TRUE(drawn);

    const rg::CompiledGraphInput input = scene.builder.Finish();
    ASSERT_EQ(input.passes.size(), 2u);
    for (const rg::PassRecord& pass : input.passes) {
        EXPECT_EQ(pass.viewScope, rg::ViewScope::GameView);
        EXPECT_EQ(pass.renderPassEvent, rg::RenderPassEvent::AfterEverything);
    }
}

TEST(KitChainTest, ADisabledTogglePassDeclaresNothing)
{
    HeadlessRenderGraphFixture fixture;
    if (!fixture.IsUsable()) {
        GTEST_SKIP() << fixture.SkipReason();
    }
    Scene scene(fixture);
    rg::RenderPassToggleRegistry toggles;
    toggles.SetEnabled("Test.Draw", false);
    scene.context.toggles = &toggles;
    FullscreenShader fullscreen("ReflectFixture");
    CompositeFrame frame = scene.MakeFrame();

    const bool drawn = frame.Fullscreen(fullscreen, "Test.Draw")
                           .Bind("sourceColor", frame.SceneColor())
                           .Bind("sourceMask", frame.Stamp(scene.maskHandle))
                           .Push(0.5f)
                           .Target(frame.PrivateTarget())
                           .Draw();
    EXPECT_FALSE(drawn);
    EXPECT_EQ(scene.builder.DeclaredPassCount(), 0u);
}

TEST(KitChainTest, TheSceneDepthIsAcknowledgedUnlessTheAuthorCoveredIt)
{
    HeadlessRenderGraphFixture fixture;
    if (!fixture.IsUsable()) {
        GTEST_SKIP() << fixture.SkipReason();
    }
    Scene scene(fixture);
    FullscreenShader fullscreen("ReflectFixture");
    CompositeFrame frame = scene.MakeFrame();

    EXPECT_TRUE(frame.Fullscreen(fullscreen, "Test.Draw")
                    .Bind("sourceColor", frame.Stamp(scene.maskHandle))
                    .Bind("sourceMask", frame.Stamp(scene.maskHandle))
                    .Push(0.5f)
                    .Target(frame.PrivateTarget())
                    .Draw());
    EXPECT_TRUE(scene.builder.PassReadsTexture(0, scene.viewHandle));
}

} // namespace
} // namespace gte::fx
