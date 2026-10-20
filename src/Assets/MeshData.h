#pragma once

#include "../Math/Vec2.h"
#include "../Math/Vec3.h"
#include "../Math/Vec4.h"

#include <cstdint>
#include <vector>

namespace gte {

// PMX bone weighting scheme of one vertex (mirrors saba's PMXVertexWeight).
//   BDEF1 one bone, weight 1.
//   BDEF2 two bones, boneWeights[0] is bone 0's weight, bone 1 gets the rest.
//   BDEF4 up to four independently weighted bones (not guaranteed to sum to 1).
//   SDEF  like BDEF2 plus the sdefC/sdefR0/sdefR1 correction terms (BDEF2 is a valid fallback).
//   QDEF  four bones like BDEF4, meant for dual-quaternion blending.
enum class VertexWeightType : std::uint8_t {
    BDEF1,
    BDEF2,
    BDEF4,
    SDEF,
    QDEF,
};

// Per-vertex skinning data: skinWeights[i] deforms positions[i] by SkeletonData::bones.
// Unused influence slots are always boneIndex -1, weight 0.
struct VertexSkinWeights {
    VertexWeightType type = VertexWeightType::BDEF1;

    std::int32_t boneIndices[4] = { -1, -1, -1, -1 };
    float boneWeights[4] = { 1.0f, 0.0f, 0.0f, 0.0f };

    // SDEF only; zero for every other type.
    Vec3 sdefC = Vec3::Zero();
    Vec3 sdefR0 = Vec3::Zero();
    Vec3 sdefR1 = Vec3::Zero();
};

// Engine-native mesh geometry produced by every importer and stored as the *.gta mesh payload.
// All per-vertex arrays are index-aligned with positions.
struct MeshData {
    std::vector<Vec3> positions;
    std::vector<Vec3> normals;
    std::vector<Vec2> uvs;

    // Optional. xyz = unit tangent, w = +-1; bitangent = cross(N, T) * w. Empty when the source has none.
    std::vector<Vec4> tangents;

    // Triangle list, 3 indices per face.
    std::vector<std::uint32_t> indices;

    // Optional; empty when the mesh is not skinned, else skinWeights.size() == positions.size().
    std::vector<VertexSkinWeights> skinWeights;
};

} // namespace gte
