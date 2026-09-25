// editor-core-separation-9 campaign, PHASE1
// (PHASE1_RESOURCE_VOCABULARY_AND_ABI_FOUNDATION.md) - Tier-1 tests for the
// new, pure PluginRenderResourceTranslation.h mapping functions. No live
// VkDevice/Renderer involved at all - mirrors
// tests/Renderer/RenderGraph/RenderGraphTypesTests.cpp's own established
// "plain data in, plain data out" style for this exact class of pure
// enum/struct mapping logic.

#include "Core/Plugins/PluginRenderResourceTranslation.h"

#include <gtest/gtest.h>

namespace gte {
namespace {

// --- ToRgAccess() - one case per PluginResourceAccess enumerator ---------

TEST(PluginRenderResourceTranslationTest, ColorAttachmentWriteMapsToRgColorAttachmentWrite)
{
    EXPECT_EQ(ToRgAccess(PluginResourceAccess::ColorAttachmentWrite), rg::ResourceAccess::ColorAttachmentWrite);
}

TEST(PluginRenderResourceTranslationTest, ShaderReadMapsToRgShaderRead)
{
    EXPECT_EQ(ToRgAccess(PluginResourceAccess::ShaderRead), rg::ResourceAccess::ShaderRead);
}

TEST(PluginRenderResourceTranslationTest, ComputeShaderReadMapsToRgComputeShaderRead)
{
    EXPECT_EQ(ToRgAccess(PluginResourceAccess::ComputeShaderRead), rg::ResourceAccess::ComputeShaderRead);
}

TEST(PluginRenderResourceTranslationTest, ComputeShaderWriteMapsToRgComputeShaderWrite)
{
    EXPECT_EQ(ToRgAccess(PluginResourceAccess::ComputeShaderWrite), rg::ResourceAccess::ComputeShaderWrite);
}

// --- ToRgTextureDesc() - one case per PluginTextureDesc::Format enumerator,
// plus width/height round-trip and the fixed hasDepth == false rule --------

TEST(PluginRenderResourceTranslationTest, Rgba8UnormMapsToExpectedVkFormat)
{
    PluginTextureDesc desc;
    desc.width = 256;
    desc.height = 128;
    desc.format = PluginTextureDesc::Format::Rgba8Unorm;

    const rg::TextureDesc result = ToRgTextureDesc(desc);
    EXPECT_EQ(result.width, 256u);
    EXPECT_EQ(result.height, 128u);
    EXPECT_EQ(result.format, VK_FORMAT_R8G8B8A8_UNORM);
    EXPECT_FALSE(result.hasDepth);
}

TEST(PluginRenderResourceTranslationTest, Rgba16FloatMapsToExpectedVkFormat)
{
    PluginTextureDesc desc;
    desc.width = 64;
    desc.height = 64;
    desc.format = PluginTextureDesc::Format::Rgba16Float;

    const rg::TextureDesc result = ToRgTextureDesc(desc);
    EXPECT_EQ(result.format, VK_FORMAT_R16G16B16A16_SFLOAT);
    EXPECT_FALSE(result.hasDepth);
}

TEST(PluginRenderResourceTranslationTest, R32FloatMapsToExpectedVkFormat)
{
    PluginTextureDesc desc;
    desc.width = 32;
    desc.height = 32;
    desc.format = PluginTextureDesc::Format::R32Float;

    const rg::TextureDesc result = ToRgTextureDesc(desc);
    EXPECT_EQ(result.format, VK_FORMAT_R32_SFLOAT);
    EXPECT_FALSE(result.hasDepth);
}

// --- ToRgBufferDesc() - sizeBytes round-trip + fixed usage flags ----------

TEST(PluginRenderResourceTranslationTest, BufferDescRoundTripsSizeAndSetsFixedStorageBufferUsage)
{
    PluginBufferDesc desc;
    desc.sizeBytes = 4096;

    const rg::BufferDesc result = ToRgBufferDesc(desc);
    EXPECT_EQ(result.size, static_cast<VkDeviceSize>(4096));
    EXPECT_EQ(result.usage, static_cast<VkBufferUsageFlags>(VK_BUFFER_USAGE_STORAGE_BUFFER_BIT));
}

TEST(PluginRenderResourceTranslationTest, BufferDescAlwaysSetsSameUsageFlagsRegardlessOfSize)
{
    PluginBufferDesc small;
    small.sizeBytes = 16;
    PluginBufferDesc large;
    large.sizeBytes = 1024 * 1024;

    EXPECT_EQ(ToRgBufferDesc(small).usage, ToRgBufferDesc(large).usage);
}

} // namespace
} // namespace gte
