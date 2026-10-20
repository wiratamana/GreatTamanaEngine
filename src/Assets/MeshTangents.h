#pragma once

#include "MeshData.h"

#include <cstdint>

namespace gte {

// Unit tangent perpendicular to `normal`, w = +1. A zero or non-finite normal gives (1, 0, 0, 1).
Vec4 FallbackTangent(const Vec3& normal);

struct TangentGenerationResult {
    bool generated = false;              // False: every tangent is a FallbackTangent.
    std::uint32_t splitVertexCount = 0;  // Vertices appended because one vertex needs several tangent frames.
};

// Overwrites mesh.tangents with MikkTSpace tangents; bitangent = cross(N, T) * w points along decreasing v.
// Vertices whose corners need different frames are copied (positions, normals, uvs, skin weights, indices follow).
// mesh.tangents.size() == mesh.positions.size() afterwards.
TangentGenerationResult GenerateMeshTangents(MeshData& mesh);

// Makes every tangent perpendicular to its normal and unit length; keeps the sign of w.
// A zero-length or non-finite tangent becomes FallbackTangent.
void OrthonormalizeMeshTangents(MeshData& mesh);

} // namespace gte
