#pragma once

#include "MeshData.h"
#include "MorphData.h"
#include "PhysicsData.h"
#include "SkeletonData.h"

#include <cstdint>
#include <optional>
#include <vector>

namespace gte {

// The result of one DecodeRigDataFromBytes() call below - the "rig" half of
// an imported MMD model (everything PmxLoader.h's LoadPmxModel() extracts
// BESIDES plain positions/normals/UVs/indices, which stay in MeshData/
// MeshFile.h exactly as before). Grouped into one struct/one blob rather
// than four separate ones purely because they are always produced/consumed
// together for a single imported model - see AssetImporter.cpp, which
// stores this as the Mesh *.gta's METADATA section (see GtaFile.h's
// GtaFileData::metadata - deliberately left an opaque "ad hoc bytes" slot
// for exactly this kind of sidecar data) alongside the unchanged MeshFile.h
// payload.
struct RigFileData {
    std::vector<VertexSkinWeights> skinWeights;
    SkeletonData skeleton;
    MorphData morphs;
    PhysicsData physics;
};

// (De)serializes a RigFileData - per-vertex bone skinning weights, the bone
// hierarchy, morphs, and rigid-body/joint physics setup - to/from a flat,
// engine-private binary blob, the "Bones / Morphs / Physics / (skin weight
// half of) Skinning" equivalent of MeshFile.h's own EncodeMeshDataToBytes()/
// DecodeMeshDataFromBytes() for plain vertex geometry. Deliberately a
// separate format/file from MeshFile.h rather than an extension of it: this
// data is optional/model-level (a boneless static mesh has none of it) and
// far more variable-shaped (nested per-bone/per-morph arrays, strings) than
// MeshData's tightly-packed fixed-stride arrays, so keeping the two formats
// independent means neither one's future evolution (e.g. a hypothetical
// glTF skin importer that only ever produces skin weights, never
// morphs/physics) needs to touch the other's on-disk layout at all.
//
// On-disk layout (all integers little-endian, all floats IEEE 754 binary32
// - same conventions as MeshFile.h; every variable-length section is
// length-prefixed with a uint32_t count/byte-length immediately before it,
// so a reader never needs to know a section's size in advance):
//
//   8 bytes  : magic ("GTERIG01", no null terminator - exactly 8 bytes)
//   uint32_t : skinWeights count, followed by that many fixed-size records
//              (1 byte weight type + 4 int32 bone indices + 4 float weights
//              + 3 Vec3 SDEF correction terms each)
//   uint32_t : bones count, followed by that many variable-size bone
//              records (two length-prefixed UTF-8 strings, fixed fields,
//              then a length-prefixed IK link array)
//   uint32_t : morphs count, followed by that many variable-size morph
//              records (two strings, then 7 length-prefixed offset arrays -
//              only the ones matching that morph's own type are ever
//              non-empty, but every array is always present/zero-length
//              otherwise, so decoding never needs to branch on type)
//   uint32_t : rigid bodies count, followed by that many fixed-shape-per-
//              record (two strings + fixed fields) rigid body records
//   uint32_t : joints count, followed by that many fixed-shape-per-record
//              (two strings + fixed fields) joint records
//
// Encodes/decodes never fail on a well-formed (even if entirely empty)
// RigFileData; Decode only fails (returns std::nullopt) on a genuinely
// malformed/truncated/wrong-magic blob - never throws, same failure
// contract as MeshFile.h's own DecodeMeshDataFromBytes().
inline constexpr char kRigFileMagic[8] = { 'G', 'T', 'E', 'R', 'I', 'G', '0', '1' };

std::vector<std::uint8_t> EncodeRigDataToBytes(const RigFileData& rig);

std::optional<RigFileData> DecodeRigDataFromBytes(const std::vector<std::uint8_t>& bytes);

} // namespace gte
