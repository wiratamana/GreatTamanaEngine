// Tier-1 tests for the pure parts of FullscreenPass: validation and pass declaration on a bare PassRecord.

#include "Renderer/RenderGraph/ShaderBindings.h"

#include <gtest/gtest.h>

#include <array>
#include <optional>
#include <string>
#include <vector>

namespace gte::rg {
namespace {

const TextureHandle kTarget{ 1, 1 };
const TextureHandle kSource{ 2, 1 };
const TextureHandle kDepth{ 3, 1 };

TextureDesc ColorDesc(std::uint32_t width = 64, std::uint32_t height = 32)
{
    TextureDesc desc;
    desc.width = width;
    desc.height = height;
    desc.format = PixelFormat::RGBA8_UNorm;
    desc.hasDepth = false;
    return desc;
}

ReflectedDescriptorBinding Sampler(std::uint32_t set, std::uint32_t binding)
{
    ReflectedDescriptorBinding b;
    b.set = set;
    b.binding = binding;
    b.type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    return b;
}

TEST(ValidateFullscreenTargetTest, MissingTargetIsRefused)
{
    EXPECT_TRUE(ValidateFullscreenTarget(std::nullopt).has_value());
}

TEST(ValidateFullscreenTargetTest, ZeroExtentIsRefused)
{
    EXPECT_TRUE(ValidateFullscreenTarget(ColorDesc(0, 32)).has_value());
    EXPECT_TRUE(ValidateFullscreenTarget(ColorDesc(64, 0)).has_value());
}

TEST(ValidateFullscreenTargetTest, UndefinedFormatIsRefused)
{
    TextureDesc desc = ColorDesc();
    desc.format = PixelFormat::Undefined;
    EXPECT_TRUE(ValidateFullscreenTarget(desc).has_value());
}

TEST(ValidateFullscreenTargetTest, DepthTargetIsRefused)
{
    TextureDesc desc = ColorDesc();
    desc.hasDepth = true;
    const std::optional<std::string> error = ValidateFullscreenTarget(desc);
    ASSERT_TRUE(error.has_value());
    EXPECT_NE(error->find("depth texture"), std::string::npos);
}

TEST(ValidateFullscreenTargetTest, ColorTargetIsAccepted)
{
    EXPECT_FALSE(ValidateFullscreenTarget(ColorDesc()).has_value());
}

TEST(ValidateFullscreenReflectionTest, SetZeroSamplersAreAccepted)
{
    ShaderReflectionResult reflection;
    reflection.descriptorBindings = { Sampler(0, 0), Sampler(0, 1) };
    EXPECT_FALSE(ValidateFullscreenReflection(reflection).has_value());
}

TEST(ValidateFullscreenReflectionTest, OtherSetsAreRefused)
{
    ShaderReflectionResult reflection;
    reflection.descriptorBindings = { Sampler(0, 0), Sampler(1, 0) };
    EXPECT_TRUE(ValidateFullscreenReflection(reflection).has_value());
}

TEST(ValidateFullscreenReflectionTest, NonSamplerBindingsAreRefused)
{
    ReflectedDescriptorBinding buffer = Sampler(0, 1);
    buffer.type = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    ShaderReflectionResult reflection;
    reflection.descriptorBindings = { Sampler(0, 0), buffer };
    EXPECT_TRUE(ValidateFullscreenReflection(reflection).has_value());
}

TEST(ValidateFullscreenReflectionTest, MissingSetZeroIsRefused)
{
    EXPECT_TRUE(ValidateFullscreenReflection(ShaderReflectionResult{}).has_value());
}

TEST(ValidateFullscreenReflectionTest, PushBlockAwayFromOffsetZeroIsRefused)
{
    ShaderReflectionResult reflection;
    reflection.descriptorBindings = { Sampler(0, 0) };
    reflection.hasPushConstantRange = true;
    reflection.pushConstantRange = ReflectedPushConstantRange{ 16, 4 };
    EXPECT_TRUE(ValidateFullscreenReflection(reflection).has_value());
}

BindingPlan TwoSamplerPlan()
{
    BindingPlan plan;
    PlannedBinding first;
    first.binding = 0;
    first.type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    first.texture = kSource;
    PlannedBinding second = first;
    second.binding = 1;
    plan.bindings = { first, second };
    return plan;
}

const std::array<TextureHandle, 1> kOneTarget{ kTarget };

TEST(DeclareFullscreenTest, DeclaresBindingsTargetAndDepthAcknowledgement)
{
    PassRecord record;
    RenderGraphBuilder::PassBuilder pass(record);
    const DescriptorSetLayoutHandle layout = static_cast<DescriptorSetLayoutHandle>(7);
    const std::array<TextureHandle, 1> acknowledgements{ kDepth };

    DeclareFullscreen(pass, TwoSamplerPlan(), layout, kOneTarget, std::nullopt, acknowledgements, std::nullopt);

    ASSERT_EQ(record.colorAttachments.size(), 1u);
    EXPECT_EQ(record.colorAttachments[0].handle, kTarget);
    EXPECT_FALSE(record.colorAttachments[0].clearColor.has_value());
    EXPECT_EQ(record.computeBindingLayout, layout);

    ASSERT_EQ(record.bindings.size(), 2u);
    EXPECT_EQ(record.bindings[0].binding, 0u);
    EXPECT_EQ(record.bindings[1].binding, 1u);

    ASSERT_EQ(record.reads.size(), 3u);
    EXPECT_FALSE(record.reads[0].isDepthResource);
    EXPECT_EQ(record.reads[2].texture, kDepth);
    EXPECT_TRUE(record.reads[2].isDepthResource);
}

TEST(DeclareFullscreenTest, ClearColorIsForwardedToTheAttachment)
{
    PassRecord record;
    RenderGraphBuilder::PassBuilder pass(record);
    const std::array<float, 4> clear{ 0.1f, 0.2f, 0.3f, 1.0f };

    DeclareFullscreen(pass, BindingPlan{}, static_cast<DescriptorSetLayoutHandle>(7), kOneTarget, clear, {}, std::nullopt);

    ASSERT_EQ(record.colorAttachments.size(), 1u);
    ASSERT_TRUE(record.colorAttachments[0].clearColor.has_value());
    EXPECT_EQ(*record.colorAttachments[0].clearColor, clear);
}

TEST(DeclareFullscreenTest, NoAcknowledgementsDeclaresNoDepthRead)
{
    PassRecord record;
    RenderGraphBuilder::PassBuilder pass(record);

    DeclareFullscreen(
        pass, BindingPlan{}, static_cast<DescriptorSetLayoutHandle>(7), kOneTarget, std::nullopt, {}, std::nullopt);

    EXPECT_TRUE(record.reads.empty());
}

TEST(DeclareFullscreenTest, MultipleTargetsDeclareOneAttachmentEachInOrder)
{
    PassRecord record;
    RenderGraphBuilder::PassBuilder pass(record);
    const std::array<TextureHandle, 3> targets{ TextureHandle{ 5, 1 }, TextureHandle{ 6, 1 }, TextureHandle{ 7, 1 } };

    DeclareFullscreen(pass, BindingPlan{}, static_cast<DescriptorSetLayoutHandle>(7), targets, std::nullopt, {}, std::nullopt);

    ASSERT_EQ(record.colorAttachments.size(), 3u);
    EXPECT_EQ(record.colorAttachments[0].handle, targets[0]);
    EXPECT_EQ(record.colorAttachments[1].handle, targets[1]);
    EXPECT_EQ(record.colorAttachments[2].handle, targets[2]);
}

TEST(DeclareFullscreenTest, DepthTestDeclaresADepthAttachmentWriteWithoutAClear)
{
    PassRecord record;
    RenderGraphBuilder::PassBuilder pass(record);

    DeclareFullscreen(pass, BindingPlan{}, static_cast<DescriptorSetLayoutHandle>(7), kOneTarget, std::nullopt, {}, kDepth);

    bool found = false;
    for (const ResourceUsage& write : record.writes) {
        found = found || (write.texture == kDepth && write.access == ResourceAccess::DepthStencilAttachmentReadWrite);
    }
    EXPECT_TRUE(found);
    EXPECT_FALSE(record.depthClearValue.has_value());
}

// --- PlanFullscreenAttachments -------------------------------------------------

std::optional<TextureDesc> ViewTargetDesc()
{
    TextureDesc desc = ColorDesc();
    desc.hasDepth = true;
    desc.depthFormat = PixelFormat::D32_Float;
    return desc;
}

TEST(PlanFullscreenAttachmentsTest, OneColorTargetBuildsTheKeyFromItsFormat)
{
    const std::optional<TextureDesc> targets[] = { ColorDesc() };
    const FullscreenAttachmentPlan plan =
        PlanFullscreenAttachments(targets, FullscreenDepthRequest{}, PixelFormat::D32_Float, BlendMode::Additive);
    ASSERT_TRUE(plan.Ok()) << plan.error;
    EXPECT_EQ(plan.key.colorCount, 1u);
    EXPECT_EQ(plan.key.colorFormats[0], PixelFormat::RGBA8_UNorm);
    EXPECT_EQ(plan.key.blend, BlendMode::Additive);
    EXPECT_FALSE(plan.key.depthTest);
    EXPECT_EQ(plan.key.depthFormat, PixelFormat::Undefined);
}

TEST(PlanFullscreenAttachmentsTest, MultipleTargetsKeepTheirOrderAndFormats)
{
    TextureDesc second = ColorDesc();
    second.format = PixelFormat::RGBA16_Float;
    const std::optional<TextureDesc> targets[] = { ColorDesc(), second };
    const FullscreenAttachmentPlan plan =
        PlanFullscreenAttachments(targets, FullscreenDepthRequest{}, PixelFormat::D32_Float, BlendMode::Opaque);
    ASSERT_TRUE(plan.Ok()) << plan.error;
    EXPECT_EQ(plan.key.colorCount, 2u);
    EXPECT_EQ(plan.key.colorFormats[0], PixelFormat::RGBA8_UNorm);
    EXPECT_EQ(plan.key.colorFormats[1], PixelFormat::RGBA16_Float);
}

TEST(PlanFullscreenAttachmentsTest, TargetsOfDifferentExtentAreRefused)
{
    const std::optional<TextureDesc> targets[] = { ColorDesc(64, 32), ColorDesc(32, 32) };
    const FullscreenAttachmentPlan plan =
        PlanFullscreenAttachments(targets, FullscreenDepthRequest{}, PixelFormat::D32_Float, BlendMode::Opaque);
    EXPECT_FALSE(plan.Ok());
    EXPECT_NE(plan.error.find("extent"), std::string::npos);
}

TEST(PlanFullscreenAttachmentsTest, NoTargetAndTooManyTargetsAreRefused)
{
    EXPECT_FALSE(PlanFullscreenAttachments({}, FullscreenDepthRequest{}, PixelFormat::D32_Float, BlendMode::Opaque).Ok());

    std::vector<std::optional<TextureDesc>> many(FullscreenKey::kMaxTargets + 1, ColorDesc());
    EXPECT_FALSE(PlanFullscreenAttachments(many, FullscreenDepthRequest{}, PixelFormat::D32_Float, BlendMode::Opaque).Ok());
}

TEST(PlanFullscreenAttachmentsTest, DepthTestOnTheViewTargetItselfIsAccepted)
{
    const std::optional<TextureDesc> targets[] = { ViewTargetDesc() };
    FullscreenDepthRequest depth;
    depth.enabled = true;
    depth.desc = ViewTargetDesc();
    depth.compare = CompareOp::Equal;
    depth.write = false;
    depth.sameTextureAsTarget = 0;

    const FullscreenAttachmentPlan plan = PlanFullscreenAttachments(targets, depth, PixelFormat::D32_Float, BlendMode::Opaque);
    ASSERT_TRUE(plan.Ok()) << plan.error;
    EXPECT_TRUE(plan.key.depthTest);
    EXPECT_FALSE(plan.key.depthWrite);
    EXPECT_EQ(plan.key.depthCompare, CompareOp::Equal);
    EXPECT_EQ(plan.key.depthFormat, PixelFormat::D32_Float);
}

TEST(PlanFullscreenAttachmentsTest, ADepthTextureWithoutDepthAspectIsRefused)
{
    const std::optional<TextureDesc> targets[] = { ColorDesc() };
    FullscreenDepthRequest depth;
    depth.enabled = true;
    depth.desc = ColorDesc();
    const FullscreenAttachmentPlan plan = PlanFullscreenAttachments(targets, depth, PixelFormat::D32_Float, BlendMode::Opaque);
    EXPECT_FALSE(plan.Ok());
    EXPECT_NE(plan.error.find("depth aspect"), std::string::npos);
}

TEST(PlanFullscreenAttachmentsTest, ADepthCompanionOnAnotherTargetThanTheDepthTextureIsRefused)
{
    const std::optional<TextureDesc> targets[] = { ViewTargetDesc() };
    FullscreenDepthRequest depth;
    depth.enabled = true;
    depth.desc = ViewTargetDesc(); // A different texture than the color target.
    const FullscreenAttachmentPlan plan = PlanFullscreenAttachments(targets, depth, PixelFormat::D32_Float, BlendMode::Opaque);
    EXPECT_FALSE(plan.Ok());
}

TEST(PlanFullscreenAttachmentsTest, UnknownDepthFormatFallsBackToTheRenderersDepthFormat)
{
    TextureDesc depthDesc = ViewTargetDesc().value();
    depthDesc.depthFormat = PixelFormat::Undefined;
    const std::optional<TextureDesc> targets[] = { ViewTargetDesc() };
    FullscreenDepthRequest depth;
    depth.enabled = true;
    depth.desc = depthDesc;
    depth.sameTextureAsTarget = 0;
    const FullscreenAttachmentPlan plan =
        PlanFullscreenAttachments(targets, depth, PixelFormat::D24_UNorm_S8_UInt, BlendMode::Opaque);
    ASSERT_TRUE(plan.Ok()) << plan.error;
    EXPECT_EQ(plan.key.depthFormat, PixelFormat::D24_UNorm_S8_UInt);
}

} // namespace
} // namespace gte::rg
