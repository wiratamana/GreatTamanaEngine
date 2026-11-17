#pragma once

// Node key and the CPU mesh of one terrain node. Every node has the same vertex and index count.

#include "../../Math/Vec3.h"

#include <cstddef>
#include <cstdint>
#include <vector>

namespace gte::terrain {

// One quadtree cell: covers NodeSize(level) metres starting at (x, z) * NodeSize(level).
struct NodeKey {
    std::int32_t x = 0;
    std::int32_t z = 0;
    std::uint8_t level = 0;

    friend constexpr bool operator==(const NodeKey&, const NodeKey&) = default;

    // Same area, next coarser level.
    constexpr NodeKey Parent() const noexcept
    {
        return { x >> 1, z >> 1, static_cast<std::uint8_t>(level + 1) };
    }
    // Child 0..3: bit 0 = +x half, bit 1 = +z half.
    constexpr NodeKey Child(int index) const noexcept
    {
        return { x * 2 + (index & 1), z * 2 + ((index >> 1) & 1), static_cast<std::uint8_t>(level - 1) };
    }
};

struct NodeKeyHash {
    std::size_t operator()(const NodeKey& k) const noexcept
    {
        std::uint64_t h = static_cast<std::uint32_t>(k.x) * 0x9E3779B97F4A7C15ull;
        h ^= (static_cast<std::uint64_t>(static_cast<std::uint32_t>(k.z)) + 0x7F4A7C15ull + (h << 6) + (h >> 2)) * 0xC2B2AE3D27D4EB4Full;
        h ^= static_cast<std::uint64_t>(k.level) * 0x165667B19E3779F9ull;
        return static_cast<std::size_t>(h ^ (h >> 29));
    }
};

// Same layout as the engine's position + normal + uv mesh vertex (32 bytes).
struct TerrainVertex {
    float position[3]; // Metres from the node origin.
    float normal[3];
    float uv[2];       // x = Height01, y = Slope01: coordinates into the colour ramp.
};
static_assert(sizeof(TerrainVertex) == 32);

struct NodeMesh {
    NodeKey key;
    double originX = 0.0; // World position of vertex (0, 0), metres.
    double originZ = 0.0;
    float spacing = 0.0f; // Metres between grid vertices.
    float size = 0.0f;    // Metres covered by the node.
    std::vector<TerrainVertex> vertices;   // Grid first, then the skirt ring.
    std::vector<std::uint32_t> colors;     // RGBA8 per vertex, same order.
    Vec3 boundsMin;                        // Exact box of all vertices (skirt included), node-local.
    Vec3 boundsMax;

    std::size_t ByteSize() const noexcept
    {
        return vertices.size() * sizeof(TerrainVertex) + colors.size() * sizeof(std::uint32_t) + sizeof(NodeMesh);
    }
};

// Vertex and index counts of every node for a grid of `quads` quads per side.
constexpr std::uint32_t GridVertexCount(int quads) noexcept
{
    return static_cast<std::uint32_t>((quads + 1) * (quads + 1));
}
constexpr std::uint32_t NodeVertexCount(int quads) noexcept
{
    return GridVertexCount(quads) + static_cast<std::uint32_t>(4 * (quads + 1));
}
constexpr std::uint32_t NodeIndexCount(int quads) noexcept
{
    return static_cast<std::uint32_t>(quads * quads * 6 + 4 * quads * 6);
}

// Index buffer shared by every node (grid triangles, then skirt triangles). Front faces look up (+Y).
std::vector<std::uint32_t> BuildSharedIndices(int quads);

} // namespace gte::terrain
