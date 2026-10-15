#pragma once

#include <volk.h>

#include <array>
#include <cstddef>

namespace gte {

// Vertex for the primitive-shape draw path: position, normal and a flat base color.
// Triangle.frag lights from the normal; see Renderer/Primitives/PrimitiveMeshGenerator.h.
struct Vertex {
    float position[3];
    float normal[3];
    float color[3];

    static VkVertexInputBindingDescription BindingDescription() noexcept
    {
        VkVertexInputBindingDescription binding{};
        binding.binding = 0;
        binding.stride = sizeof(Vertex);
        binding.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;
        return binding;
    }

    static std::array<VkVertexInputAttributeDescription, 3> AttributeDescriptions() noexcept
    {
        std::array<VkVertexInputAttributeDescription, 3> attributes{};

        attributes[0].location = 0;
        attributes[0].binding = 0;
        attributes[0].format = VK_FORMAT_R32G32B32_SFLOAT;
        attributes[0].offset = offsetof(Vertex, position);

        attributes[1].location = 1;
        attributes[1].binding = 0;
        attributes[1].format = VK_FORMAT_R32G32B32_SFLOAT;
        attributes[1].offset = offsetof(Vertex, normal);

        attributes[2].location = 2;
        attributes[2].binding = 0;
        attributes[2].format = VK_FORMAT_R32G32B32_SFLOAT;
        attributes[2].offset = offsetof(Vertex, color);

        return attributes;
    }
};

} // namespace gte
