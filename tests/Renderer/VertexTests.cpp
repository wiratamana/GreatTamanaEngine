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

TEST(VertexTest, AttributeDescriptions_HasExactlyThreeAttributes)
{
    EXPECT_EQ(Vertex::AttributeDescriptions().size(), 3u);
}

TEST(VertexTest, AttributeDescriptions_PositionIsLocation0AsVec3AtItsRealOffset)
{
    const auto attributes = Vertex::AttributeDescriptions();
    const VkVertexInputAttributeDescription& position = attributes[0];

    EXPECT_EQ(position.location, 0u);
    EXPECT_EQ(position.binding, 0u);
    EXPECT_EQ(position.format, VK_FORMAT_R32G32B32_SFLOAT);
    EXPECT_EQ(position.offset, offsetof(Vertex, position));
}

TEST(VertexTest, AttributeDescriptions_NormalIsLocation1AsVec3AtItsRealOffset)
{
    const auto attributes = Vertex::AttributeDescriptions();
    const VkVertexInputAttributeDescription& normal = attributes[1];

    EXPECT_EQ(normal.location, 1u);
    EXPECT_EQ(normal.binding, 0u);
    EXPECT_EQ(normal.format, VK_FORMAT_R32G32B32_SFLOAT);
    EXPECT_EQ(normal.offset, offsetof(Vertex, normal));
}

TEST(VertexTest, AttributeDescriptions_ColorIsLocation2AsVec3AtItsRealOffset)
{
    const auto attributes = Vertex::AttributeDescriptions();
    const VkVertexInputAttributeDescription& color = attributes[2];

    EXPECT_EQ(color.location, 2u);
    EXPECT_EQ(color.binding, 0u);
    EXPECT_EQ(color.format, VK_FORMAT_R32G32B32_SFLOAT);
    EXPECT_EQ(color.offset, offsetof(Vertex, color));
}

TEST(VertexTest, PositionNormalAndColorAttributes_DoNotOverlap)
{
    const auto attributes = Vertex::AttributeDescriptions();
    EXPECT_GE(attributes[1].offset, attributes[0].offset + 3 * sizeof(float));
    EXPECT_GE(attributes[2].offset, attributes[1].offset + 3 * sizeof(float));
}

} // namespace
} // namespace gte
