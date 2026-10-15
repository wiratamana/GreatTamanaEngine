// Tier-1 tests for the engine stage mask <-> Vulkan stage flag conversion.

#include "Renderer/Vulkan/ShaderStageMask.h"

#include <gtest/gtest.h>

namespace gte {
namespace {

TEST(ShaderStageMaskTest, EachStageMapsToItsVulkanBit)
{
    EXPECT_EQ(ToVkShaderStageFlags(ShaderStageMask::Vertex), static_cast<VkShaderStageFlags>(VK_SHADER_STAGE_VERTEX_BIT));
    EXPECT_EQ(ToVkShaderStageFlags(ShaderStageMask::Fragment), static_cast<VkShaderStageFlags>(VK_SHADER_STAGE_FRAGMENT_BIT));
    EXPECT_EQ(ToVkShaderStageFlags(ShaderStageMask::Compute), static_cast<VkShaderStageFlags>(VK_SHADER_STAGE_COMPUTE_BIT));
    EXPECT_EQ(ToVkShaderStageFlags(ShaderStageMask::None), 0u);
}

TEST(ShaderStageMaskTest, EveryCombinationRoundTrips)
{
    for (std::uint8_t bits = 0; bits < 8; ++bits) {
        const ShaderStageMask mask = static_cast<ShaderStageMask>(bits);
        EXPECT_EQ(ToShaderStageMask(ToVkShaderStageFlags(mask)), mask) << "bits " << static_cast<int>(bits);
    }
}

TEST(ShaderStageMaskTest, UnmodelledStagesAreDropped)
{
    const VkShaderStageFlags flags = VK_SHADER_STAGE_GEOMETRY_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;
    EXPECT_EQ(ToShaderStageMask(flags), ShaderStageMask::Fragment);
}

} // namespace
} // namespace gte
