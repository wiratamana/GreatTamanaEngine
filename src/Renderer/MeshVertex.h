#pragma once

#include <volk.h>

#include <array>
#include <cstddef>

namespace gte {

// Vertex layout for imported, INDEXED mesh assets (a *.gta AssetType::Mesh
// file - see src/Assets/MeshFile.h/PmxLoader.h) - position + normal, tightly
// packed (24 bytes/vertex). Deliberately a SEPARATE struct from this
// engine's original position+color Vertex (see Vertex.h) rather than
// growing Vertex itself to carry both: PrimitiveMeshGenerator's built-in
// shapes bake a fixed-direction "faux-lit" shade directly into a per-vertex
// COLOR (see its own class comment) and have no real per-vertex normal
// concept at all, whereas an imported mesh's actual authored normals need to
// reach the fragment shader untouched instead. Mirrors
// src/Editor/AssetPreviewMesh.cpp's own (Editor-only) PreviewVertex layout
// exactly (same two attributes, same offsets) - this is that same layout,
// promoted out of the Editor-only Inspector preview into the shared
// Renderer/ layer so real GAMEPLAY rendering can use it too: see
// Pipeline.h's VertexLayout::PositionNormal and
// Game::CreateMeshEntityFromGtaFile() (src/Game/Game.h/.cpp), which is what
// actually builds a Mesh of these from a decoded MeshData.
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

} // namespace gte
