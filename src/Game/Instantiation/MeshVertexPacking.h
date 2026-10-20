#pragma once

#include "../../Math/Vec2.h"
#include "../../Math/Vec3.h"
#include "../../Math/Vec4.h"
#include "../../Renderer/MeshVertex.h"

#include <cstdint>
#include <vector>

namespace gte {

// Pure CPU helpers that pack mesh arrays into GPU vertex structs (Renderer/MeshVertex.h).
// A missing or size-mismatched normal array packs as Vec3::Up(), uvs as zero.

// PositionNormal layout, one entry per position.
std::vector<MeshVertex> PackMeshVertices(const std::vector<Vec3>& positions, const std::vector<Vec3>& normals);

// PositionNormalUv layout, one entry per position.
std::vector<MeshVertexUv> PackMeshVertexUvs(
    const std::vector<Vec3>& positions, const std::vector<Vec3>& normals, const std::vector<Vec2>& uvs);

// PositionNormalUvTangent layout, one entry per position. Missing tangents pack as (1, 0, 0, 1).
std::vector<MeshVertexTangent> PackMeshVertexTangents(const std::vector<Vec3>& positions,
    const std::vector<Vec3>& normals, const std::vector<Vec2>& uvs, const std::vector<Vec4>& tangents);

// Range variants for parallel packing: pack [beginIndex, endIndex) into the caller-sized `out`.
// `out` is never resized; the range is clamped to the input and output sizes.
void PackMeshVertexRange(std::uint32_t beginIndex, std::uint32_t endIndex, const std::vector<Vec3>& positions,
    const std::vector<Vec3>& normals, std::vector<MeshVertex>& out);

void PackMeshVertexUvRange(std::uint32_t beginIndex, std::uint32_t endIndex, const std::vector<Vec3>& positions,
    const std::vector<Vec3>& normals, const std::vector<Vec2>& uvs, std::vector<MeshVertexUv>& out);

void PackMeshVertexTangentRange(std::uint32_t beginIndex, std::uint32_t endIndex, const std::vector<Vec3>& positions,
    const std::vector<Vec3>& normals, const std::vector<Vec2>& uvs, const std::vector<Vec4>& tangents,
    std::vector<MeshVertexTangent>& out);

} // namespace gte
