#pragma once

#include "MeshData.h"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace gte {

// Mesh payload of a *.gta mesh asset: little-endian, one contiguous block per array.
// Byte layout: docs/conventions/gpu-driven-clusters.md. Nothing may follow the last block.
inline constexpr char kMeshFileMagic[8] = { 'G', 'T', 'E', 'M', 'E', 'S', 'H', '1' };
inline constexpr char kMeshClusterTailMagic[4] = { 'G', 'C', 'L', '1' };

// True when the counts fit the 32-bit fields of the payload.
bool MeshCountsFitUint32(std::size_t vertexCount, std::size_t indexCount, std::size_t clusterCount);

// Encodes `mesh`. Returns an EMPTY vector (nothing may be written) when the counts do not fit 32 bits,
// normals or uvs differ in length from positions, tangents are non-empty and differ from positions,
// an index is out of range, or the cluster table is non-empty and invalid.
std::vector<std::uint8_t> EncodeMeshDataToBytes(const MeshData& mesh);

// Decodes a payload; nullopt when the mesh itself is damaged (bad magic, truncation, bad tangent block,
// stray trailing bytes). A damaged cluster tail keeps the mesh with `clusters` empty and the reason in
// `clusterTailRejection` (when non-null). Never throws.
std::optional<MeshData> DecodeMeshDataFromBytes(const std::vector<std::uint8_t>& bytes, std::string* clusterTailRejection = nullptr);

} // namespace gte
