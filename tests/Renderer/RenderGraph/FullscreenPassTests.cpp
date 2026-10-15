// Tier-2 (headless GPU) tests for rg::FullscreenPass and graphics rg::Shader.
// Passes are declared on a bare builder (never executed); pipelines are real.

#include "Renderer/RenderGraph/ComputePass.h"
#include "Renderer/RenderGraph/FullscreenPass.h"
#include "Renderer/RenderGraph/RenderGraph.h"
#include "Renderer/RenderGraph/RenderGraphBuilder.h"
#include "Renderer/RenderGraph/Shader.h"

#include "../../Fakes/HeadlessRenderGraphFixture.h"
#include "../../Fakes/RecordingLogSink.h"

#include <gtest/gtest.h>

#include <array>
#include <cstdint>
#include <string>

namespace gte::rg {
namespace {

using test::CountByCategory;
using test::RecordingLogSink;
using test::ScopedLogSinkInstall;

constexpr ExecuteTimingMode kSync = ExecuteTimingMode::SynchronousImmediateReadback;

TextureDesc ColorDesc(PixelFormat format)
{
    TextureDesc desc;
    desc.width = 64;
    desc.height = 64;
    desc.format = format;
    desc.hasDepth = false;
    return desc;
}

// A bare builder with a renderer and a frame counter, enough to build shaders and declare passes.
struct DeclareScene {
    DeclareScene(HeadlessRenderGraphFixture& fixture, std::uint64_t frame, PixelFormat targetFormat)
    {
        builder.SetRenderer(&fixture.GetRenderer());
        builder.SetPersistentResourceCache(nullptr, kSync, frame);
        source = builder.CreateTexture("Source", ColorDesc(PixelFormat::RGBA8_UNorm));
        mask = builder.CreateTexture("Mask", ColorDesc(PixelFormat::R8_UNorm));
        target = builder.CreateTexture("Target", ColorDesc(targetFormat));
    }

