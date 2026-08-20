#pragma once

#include <volk.h>

#include <array>
#include <cstddef>

namespace gte {

// Plain vertex format for the engine's first hardcoded draw path(s): a 3D
// position plus a per-vertex color, interpolated across each triangle by
// the rasterizer and consumed as-is by Triangle.frag (see src/Shaders/).
// Position grew from a 2D (XY, z implicitly 0) layout to a real 3D one once
// the engine's first genuine 3D geometry - the built-in primitive shapes
// (Cube/Sphere/Capsule/Cone/Plane, see Renderer/Primitives/
// PrimitiveMeshGenerator.h) - needed vertices that aren't all coplanar. This
// will grow further (normals, UVs, ...) - or be replaced by several
// per-material vertex formats - once there's an actual material/lighting
// system instead of a single flat unlit vertex-color pass-through. Kept
// deliberately minimal for now.
struct Vertex {
    float position[3];
    float color[3];

    static VkVertexInputBindingDescription BindingDescription() noexcept
    {
        VkVertexInputBindingDescription binding{};
        binding.binding = 0;
        binding.stride = sizeof(Vertex);
        binding.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;
        return binding;
    }

    static std::array<VkVertexInputAttributeDescription, 2> AttributeDescriptions() noexcept
    {
        std::array<VkVertexInputAttributeDescription, 2> attributes{};

        attributes[0].location = 0;
        attributes[0].binding = 0;
        attributes[0].format = VK_FORMAT_R32G32B32_SFLOAT;
        attributes[0].offset = offsetof(Vertex, position);

        attributes[1].location = 1;
        attributes[1].binding = 0;
        attributes[1].format = VK_FORMAT_R32G32B32_SFLOAT;
        attributes[1].offset = offsetof(Vertex, color);

        return attributes;
    }
};

} // namespace gte
