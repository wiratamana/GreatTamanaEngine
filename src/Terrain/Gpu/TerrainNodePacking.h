#pragma once

// Turns a CPU node mesh into the vertex data the GPU pool and the preview mesh hold.
// Positions become absolute world floats (node origin added in double first), so two nodes sharing a border
// vertex produce bit-identical floats.

#include "TerrainClusterRecords.h"
#include "TerrainRampImage.h"

#include "../TerrainTypes.h"

#include <span>

namespace gte::terrain::gpu {

// Preview vertex: position, normal and a flat colour (36 bytes).
struct PreviewVertex {
    float position[3];
    float normal[3];
    float color[3];
};
static_assert(sizeof(PreviewVertex) == 36);

// World box of `node`, rounded outward to float so no vertex is ever outside it.
BoxF NodeWorldBox(const NodeMesh& node) noexcept;

// Writes node.vertices.size() pool vertices into `out` (it must be at least that long). The uv is mapped into the
// ramp. Returns the world box.
BoxF PackNodeVertices(const NodeMesh& node, const RampUvMap& uvMap, std::span<TerrainVertex> out) noexcept;

// Same positions and normals, with the per-vertex colour (linear 0..1 per channel as stored in node.colors).
void PackPreviewVertices(const NodeMesh& node, std::span<PreviewVertex> out) noexcept;

} // namespace gte::terrain::gpu