    RenderGraphBuilder builder;
    TextureHandle source;
    TextureHandle mask;
    TextureHandle target;
};

bool DrawValid(HeadlessRenderGraphFixture& fixture, Shader& shader, std::uint64_t frame,
    PixelFormat format = PixelFormat::RGBA8_UNorm, BlendMode blend = BlendMode::Opaque)
{
    DeclareScene scene(fixture, frame, format);
    return FullscreenPass(scene.builder, shader, "Test.Fullscreen", ViewScope::GameView)
        .Bind("sourceColor", scene.source)
        .Bind("sourceMask", scene.mask)
        .Push(0.5f)
        .Target(scene.target)
        .Blend(blend)
        .Draw();
}

TEST(FullscreenPassTest, ValidChainDeclaresOnePassWithItsAttachmentAndReads)
{
    HeadlessRenderGraphFixture fixture;
    if (!fixture.IsUsable()) {
        GTEST_SKIP() << fixture.SkipReason();
    }
    Shader shader("ReflectFixture", ShaderKind::Graphics);

    DeclareScene scene(fixture, 1, PixelFormat::RGBA8_UNorm);
    const bool drawn = FullscreenPass(scene.builder, shader, "Test.Fullscreen", ViewScope::GameView)
                           .Bind("sourceColor", scene.source)
                           .Bind("sourceMask", scene.mask)
                           .Push(0.5f)
                           .Target(scene.target)
                           .Draw();

    EXPECT_TRUE(drawn);
    EXPECT_EQ(scene.builder.DeclaredPassCount(), 1u);
    EXPECT_TRUE(scene.builder.PassReadsTexture(0, scene.source));
    EXPECT_TRUE(scene.builder.PassReadsTexture(0, scene.mask));
}

TEST(FullscreenPassTest, EveryFormatAndBlendVariantBuilds)
{
    HeadlessRenderGraphFixture fixture;
    if (!fixture.IsUsable()) {
        GTEST_SKIP() << fixture.SkipReason();
    }
    Shader shader("ReflectFixture", ShaderKind::Graphics);

    std::uint64_t frame = 1;
    for (const PixelFormat format : { PixelFormat::RGBA8_UNorm, PixelFormat::BGRA8_UNorm, PixelFormat::RGBA16_Float }) {
        for (const BlendMode blend : { BlendMode::Opaque, BlendMode::AlphaBlend, BlendMode::Additive }) {
            EXPECT_TRUE(DrawValid(fixture, shader, frame++, format, blend))
                << "format " << static_cast<int>(format) << " blend " << static_cast<int>(blend);
        }
    }
}

TEST(FullscreenPassTest, UnknownBindingNameDeclaresNothingAndIsLoggedOnce)
{
    HeadlessRenderGraphFixture fixture;
    if (!fixture.IsUsable()) {
        GTEST_SKIP() << fixture.SkipReason();
    }
    RecordingLogSink sink;
    const ScopedLogSinkInstall logGuard(&sink);
    Shader shader("ReflectFixture", ShaderKind::Graphics);

    for (std::uint64_t frame = 1; frame <= 2; ++frame) {
        DeclareScene scene(fixture, frame, PixelFormat::RGBA8_UNorm);
        const bool drawn = FullscreenPass(scene.builder, shader, "Test.Fullscreen", ViewScope::GameView)
                               .Bind("nope", scene.source)
                               .Bind("sourceMask", scene.mask)
                               .Push(0.5f)
                               .Target(scene.target)
                               .Draw();
        EXPECT_FALSE(drawn);
        EXPECT_EQ(scene.builder.DeclaredPassCount(), 0u);
    }
    EXPECT_EQ(CountByCategory(sink, "PassChain"), 1);
}

TEST(FullscreenPassTest, WrongPushSizeDeclaresNothing)
{
    HeadlessRenderGraphFixture fixture;
    if (!fixture.IsUsable()) {
        GTEST_SKIP() << fixture.SkipReason();
    }
    Shader shader("ReflectFixture", ShaderKind::Graphics);

    DeclareScene scene(fixture, 1, PixelFormat::RGBA8_UNorm);
    const bool drawn = FullscreenPass(scene.builder, shader, "Test.Fullscreen", ViewScope::GameView)
                           .Bind("sourceColor", scene.source)
                           .Bind("sourceMask", scene.mask)
                           .Push(std::uint64_t{ 1 })
                           .Target(scene.target)
                           .Draw();

    EXPECT_FALSE(drawn);
    EXPECT_EQ(scene.builder.DeclaredPassCount(), 0u);
}

TEST(FullscreenPassTest, MissingTargetDeclaresNothing)
{
    HeadlessRenderGraphFixture fixture;
    if (!fixture.IsUsable()) {
        GTEST_SKIP() << fixture.SkipReason();
    }
    Shader shader("ReflectFixture", ShaderKind::Graphics);

    DeclareScene scene(fixture, 1, PixelFormat::RGBA8_UNorm);
    const bool drawn = FullscreenPass(scene.builder, shader, "Test.Fullscreen", ViewScope::GameView)
                           .Bind("sourceColor", scene.source)
                           .Bind("sourceMask", scene.mask)
                           .Push(0.5f)
                           .Draw();

    EXPECT_FALSE(drawn);
    EXPECT_EQ(scene.builder.DeclaredPassCount(), 0u);
}

TEST(FullscreenPassTest, MissingShaderFilesAreInertAndLoggedOnce)
{
    HeadlessRenderGraphFixture fixture;
    if (!fixture.IsUsable()) {
        GTEST_SKIP() << fixture.SkipReason();
    }
    RecordingLogSink sink;
    const ScopedLogSinkInstall logGuard(&sink);
    Shader shader("DoesNotExist", ShaderKind::Graphics);

    EXPECT_FALSE(DrawValid(fixture, shader, 1));
    EXPECT_FALSE(DrawValid(fixture, shader, 2));
    EXPECT_EQ(CountByCategory(sink, "Shader"), 1);
}

TEST(FullscreenPassTest, ComputeShaderCannotBackAFullscreenPass)
{
    HeadlessRenderGraphFixture fixture;
    if (!fixture.IsUsable()) {
        GTEST_SKIP() << fixture.SkipReason();
    }
    Shader shader("ReflectFixture", ShaderKind::Compute);

    EXPECT_FALSE(DrawValid(fixture, shader, 1));
}

TEST(ComputePassKindTest, GraphicsShaderCannotBackAComputePass)
{
    HeadlessRenderGraphFixture fixture;
    if (!fixture.IsUsable()) {
        GTEST_SKIP() << fixture.SkipReason();
    }
    Shader shader("ReflectFixture", ShaderKind::Graphics);

    DeclareScene scene(fixture, 1, PixelFormat::RGBA8_UNorm);
    const bool dispatched =
        ComputePass(scene.builder, shader, "Test.Compute", ViewScope::GameView, RenderPassEvent::AfterOpaques)
            .Dispatch(Extent2D{ 8, 8 });

    EXPECT_FALSE(dispatched);
    EXPECT_EQ(scene.builder.DeclaredPassCount(), 0u);
}

TEST(FullscreenPassTest, EvictedPipelineIsFreedOnlyAfterTheRetireFramesPass)
{
    HeadlessRenderGraphFixture fixture;
    if (!fixture.IsUsable()) {
        GTEST_SKIP() << fixture.SkipReason();
    }
    Shader shader("ReflectFixture", ShaderKind::Graphics);

    // Nine distinct (format, blend) variants overflow the 8-slot cache once, in one frame each.
    std::uint64_t frame = 1;
    for (const PixelFormat format : { PixelFormat::RGBA8_UNorm, PixelFormat::BGRA8_UNorm, PixelFormat::RGBA16_Float }) {
        for (const BlendMode blend : { BlendMode::Opaque, BlendMode::AlphaBlend, BlendMode::Additive }) {
            ASSERT_TRUE(DrawValid(fixture, shader, frame++, format, blend));
        }
    }
    EXPECT_EQ(shader.RetiredPipelineCount(), 1u);

    // Same frame: nothing is freed yet.
    ASSERT_TRUE(DrawValid(fixture, shader, frame - 1, PixelFormat::RGBA16_Float, BlendMode::Additive));
    EXPECT_EQ(shader.RetiredPipelineCount(), 1u);

    // A few frames later the victim is released.
    ASSERT_TRUE(DrawValid(fixture, shader, frame + 3, PixelFormat::RGBA16_Float, BlendMode::Additive));
    EXPECT_EQ(shader.RetiredPipelineCount(), 0u);
}

TEST(FullscreenPassTest, ErrorsPastTheOnceCapAreCoveredByOneHonestSuppressedNotice)
{
    HeadlessRenderGraphFixture fixture;
    if (!fixture.IsUsable()) {
        GTEST_SKIP() << fixture.SkipReason();
    }
    RecordingLogSink sink;
    const ScopedLogSinkInstall logGuard(&sink);
    Shader shader("ReflectFixture", ShaderKind::Graphics);

    // 64 distinct messages fit; every later distinct message must not be logged again.
    constexpr int kAttempts = 70;
    for (int i = 0; i < kAttempts; ++i) {
        const std::string badName = "nope" + std::to_string(i);
        DeclareScene scene(fixture, static_cast<std::uint64_t>(i + 1), PixelFormat::RGBA8_UNorm);
        EXPECT_FALSE(FullscreenPass(scene.builder, shader, "Test.Fullscreen", ViewScope::GameView)
                         .Bind(badName, scene.source)
                         .Bind("sourceMask", scene.mask)
                         .Push(0.5f)
                         .Target(scene.target)
                         .Draw());
    }

    EXPECT_EQ(CountByCategory(sink, "PassChain"), 65);
    ASSERT_FALSE(sink.entries.empty());
    int notices = 0;
    for (const RecordingLogSink::Recorded& entry : sink.entries) {
        notices += entry.message.find("suppressed") != std::string::npos ? 1 : 0;
    }
    EXPECT_EQ(notices, 1);
}

} // namespace
} // namespace gte::rg
