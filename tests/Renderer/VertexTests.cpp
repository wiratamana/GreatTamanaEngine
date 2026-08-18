// Unit tests for Vertex (src/Renderer/Vertex.h) - checks the Vulkan
// binding/attribute description metadata describing this vertex layout is
// internally consistent with Vertex's actual field layout. Pure metadata -
// no VkDevice, pipeline, or GPU of any kind involved; VkVertexInput*
// structs are plain value types regardless of whether a Vulkan instance
// exists.

#include "Renderer/Vertex.h"

#include <gtest/gtest.h>

#include <cstddef>

namespace gte {
namespace {

TEST(VertexTest, BindingDescription_UsesBinding0AndPerVertexStride)
{
    const VkVertexInputBindingDescription binding = Vertex::BindingDescription();

    EXPECT_EQ(binding.binding, 0u);
    EXPECT_EQ(binding.stride, sizeof(Vertex));
    EXPECT_EQ(binding.inputRate, VK_VERTEX_INPUT_RATE_VERTEX);
}

TEST(VertexTest, AttributeDescriptions_HasExactlyTwoAttributes)
{
    EXPECT_EQ(Vertex::AttributeDescriptions().size(), 2u);
}

TEST(VertexTest, AttributeDescriptions_PositionIsLocation0AsVec2AtItsRealOffset)
{
    const auto attributes = Vertex::AttributeDescriptions();
    const VkVertexInputAttributeDescription& position = attributes[0];

    EXPECT_EQ(position.location, 0u);
    EXPECT_EQ(position.binding, 0u);
    EXPECT_EQ(position.format, VK_FORMAT_R32G32_SFLOAT);
    EXPECT_EQ(position.offset, offsetof(Vertex, position));
}

TEST(VertexTest, AttributeDescriptions_ColorIsLocation1AsVec3AtItsRealOffset)
{
    const auto attributes = Vertex::AttributeDescriptions();
    const VkVertexInputAttributeDescription& color = attributes[1];

    EXPECT_EQ(color.location, 1u);
    EXPECT_EQ(color.binding, 0u);
    EXPECT_EQ(color.format, VK_FORMAT_R32G32B32_SFLOAT);
    EXPECT_EQ(color.offset, offsetof(Vertex, color));
}

TEST(VertexTest, PositionAndColorAttributes_DoNotOverlap)
{
    const auto attributes = Vertex::AttributeDescriptions();
    // R32G32_SFLOAT (position) is 2 * 4 = 8 bytes - color's offset must be
    // at or beyond that, or the rasterizer would read color data from
    // inside the position field.
    EXPECT_GE(attributes[1].offset, attributes[0].offset + 2 * sizeof(float));
}

} // namespace
} // namespace gte
