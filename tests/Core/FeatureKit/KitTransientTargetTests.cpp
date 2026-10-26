// Tests: fx::TransientTarget names per view, kept output, inert without a render callback.

#include "Core/FeatureKit/FeatureKit.h"
#include "Core/FeatureKit/FrameContext.h"
#include "ECS/Registry.h"
#include "Renderer/RenderGraph/RenderGraphBuilder.h"

#include "../../Fakes/RecordingLogSink.h"

#include <gtest/gtest.h>

#include <string>

namespace gte::fx {
namespace {

using test::CountErrorsByCategory;
using test::RecordingLogSink;
using test::ScopedLogSinkInstall;

Registry& Scene()
{
    static Registry registry;
    return registry;
}

struct Harness {
    explicit Harness(rg::ViewScope scope)
    {
        context.builder = &builder;
        context.scope = scope;
        context.event = rg::RenderPassEvent::AfterOpaques;
        context.serial = 1;
    }

    rg::RenderGraphBuilder builder;
    detail::FrameContext context;
};

TEST(FxTransientTargetTest, AcquireCreatesAKeptPoolTextureNamedPerView)
{
    for (const rg::ViewScope scope : { rg::ViewScope::Primary, rg::ViewScope::Secondary, rg::ViewScope::Shared }) {
        Harness h(scope);
        PostOpaqueFrame frame(View{}, Scene(), 1, rg::TextureHandle{}, rg::TextureHandle{}, &h.context);
        TransientTarget target("Scratch");

        const Texture texture = target.Acquire(frame, Extent2D{ 40, 30 }, PixelFormat::RGBA16_Float);
        ASSERT_TRUE(texture.IsValid());
        EXPECT_TRUE(frame.Owns(texture));

        const rg::CompiledGraphInput input = h.builder.Finish();
        ASSERT_EQ(input.textures.size(), 1u);
        EXPECT_EQ(std::string(input.textures[0].name), PersistentName("Scratch", scope));
        EXPECT_FALSE(input.textures[0].importInfo.isImported);
        EXPECT_EQ(input.textures[0].desc.width, 40u);
        EXPECT_EQ(input.textures[0].desc.height, 30u);
        EXPECT_EQ(input.textures[0].desc.format, PixelFormat::RGBA16_Float);
        EXPECT_TRUE(rg::HasFlag(input.textures[0].desc.usage, rg::TextureUsage::Storage));
        ASSERT_EQ(input.finalTextureOutputs.size(), 1u);
        EXPECT_EQ(input.finalTextureOutputs[0], texture.handle);
    }
}

TEST(FxTransientTargetTest, StorageCanBeSwitchedOffAndAZeroExtentIsClamped)
{
    Harness h(rg::ViewScope::Primary);
    PostOpaqueFrame frame(View{}, Scene(), 1, rg::TextureHandle{}, rg::TextureHandle{}, &h.context);
    TransientTarget target("Scratch");

    ASSERT_TRUE(target.Acquire(frame, Extent2D{ 0, 0 }, PixelFormat::RGBA8_UNorm, /*storage=*/false).IsValid());
    const rg::CompiledGraphInput input = h.builder.Finish();
    EXPECT_EQ(input.textures[0].desc.width, 1u);
    EXPECT_FALSE(rg::HasFlag(input.textures[0].desc.usage, rg::TextureUsage::Storage));
}

TEST(FxTransientTargetTest, AnUndefinedFormatIsRefusedAndLoggedOnce)
{
    RecordingLogSink sink;
    const ScopedLogSinkInstall guard(&sink);
    Harness h(rg::ViewScope::Primary);
    PostOpaqueFrame frame(View{}, Scene(), 1, rg::TextureHandle{}, rg::TextureHandle{}, &h.context);
    TransientTarget target("Scratch");

    EXPECT_FALSE(target.Acquire(frame, Extent2D{ 8, 8 }, PixelFormat::Undefined).IsValid());
    EXPECT_FALSE(target.Acquire(frame, Extent2D{ 8, 8 }, PixelFormat::Undefined).IsValid());
    EXPECT_EQ(CountErrorsByCategory(sink, "FeatureKit"), 1);
}

TEST(FxTransientTargetTest, WithoutAContextTheTargetIsInertAndLogsOnce)
{
    RecordingLogSink sink;
    const ScopedLogSinkInstall guard(&sink);
    PreOpaqueFrame frame(View{}, Scene(), 1);
    TransientTarget target("Scratch");

    EXPECT_FALSE(target.Acquire(frame, Extent2D{ 8, 8 }, PixelFormat::RGBA8_UNorm).IsValid());
    EXPECT_FALSE(target.Acquire(frame, Extent2D{ 8, 8 }, PixelFormat::RGBA8_UNorm).IsValid());
    EXPECT_EQ(CountErrorsByCategory(sink, "FeatureKit"), 1);
}

} // namespace
} // namespace gte::fx
