// Tier-1 tests for the fused PassBuilder helpers: one call declares both the access and the binding.

#include "Renderer/RenderGraph/RenderGraphBuilder.h"

#include <gtest/gtest.h>

namespace gte::rg {
namespace {

const TextureHandle kTexture{ 3, 1 };

TEST(PassBuilderFusedBindingTest, SampleDepthDeclaresDepthReadAndDepthSamplerBinding)
{
    PassRecord record;
    RenderGraphBuilder::PassBuilder pass(record);
    pass.SampleDepth(7, kTexture);

    ASSERT_EQ(record.reads.size(), 1u);
    EXPECT_TRUE(record.reads[0].isDepthResource);
    EXPECT_EQ(record.reads[0].access, ResourceAccess::ShaderRead);
    EXPECT_EQ(record.reads[0].texture, kTexture);
    EXPECT_TRUE(record.writes.empty());

    ASSERT_EQ(record.bindings.size(), 1u);
    EXPECT_EQ(record.bindings[0].binding, 7u);
    EXPECT_EQ(record.bindings[0].kind, BindingKind::CombinedImageSampler);
    EXPECT_EQ(record.bindings[0].textureAspect, TextureAspect::Depth);
    EXPECT_EQ(record.bindings[0].textureHandle, kTexture);
}

TEST(PassBuilderFusedBindingTest, SampleTextureDeclaresColorReadAndColorSamplerBinding)
{
    PassRecord record;
    RenderGraphBuilder::PassBuilder pass(record);
    pass.SampleTexture(2, kTexture);

    ASSERT_EQ(record.reads.size(), 1u);
    EXPECT_FALSE(record.reads[0].isDepthResource);
    EXPECT_EQ(record.reads[0].access, ResourceAccess::ShaderRead);

    ASSERT_EQ(record.bindings.size(), 1u);
    EXPECT_EQ(record.bindings[0].binding, 2u);
    EXPECT_EQ(record.bindings[0].kind, BindingKind::CombinedImageSampler);
    EXPECT_EQ(record.bindings[0].textureAspect, TextureAspect::Color);
    EXPECT_EQ(record.bindings[0].explicitSampler, SamplerHandle::Invalid);
}

TEST(PassBuilderFusedBindingTest, SampleTextureForwardsExplicitSampler)
{
    PassRecord record;
    RenderGraphBuilder::PassBuilder pass(record);
    const SamplerHandle sampler = static_cast<SamplerHandle>(9);
    pass.SampleTexture(0, kTexture, sampler);

    ASSERT_EQ(record.bindings.size(), 1u);
    EXPECT_EQ(record.bindings[0].explicitSampler, sampler);
}

TEST(PassBuilderFusedBindingTest, WriteStorageImageDeclaresComputeWriteAndStorageImageBinding)
{
    PassRecord record;
    RenderGraphBuilder::PassBuilder pass(record);
    pass.WriteStorageImage(3, kTexture);

    ASSERT_EQ(record.writes.size(), 1u);
    EXPECT_EQ(record.writes[0].access, ResourceAccess::ComputeShaderWrite);
    EXPECT_EQ(record.writes[0].texture, kTexture);
    EXPECT_TRUE(record.reads.empty());

    ASSERT_EQ(record.bindings.size(), 1u);
    EXPECT_EQ(record.bindings[0].binding, 3u);
    EXPECT_EQ(record.bindings[0].kind, BindingKind::StorageImage);
    EXPECT_EQ(record.bindings[0].textureHandle, kTexture);
}

TEST(PassBuilderFusedBindingTest, ExternalStorageBufferDeclaresBindingOnly)
{
    PassRecord record;
    RenderGraphBuilder::PassBuilder pass(record);
    const BufferObjectHandle external = static_cast<BufferObjectHandle>(11);
    pass.ReadStorageBuffer(4, external);

    EXPECT_TRUE(record.reads.empty());
    EXPECT_TRUE(record.writes.empty());
    ASSERT_EQ(record.bindings.size(), 1u);
    EXPECT_EQ(record.bindings[0].binding, 4u);
    EXPECT_EQ(record.bindings[0].bufferObjectHandle, external);
}

TEST(PassBuilderFusedBindingTest, BindingNumbersAreCopiedExactlyInCallOrder)
{
    PassRecord record;
    RenderGraphBuilder::PassBuilder pass(record);
    pass.SampleDepth(1, kTexture);
    pass.SampleDepth(2, kTexture);
    pass.WriteStorageImage(3, kTexture);

    ASSERT_EQ(record.bindings.size(), 3u);
    EXPECT_EQ(record.bindings[0].binding, 1u);
    EXPECT_EQ(record.bindings[1].binding, 2u);
    EXPECT_EQ(record.bindings[2].binding, 3u);
}

} // namespace
} // namespace gte::rg
