#pragma once

#include "MaterialData.h"
#include "MeshData.h"
#include "MorphData.h"
#include "PhysicsData.h"
#include "SkeletonData.h"

#include <cstdint>
#include <optional>
#include <vector>

namespace gte {

// Everything an imported model carries besides plain mesh geometry.
// Stored as the metadata section of a Mesh *.gta; the payload stays a MeshFile.
struct RigFileData {
    std::vector<VertexSkinWeights> skinWeights;
    SkeletonData skeleton;
    MorphData morphs;
    PhysicsData physics;
    MaterialData materials;

    // User-saved per-joint physics overrides; empty when never saved.
    std::vector<JointPhysicsOverride> jointPhysicsOverrides;
};

// Flat little-endian binary blob of a RigFileData. Layout, in order:
//   8 bytes magic "GTERIG03"
//   u32 count + records: skin weights, bones (with IK links), morphs (7 offset lists each),
//     rigid bodies, joints, texture refs (path + 16-byte Guid), materials
//   OPTIONAL trailing sections, each u32 count + records; an older blob simply ends before them:
//     joint physics overrides, material extras (slot indices, alpha), material color flags,
//     material PBR factors (4 floats per material: metallic, roughness, normal scale, occlusion strength)
// Decode returns nullopt for a malformed, truncated or wrong-magic blob. It never throws.
inline constexpr char kRigFileMagic[8] = { 'G', 'T', 'E', 'R', 'I', 'G', '0', '3' };

std::vector<std::uint8_t> EncodeRigDataToBytes(const RigFileData& rig);

std::optional<RigFileData> DecodeRigDataFromBytes(const std::vector<std::uint8_t>& bytes);

} // namespace gte
