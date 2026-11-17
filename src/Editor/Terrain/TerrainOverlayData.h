#pragma once

// Pure parts of the chunk bounding-box overlay: the 12 edges of a node box and the colour bucket of a node.

#include "../CameraFrustumGizmo.h"
#include "../../Features/Terrain/TerrainControl.h"

#include <array>
#include <cstdint>
#include <vector>

namespace gte {

// Buckets: 0 drawn in view, 1 resident but outside the view, 2 hidden by another node. LOD colours start at 3.
inline constexpr int kOverlayStateBuckets = 3;
inline constexpr int kOverlayBucketCount = kOverlayStateBuckets + 12;

// Bucket of a node: by state, or by LOD level.
int OverlayBucketOf(const terrain::TerrainNodeInfo& node, bool colorByState) noexcept;

// RGBA colour of a bucket.
std::array<std::uint8_t, 4> OverlayBucketColor(int bucket) noexcept;

// Appends the 12 box edges (world space, floats) of the node.
void AppendNodeBoxSegments(const terrain::TerrainNodeInfo& node, std::vector<FrustumSegment>& out);

} // namespace gte
