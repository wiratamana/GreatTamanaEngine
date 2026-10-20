#pragma once

#include "MeshData.h"

#include <cstdint>
#include <optional>
#include <vector>

namespace gte {

// Mesh payload of a *.gta mesh asset. Little-endian, IEEE 754 floats, each array one contiguous block:
//   8 bytes  magic "GTEMESH1"
//   4 bytes  vertexCount, 4 bytes indexCount
//   vertexCount * 12  positions (xyz)
//   vertexCount * 12  normals (xyz)
//   vertexCount * 8   uvs (xy)
//   indexCount  * 4   indices (uint32)
//   optional tail: 4 bytes tangentCount (== vertexCount), tangentCount * 16 tangents (xyzw)
// Nothing may follow the last block.
inline constexpr char kMeshFileMagic[8] = { 'G', 'T', 'E', 'M', 'E', 'S', 'H', '1' };

// Encodes `mesh`. The tangent block is written only when tangents.size() == positions.size() and not 0.
std::vector<std::uint8_t> EncodeMeshDataToBytes(const MeshData& mesh);

// Decodes a payload; nullopt on a bad magic, truncation, a bad tangent block or trailing bytes. Never throws.
std::optional<MeshData> DecodeMeshDataFromBytes(const std::vector<std::uint8_t>& bytes);

} // namespace gte
