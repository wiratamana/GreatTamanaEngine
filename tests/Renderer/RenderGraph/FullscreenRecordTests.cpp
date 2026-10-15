// Tier-1 tests for the pure parts of FullscreenPass: validation and pass declaration on a bare PassRecord.

#include "Renderer/RenderGraph/ShaderBindings.h"

#include <gtest/gtest.h>

#include <array>
#include <optional>

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

TEST(DeclareFullscreenTest, DeclaresBindingsTargetAndDepthAcknowledgement)
{
    PassRecord record;
    RenderGraphBuilder::PassBuilder pass(record);
    const DescriptorSetLayoutHandle layout = static_cast<DescriptorSetLayoutHandle>(7);
    const std::array<TextureHandle, 1> acknowledgements{ kDepth };

    DeclareFullscreen(pass, TwoSamplerPlan(), layout, kTarget, std::nullopt, acknowledgements);

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

    DeclareFullscreen(pass, BindingPlan{}, static_cast<DescriptorSetLayoutHandle>(7), kTarget, clear, {});

    ASSERT_EQ(record.colorAttachments.size(), 1u);
    ASSERT_TRUE(record.colorAttachments[0].clearColor.has_value());
    EXPECT_EQ(*record.colorAttachments[0].clearColor, clear);
}

TEST(DeclareFullscreenTest, NoAcknowledgementsDeclaresNoDepthRead)
{
    PassRecord record;
    RenderGraphBuilder::PassBuilder pass(record);

    DeclareFullscreen(pass, BindingPlan{}, static_cast<DescriptorSetLayoutHandle>(7), kTarget, std::nullopt, {});

    EXPECT_TRUE(record.reads.empty());
}

} // namespace
} // namespace gte::rg
