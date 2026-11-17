#pragma once

// Builds the CPU mesh of one node: heights, optional erosion, normals, colours, skirt, exact bounds.

#include "TerrainHeightField.h"
#include "TerrainTypes.h"

namespace gte::terrain {

struct NodeBuildTiming {
    double heightMs = 0.0;
    double erosionMs = 0.0;
    double carveMs = 0.0; // Zero without a carve.
    double meshMs = 0.0; // Normals, colours, skirt, bounds.
};

// World-space origin and sizes of a node.
struct NodeGeometry {
    double originX = 0.0;
    double originZ = 0.0;
    double size = 0.0;
    double spacing = 0.0;
};
NodeGeometry NodeGeometryOf(const TerrainLayout& layout, const NodeKey& key) noexcept;

// Safe to call from many threads at once. `timing` is optional.
NodeMesh BuildNode(const TerrainHeightField& field, const NodeKey& key, NodeBuildTiming* timing = nullptr);

// A square grid of `quads` quads at any origin and spacing (the live preview chunk). `erode` still needs the
// erosion settings to be on. The origin should be a multiple of the spacing so the noise lattice applies.
struct GridBuildSpec {
    NodeKey key;
    double originX = 0.0;
    double originZ = 0.0;
    double spacing = 2.0;
    int quads = 64;
    bool erode = false;
    std::uint64_t erosionSeed = 0;
};
NodeMesh BuildGridMesh(const TerrainHeightField& field, const GridBuildSpec& spec, NodeBuildTiming* timing = nullptr);

// Fills the skirt ring and exact bounds of a mesh whose grid vertices, colours and spacing are already set.
void FinishNodeMesh(NodeMesh& mesh, int quads);

// Recomputes the vertex colours of `mesh` from its uv after material settings changed.
void RecolorNode(NodeMesh& mesh, const TerrainSettings& settings);

// True when this level gets erosion under `settings`.
bool NodeUsesErosion(const TerrainSettings& settings, int level) noexcept;

} // namespace gte::terrain
