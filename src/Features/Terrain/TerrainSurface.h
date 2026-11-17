#pragma once

// Read access to the drawn terrain surface: resident node meshes (erosion and carve included), noise as fallback.

#include "TerrainHeightField.h"
#include "TerrainSettings.h"
#include "TerrainTypes.h"

#include <array>
#include <cstdint>
#include <memory>
#include <unordered_map>
#include <vector>

namespace gte::terrain {

class TerrainStreamer;

// Resident nodes over a world rectangle, plus what changes their content. Cheap to copy; safe to use on any thread.
struct SurfaceSnapshot {
    bool open = false;
    std::shared_ptr<const TerrainHeightField> field; // Noise fallback where no node is resident.
    std::vector<std::shared_ptr<const NodeMesh>> nodes; // Finest level first.
    double nodeSizeLevel0 = 128.0;
    std::uint64_t streamRevision = 0; // Nodes built plus evicted so far.
    std::uint64_t carveHash = 0;
    int rebuilding = 0;               // Nodes still showing an old mesh while a carve rebuild runs.
};

// Gathers every resident node overlapping the rectangle. Main thread only (the streamer cache is not thread safe).
SurfaceSnapshot CollectSurface(const TerrainStreamer& streamer, const TerrainLayout& layout, double minX, double minZ,
    double maxX, double maxZ);

// Height of the node's drawn triangles at a world point. False when the point is outside the node.
bool NodeHeightAt(const NodeMesh& node, double x, double z, float& out) noexcept;

// Terrain colour of the node's drawn vertices at a world point (RGBA8, R in the lowest byte, as in NodeMesh::colors).
// Bilinear over the vertex colours. False when the point is outside the node or it has no colours.
bool NodeColorAt(const NodeMesh& node, double x, double z, std::uint32_t& rgba) noexcept;

struct SurfaceHit {
    float height = 0.0f;
    int level = -1; // Node level the height came from; -1 = noise fallback, -2 = no data at all.
    std::uint32_t color = 0xFF808080u; // RGBA8 terrain colour (noise fallback: the ramp colour of the noise surface).
};

// Finds the finest resident node at a point. Const and thread safe; `hint` is the caller's own cache of the last node.
class SurfaceSampler {
public:
    explicit SurfaceSampler(SurfaceSnapshot snapshot);

    SurfaceHit Sample(double x, double z, const NodeMesh*& hint) const noexcept;

    // Only the resident nodes: false (hit untouched) where none covers the point, no noise fallback.
    bool SampleNodes(double x, double z, const NodeMesh*& hint, SurfaceHit& hit) const noexcept;
    const SurfaceSnapshot& Snapshot() const noexcept { return m_snapshot; }

private:
    SurfaceSnapshot m_snapshot;
    std::unordered_map<NodeKey, const NodeMesh*, NodeKeyHash> m_byKey;
    std::array<bool, 16> m_hasLevel{};
};

} // namespace gte::terrain
