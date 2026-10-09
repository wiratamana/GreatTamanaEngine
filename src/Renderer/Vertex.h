#pragma once

#include <volk.h>

#include <array>
#include <cstddef>

namespace gte {

// Plain vertex format for the engine's primitive-shape draw path: a 3D
// position, a per-vertex normal, and a per-vertex color, interpolated
// across each triangle by the rasterizer. Triangle.frag shades from the
// normal (real per-pixel directional light + shadow) and multiplies in the
// color as a flat base tint - see Renderer/Primitives/
// PrimitiveMeshGenerator.h for how both fields are actually generated.
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
