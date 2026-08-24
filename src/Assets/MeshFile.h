#pragma once

#include "MeshData.h"

#include <cstdint>
#include <optional>
#include <vector>

namespace gte {

// (De)serializes a MeshData (positions/normals/UVs/triangle indices) to/from
// a flat, engine-private binary blob - this is exactly what ends up as the
// PAYLOAD section of a *.gta AssetType::Mesh file (see GtaFile.h and
// AssetImporter.h's PMX -> *.gta import pipeline), the mesh equivalent of
// Ktx2Encoder.h/Ktx2Decoder.h's role for AssetType::Texture. Deliberately a
// simple, engine-private format (not glTF/OBJ/any interchange standard) -
// there is no need for one here: nothing outside this engine ever reads a
// *.gta file, exactly like Ktx2Encoder's own "format unification, not
// interchange" rationale.
//
// On-disk layout (all integers little-endian, matching this engine's only
// target platform - see AGENTS.md's Windows-only build; all floats IEEE 754
// binary32, matching Vec2/Vec3's own `float` fields):
//
//   8 bytes  : magic ("GTEMESH1", no null terminator - exactly 8 bytes)
//   4 bytes  : vertexCount (uint32_t)
//   4 bytes  : indexCount (uint32_t)
//   vertexCount * 12 bytes : positions, tightly packed (x,y,z floats each)
//   vertexCount * 12 bytes : normals, tightly packed (x,y,z floats each)
//   vertexCount *  8 bytes : uvs, tightly packed (x,y floats each)
//   indexCount  *  4 bytes : indices (uint32_t each)
//
// Every array is a separate contiguous block (positions block, then normals
// block, then uvs block, then indices block) rather than one interleaved
// per-vertex struct - this is what lets EncodeMeshDataToBytes() just
// std::memcpy() each of MeshData's own std::vector<T>s straight out (and
// DecodeMeshDataFromBytes() straight back in), with no per-vertex
// re-packing loop needed on either side.
inline constexpr char kMeshFileMagic[8] = { 'G', 'T', 'E', 'M', 'E', 'S', 'H', '1' };

// Encodes `mesh` into the binary layout described above. Never fails (a
// MeshData with empty vectors simply encodes as a header-only blob with
// zero counts) - always returns a well-formed blob DecodeMeshDataFromBytes()
// can read back.
std::vector<std::uint8_t> EncodeMeshDataToBytes(const MeshData& mesh);

// Decodes `bytes` (as produced by EncodeMeshDataToBytes() above, or read
// straight from a *.gta AssetType::Mesh payload - see GtaFile.h) back into a
// MeshData. Returns std::nullopt if `bytes` is too short, has a bad magic,
// or its declared vertex/index counts don't actually fit the remaining
// bytes (a truncated/corrupt file) - never throws.
std::optional<MeshData> DecodeMeshDataFromBytes(const std::vector<std::uint8_t>& bytes);

} // namespace gte
