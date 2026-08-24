#pragma once

#include "../Math/Vec2.h"
#include "../Math/Vec3.h"

#include <cstdint>
#include <vector>

namespace gte {

// Which of PMX's five per-vertex bone-weighting schemes a given vertex uses
// (see third_party/saba/src/Saba/Model/MMD/PMXFile.h's own PMXVertexWeight
// enum, which this mirrors 1:1 - explicit numeric values are NOT pinned
// here the way AssetType's are, since VertexSkinWeights is never serialized
// directly; MeshFile.h/RigFile.h each define their own on-disk encoding):
//   BDEF1 - exactly one bone, weight always 1.0 (only boneIndices[0]/
//           boneWeights[0] meaningful).
//   BDEF2 - two bones, linearly blended (boneWeights[0] is bone 0's weight,
//           bone 1's weight is implicitly 1 - boneWeights[0]).
//   BDEF4 - up to four bones, independently weighted (weights are NOT
//           guaranteed to sum to 1 by the format itself, though authoring
//           tools normally normalize them).
//   SDEF - "Spherical DEFormation": like BDEF2 (two bones + one weight) but
//          with extra correction terms (sdefC/sdefR0/sdefR1 below) for a
//          more accurate blend around joints - see MMDPhysics-adjacent
//          literature; a renderer that doesn't implement true SDEF can
//          safely fall back to treating it as BDEF2 and ignoring the
//          correction terms.
//   QDEF - "Quaternion DEFormation" (PMX 2.1 extension): same four-bone/
//          four-weight shape as BDEF4, but intended to be blended via dual
//          quaternion skinning instead of linear blending to avoid the
//          classic "candy wrapper" collapse artifact - the plain weight/
//          index data stored here is identical to BDEF4's either way.
enum class VertexWeightType : std::uint8_t {
    BDEF1,
    BDEF2,
    BDEF4,
    SDEF,
    QDEF,
};

// Per-vertex bone skinning data - the "Bone weights / Skinning" half of this
// engine's MMD import support (see SkeletonData.h's own file comment for the
// other three: bones, morphs, physics). Index-aligned 1:1 with
// MeshData::positions/normals/uvs when present (see MeshData::skinWeights
// below) - i.e. skinWeights[i] describes how positions[i]/normals[i] are
// deformed by SkeletonData::bones.
//
// Unused influence slots (beyond however many `type` actually uses) are
// always boneIndex == -1, weight == 0.0 - never left uninitialized/garbage -
// so a shader/CPU-skinning loop can safely process all 4 slots uniformly
// regardless of `type` (a -1 index times a 0 weight contributes nothing).
struct VertexSkinWeights {
    VertexWeightType type = VertexWeightType::BDEF1;

    // Indices into SkeletonData::bones - see VertexWeightType's own comment
    // above for how many of these 4 slots are actually meaningful per type.
    std::int32_t boneIndices[4] = { -1, -1, -1, -1 };
    float boneWeights[4] = { 1.0f, 0.0f, 0.0f, 0.0f };

    // SDEF-only correction terms (see VertexWeightType::SDEF above) - always
    // Vec3::Zero() for every other weight type.
    Vec3 sdefC = Vec3::Zero();
    Vec3 sdefR0 = Vec3::Zero();
    Vec3 sdefR1 = Vec3::Zero();
};

// Plain, engine-native mesh geometry - positions/normals/UVs/triangle
// indices, index-aligned (positions[i]/normals[i]/uvs[i] together describe
// vertex i). This is the one shared shape every mesh IMPORTER (PmxLoader.h
// today; a future OBJ/glTF loader tomorrow) produces, and the one shape
// MeshFile.h's EncodeMeshDataToBytes()/DecodeMeshDataFromBytes() (de)
// serializes as a *.gta AssetType::Mesh payload (see AssetImporter.h) - the
// neutral home for this shape so neither of those needs to depend on the
// other, or on any one specific source format.
//
// Deliberately a plain data struct with no behavior of its own, same "ECS
// component"-style philosophy as the rest of this engine (see AGENTS.md,
// "Entity-Component-System").
struct MeshData {
    std::vector<Vec3> positions;
    std::vector<Vec3> normals;
    std::vector<Vec2> uvs;

    // Triangle list, 3 indices per face, indexing into positions/normals/uvs
    // above - directly usable as a GPU index buffer.
    std::vector<std::uint32_t> indices;

    // Optional per-vertex bone skinning data - EMPTY when the source has no
    // skinning info at all (e.g. a procedural PrimitiveMeshGenerator mesh,
    // or a future OBJ importer that doesn't support it), otherwise
    // index-aligned 1:1 with positions/normals/uvs above (skinWeights.size()
    // == positions.size()). A consumer should always check
    // `!skinWeights.empty()` before indexing into it rather than assuming
    // it is always populated.
    std::vector<VertexSkinWeights> skinWeights;
};

} // namespace gte
