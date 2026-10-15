// Tier-1 tests: PersistentTarget ownership rule and RenderGraphBuilder::DescOf on a bare builder.

#include "Renderer/RenderGraph/PersistentTarget.h"
#include "Renderer/RenderGraph/RenderFeatureScope.h"

#include "../../Fakes/RecordingLogSink.h"

#include <gtest/gtest.h>

namespace gte::rg {
namespace {

TEST(PersistentTargetTest, WithoutAFeatureScopeTheRequestIsRefused)
{
    RenderGraphBuilder builder;
    PersistentTarget target;
    const TextureHandle handle = target.Acquire(builder, "Mask", Extent2D{ 64, 64 }, PixelFormat::R8_UNorm);
    EXPECT_FALSE(handle.IsValid());
}

TEST(PersistentTargetTest, UndefinedFormatIsRefusedInsideAScope)
{
    RenderGraphBuilder builder;
    const RenderFeatureScope scope(builder, "Feature");
    PersistentTarget target;
    const TextureHandle handle = target.Acquire(builder, "Mask", Extent2D{ 64, 64 }, PixelFormat::Undefined);
    EXPECT_FALSE(handle.IsValid());
}

TEST(PersistentTargetTest, RefusalIsLoggedOncePerToken)
{
    test::RecordingLogSink sink;
    const test::ScopedLogSinkInstall logGuard(&sink);
    RenderGraphBuilder builder;
    PersistentTextureCacheToken token;
    const TextureDesc desc{ 8, 8, PixelFormat::R8_UNorm, false };
    EXPECT_FALSE(token.refusalLogged);
    EXPECT_FALSE(builder.GetOrCreatePersistentTexture(token, "Mask", desc).IsValid());
    EXPECT_TRUE(token.refusalLogged);
    EXPECT_FALSE(builder.GetOrCreatePersistentTexture(token, "Mask", desc).IsValid());
    EXPECT_EQ(test::CountErrorsByCategory(sink, "RenderGraphBuilder"), 1);
}

TEST(RenderFeatureScopeTest, TracksOpenState)
{
    RenderGraphBuilder builder;
    EXPECT_FALSE(builder.HasOwningFeatureScope());
    {
        const RenderFeatureScope scope(builder, "Feature");
        EXPECT_TRUE(builder.HasOwningFeatureScope());
    }
    EXPECT_FALSE(builder.HasOwningFeatureScope());
}

TEST(BuilderDescOfTest, CreatedTextureReportsItsDesc)
{
    RenderGraphBuilder builder;
    const TextureDesc desc{ 128, 64, PixelFormat::RGBA8_UNorm, false };
    const TextureHandle handle = builder.CreateTexture("Tex", desc);

    const std::optional<TextureDesc> found = builder.DescOf(handle);
    ASSERT_TRUE(found.has_value());
    EXPECT_EQ(*found, desc);
}

TEST(BuilderDescOfTest, ImportedTextureReportsTheRealImageExtent)
{
    RenderGraphBuilder builder;
    RenderTarget target;
    target.extent = VkExtent2D{ 320, 200 };
    target.format = VK_FORMAT_R8G8B8A8_UNORM;
    const TextureHandle handle = builder.ImportTexture("Imported", target, ImageLayout::Undefined);

    const std::optional<TextureDesc> found = builder.DescOf(handle);
    ASSERT_TRUE(found.has_value());
    EXPECT_EQ(found->width, 320u);
    EXPECT_EQ(found->height, 200u);
    EXPECT_EQ(found->format, PixelFormat::RGBA8_UNorm);
    EXPECT_FALSE(found->hasDepth);
}

TEST(BuilderDescOfTest, InvalidOrUnknownHandleHasNoDesc)
{
    RenderGraphBuilder builder;
    EXPECT_FALSE(builder.DescOf(TextureHandle{}).has_value());
    EXPECT_FALSE(builder.DescOf(TextureHandle{ 5, 1 }).has_value());
}

TEST(BuilderDescOfTest, BareBuilderHasNoRendererAndIsNotPipelined)
{
    const RenderGraphBuilder builder;
    EXPECT_EQ(builder.RendererOrNull(), nullptr);
    EXPECT_FALSE(builder.IsPersistentRegimePipelined());
}

} // namespace
} // namespace gte::rg
