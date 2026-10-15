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

// Vertex layout for an imported, INDEXED, TEXTURED mesh submesh (see
// Pipeline.h's VertexLayout::PositionNormalUv and
// Game::EnsureMeshAsset()/CreateMeshEntityFromGtaFile(), src/Game/Game.cpp) -
// position + normal + UV, tightly packed (32 bytes/vertex). A separate
// struct from MeshVertex above (rather than growing it) for the same reason
// MeshVertex itself is separate from Vertex (Vertex.h): a Pipeline/Mesh pair
// must agree byte-for-byte on vertex layout (see VertexLayout's own
// comment), and every consumer of the plain, untextured MeshVertex/
// VertexLayout::PositionNormal pipeline (e.g. a material-less PMX submesh,
// or a future non-PMX mesh importer with no UVs at all) is unaffected by
// this one growing.
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

} // namespace gte
