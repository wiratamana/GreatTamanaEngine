#pragma once

#include <volk.h>

#include <array>
#include <cstddef>

namespace gte {

// Vertex for imported indexed mesh assets: position + normal, tightly packed (24 bytes).
// Separate from Vertex (position + color); matches the editor preview vertex layout.
struct MeshVertex {
    float position[3];
    float normal[3];

    static VkVertexInputBindingDescription BindingDescription() noexcept
    {
        VkVertexInputBindingDescription binding{};
        binding.binding = 0;
        binding.stride = sizeof(MeshVertex);
        binding.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;
        return binding;
    }

    static std::array<VkVertexInputAttributeDescription, 2> AttributeDescriptions() noexcept
    {
        std::array<VkVertexInputAttributeDescription, 2> attributes{};

        attributes[0].location = 0;
        attributes[0].binding = 0;
        attributes[0].format = VK_FORMAT_R32G32B32_SFLOAT;
        attributes[0].offset = offsetof(MeshVertex, position);

        attributes[1].location = 1;
        attributes[1].binding = 0;
        attributes[1].format = VK_FORMAT_R32G32B32_SFLOAT;
        attributes[1].offset = offsetof(MeshVertex, normal);

        return attributes;
    }
};

// Vertex for an indexed, textured mesh: position + normal + UV, tightly packed (32 bytes).
// A separate struct so untextured MeshVertex pipelines keep their layout.
struct MeshVertexUv {
    float position[3];
    float normal[3];
    float uv[2];

    static VkVertexInputBindingDescription BindingDescription() noexcept
    {
        VkVertexInputBindingDescription binding{};
        binding.binding = 0;
        binding.stride = sizeof(MeshVertexUv);
        binding.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;
        return binding;
    }

    static std::array<VkVertexInputAttributeDescription, 3> AttributeDescriptions() noexcept
    {
        std::array<VkVertexInputAttributeDescription, 3> attributes{};

        attributes[0].location = 0;
        attributes[0].binding = 0;
        attributes[0].format = VK_FORMAT_R32G32B32_SFLOAT;
        attributes[0].offset = offsetof(MeshVertexUv, position);

        attributes[1].location = 1;
        attributes[1].binding = 0;
        attributes[1].format = VK_FORMAT_R32G32B32_SFLOAT;
        attributes[1].offset = offsetof(MeshVertexUv, normal);

        attributes[2].location = 2;
        attributes[2].binding = 0;
        attributes[2].format = VK_FORMAT_R32G32_SFLOAT;
        attributes[2].offset = offsetof(MeshVertexUv, uv);

        return attributes;
    }
};

// Vertex for a textured mesh with a tangent frame: position + normal + UV + tangent (48 bytes).
struct MeshVertexTangent {
    float position[3];
    float normal[3];
    float uv[2];
    float tangent[4]; // xyz tangent, w handedness (+-1)

    static VkVertexInputBindingDescription BindingDescription() noexcept
    {
        VkVertexInputBindingDescription binding{};
        binding.binding = 0;
        binding.stride = sizeof(MeshVertexTangent);
        binding.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;
        return binding;
    }

    static std::array<VkVertexInputAttributeDescription, 4> AttributeDescriptions() noexcept
    {
        std::array<VkVertexInputAttributeDescription, 4> attributes{};

        attributes[0].location = 0;
        attributes[0].binding = 0;
        attributes[0].format = VK_FORMAT_R32G32B32_SFLOAT;
        attributes[0].offset = offsetof(MeshVertexTangent, position);

        attributes[1].location = 1;
        attributes[1].binding = 0;
        attributes[1].format = VK_FORMAT_R32G32B32_SFLOAT;
        attributes[1].offset = offsetof(MeshVertexTangent, normal);

        attributes[2].location = 2;
        attributes[2].binding = 0;
        attributes[2].format = VK_FORMAT_R32G32_SFLOAT;
        attributes[2].offset = offsetof(MeshVertexTangent, uv);

        attributes[3].location = 3;
        attributes[3].binding = 0;
        attributes[3].format = VK_FORMAT_R32G32B32A32_SFLOAT;
        attributes[3].offset = offsetof(MeshVertexTangent, tangent);

        return attributes;
    }
};
static_assert(sizeof(MeshVertexTangent) == 48);
static_assert(offsetof(MeshVertexTangent, normal) == 12 && offsetof(MeshVertexTangent, uv) == 24
    && offsetof(MeshVertexTangent, tangent) == 32);

} // namespace gte
