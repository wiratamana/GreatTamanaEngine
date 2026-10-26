// Tests: kit resources from a shared frame, persistent volumes, 3D bindings, blit filter, named params.

#include "Core/FeatureKit/FeatureKit.h"
#include "Core/FeatureKit/FrameContext.h"
#include "ECS/Registry.h"
#include "Renderer/GraphicsHandleRegistry.h"
#include "Renderer/RenderTexture.h"
#include "Renderer/VolumeTexture.h"
#include "Renderer/RenderGraph/ShaderBindings.h"

#include "../../Fakes/HeadlessRenderGraphFixture.h"
#include "../../Fakes/RecordingLogSink.h"

#include <gtest/gtest.h>

#include <array>
#include <optional>
#include <string_view>
#include <vector>

namespace gte::fx {
namespace {

using test::CountErrorsByCategory;
using test::RecordingLogSink;
using test::ScopedLogSinkInstall;

Registry& FakeScene()
{
    static Registry registry;
    return registry;
}

TEST(FxSharedResourceTest, ASharedFrameNamesItsTargetsWithoutAViewSuffix)
{
    const SharedFrame frame(FakeScene(), 3);
    EXPECT_EQ(frame.Scope(), rg::ViewScope::Shared);
    EXPECT_EQ(PersistentName("Lut.Transmittance", frame.Scope()), "Lut.Transmittance");
    EXPECT_EQ(PersistentName("Lut.Transmittance", rg::ViewScope::Primary), "Lut.Transmittance.GameView");
}

TEST(FxSharedResourceTest, ResourcesOfASharedFrameWithoutAContextAreInertAndLogOnce)
{
    RecordingLogSink sink;
    const ScopedLogSinkInstall guard(&sink);
    SharedFrame frame(FakeScene(), 3);
    PersistentTarget target("Lut.Transmittance");
    PersistentVolume volume("Lut.Volume", VolumeSize{ 4, 4, 4 }, PixelFormat::RGBA16_Float);

    EXPECT_FALSE(target.Acquire(frame, Extent2D{ 8, 8 }, PixelFormat::RGBA16_Float).IsValid());
    EXPECT_FALSE(target.Acquire(frame, Extent2D{ 8, 8 }, PixelFormat::RGBA16_Float).IsValid());
    EXPECT_FALSE(volume.Acquire(frame).IsValid());
    EXPECT_EQ(CountErrorsByCategory(sink, "FeatureKit"), 1); // A frame without a context logs once.
}

TEST(FxSharedResourceTest, AVolumeSizeNeedsEveryAxis)
{
    EXPECT_TRUE(IsValidVolumeSize(VolumeSize{ 128, 128, 32 }));
    EXPECT_FALSE(IsValidVolumeSize(VolumeSize{ 0, 128, 32 }));
    EXPECT_FALSE(IsValidVolumeSize(VolumeSize{ 128, 0, 32 }));
    EXPECT_FALSE(IsValidVolumeSize(VolumeSize{ 128, 128, 0 }));
}

TEST(FxBlitTest, TheKitFilterReachesTheBlitRecord)
{
    for (const BlitFilter filter : { BlitFilter::Linear, BlitFilter::Nearest }) {
        rg::RenderGraphBuilder builder;
        const rg::TextureHandle src = builder.CreateTexture("BlitSrc", rg::TextureDesc{ 64, 64, PixelFormat::RGBA8_UNorm });
        const rg::TextureHandle dst = builder.CreateTexture("BlitDst", rg::TextureDesc{ 32, 32, PixelFormat::RGBA8_UNorm });
        detail::FrameContext context;
        context.builder = &builder;
        context.scope = rg::ViewScope::Primary;
        context.event = rg::RenderPassEvent::AfterOpaques;
        context.serial = 1;
        PreOpaqueFrame frame(View{}, FakeScene(), 1, &context);

        ASSERT_TRUE(frame.Blit("Test.Blit", frame.Stamp(src), frame.Stamp(dst), filter));
        const rg::CompiledGraphInput input = builder.Finish();
        ASSERT_EQ(input.passes.size(), 1u);
        ASSERT_TRUE(input.passes[0].blitCommand.has_value());
        EXPECT_EQ(input.passes[0].blitCommand->filter,
            filter == BlitFilter::Nearest ? rg::BlitFilter::Nearest : rg::BlitFilter::Linear);
        EXPECT_EQ(input.passes[0].viewScope, rg::ViewScope::Primary);
    }
}

// A volume is an ordinary color texture for name binding: sampled or stored, never as a depth view.
std::optional<rg::TextureDesc> VolumeDesc(rg::TextureHandle)
{
    rg::TextureDesc desc;
    desc.dimension = TextureDimension::Tex3D;
    desc.width = 128;
    desc.height = 128;
    desc.depth = 32;
    desc.format = PixelFormat::RGBA16_Float;
    return desc;
}

std::vector<ReflectedDescriptorBinding> VolumeBindings()
{
    ReflectedDescriptorBinding sampled;
    sampled.set = 0;
    sampled.binding = 0;
    sampled.type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    sampled.name = "volumeTex";
    sampled.access = ReflectedAccess::ReadOnly;
    ReflectedDescriptorBinding stored = sampled;
    stored.binding = 1;
    stored.type = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
    stored.name = "volumeOut";
    stored.access = ReflectedAccess::WriteOnly;
    return { sampled, stored };
}

TEST(FxVolumeBindingTest, AVolumeBindsToASamplerAndAStorageImageByName)
{
    const rg::TextureHandle a{ 1, 1 };
    const rg::TextureHandle b{ 2, 1 };
    std::vector<rg::BindingArgument> args(2);
    args[0].name = "volumeTex";
    args[0].texture = a;
    args[1].name = "volumeOut";
    args[1].texture = b;

    const rg::BindingPlanResult result = rg::PlanBindings(VolumeBindings(), args, VolumeDesc);
    EXPECT_TRUE(result.Ok()) << result.error;
    EXPECT_EQ(result.plan.bindings.size(), 2u);
}

TEST(FxVolumeBindingTest, ADepthViewOfAVolumeIsRefused)
{
    std::vector<rg::BindingArgument> args(2);
    args[0].name = "volumeTex";
    args[0].texture = rg::TextureHandle{ 1, 1 };
    args[0].kind = rg::BindingResourceKind::DepthTexture;
    args[1].name = "volumeOut";
    args[1].texture = rg::TextureHandle{ 2, 1 };

    EXPECT_FALSE(rg::PlanBindings(VolumeBindings(), args, VolumeDesc).Ok());
}

TEST(FxVolumeBindingTest, ADeclaredVolumeStorageWriteIsAComputeWrite)
{
    std::vector<rg::BindingArgument> args(2);
    args[0].name = "volumeTex";
    args[0].texture = rg::TextureHandle{ 1, 1 };
    args[1].name = "volumeOut";
    args[1].texture = rg::TextureHandle{ 2, 1 };
    const rg::BindingPlanResult result = rg::PlanBindings(VolumeBindings(), args, VolumeDesc);
    ASSERT_TRUE(result.Ok()) << result.error;

    rg::PassRecord record;
    rg::RenderGraphBuilder::PassBuilder pass(record);
    rg::DeclareBindings(pass, result.plan, static_cast<DescriptorSetLayoutHandle>(7));
    ASSERT_EQ(record.writes.size(), 1u);
    EXPECT_EQ(record.writes[0].access, rg::ResourceAccess::ComputeShaderWrite);
    ASSERT_EQ(record.reads.size(), 1u);
    EXPECT_EQ(record.reads[0].access, rg::ResourceAccess::ShaderRead);
}

// A shader with several parameter blocks names each one.
template <typename Chain, typename Params>
concept NamesItsParams = requires(Chain& chain, Params& params, const int& value) {
    chain.Params(std::string_view("shared"), params, value);
};
static_assert(NamesItsParams<ComputeChain, GpuParams<int>>);
// Tier-2: real volumes imported with a sampler bind through the kit compute chain, with a named parameter block.
TEST(FxVolumeGpuTest, ARealVolumeIsSampledAndStoredThroughTheComputeChain)
{
    HeadlessRenderGraphFixture fixture;
    if (!fixture.IsUsable()) {
        GTEST_SKIP() << fixture.SkipReason();
    }
    Renderer& renderer = fixture.GetRenderer();
    VolumeTexture input = renderer.CreateVolumeTexture(16, 16, 4, PixelFormat::RGBA16_Float, "InVolume");
    VolumeTexture output = renderer.CreateVolumeTexture(16, 16, 4, PixelFormat::RGBA16_Float, "OutVolume");
    RenderTexture color = renderer.CreateRenderTexture(16, 16, PixelFormat::RGBA8_UNorm, "Color", nullptr, false, false,
        /*companion=*/false, /*color=*/true);
    CachedSamplerHandle inputSampler;
    CachedSamplerHandle outputSampler;

    rg::RenderGraphBuilder builder;
    builder.SetRenderer(&renderer);
    builder.SetPersistentResourceCache(nullptr, rg::ExecuteTimingMode::SynchronousImmediateReadback, 1);
    const rg::TextureHandle in = builder.ImportVolumeTexture(
        "InVolume", input.Target(), ImageLayout::Undefined, inputSampler.Resolve(input.Sampler()));
    const rg::TextureHandle out = builder.ImportVolumeTexture(
        "OutVolume", output.Target(), ImageLayout::Undefined, outputSampler.Resolve(output.Sampler()));
    const rg::TextureHandle col = builder.ImportTexture(
        "Color", color.Target(), ImageLayout::Undefined, SamplerHandle::Invalid, SamplerHandle::Invalid);

    detail::FrameContext context;
    context.builder = &builder;
    context.scope = rg::ViewScope::Shared;
    context.event = rg::RenderPassEvent::PreOpaques;
    context.serial = 1;
    SharedFrame frame(FakeScene(), 1, &context);
    ComputeShader shader("ReflectFixture");
    GpuParams<std::array<float, 16>> params;

    const bool declared = frame.Compute(shader, "Test.Volume")
                              .Params("params", params, std::array<float, 16>{})
                              .Bind("inputDepthA", frame.Stamp(in))
                              .Bind("inputDepthB", frame.Stamp(col))
                              .Bind("maskOutput", frame.Stamp(out))
                              .Dispatch(Extent2D{ 16, 16 }, 4);
    EXPECT_TRUE(declared);
    EXPECT_EQ(builder.DeclaredPassCount(), 1u);
    if (inputSampler.handle != SamplerHandle::Invalid) {
        UnregisterSampler(inputSampler.handle);
    }
    if (outputSampler.handle != SamplerHandle::Invalid) {
        UnregisterSampler(outputSampler.handle);
    }
}

} // namespace
} // namespace gte::fx
