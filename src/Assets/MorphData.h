#pragma once

#include "../Math/Quat.h"
#include "../Math/Vec3.h"
#include "../Math/Vec4.h"

#include <cstdint>
#include <string>
#include <vector>

namespace gte {

// Plain, engine-native morph (a.k.a. "blend shape"/"face flag" in MMD
// parlance) data - the "Morphs" half of this engine's MMD import support
// (see SkeletonData.h's own file comment for the other three: bone weights/
// skinning, bones, physics). Produced by PmxLoader.h's LoadPmxModel() today
// (from saba::PMXFile's m_morphs - see
// third_party/saba/src/Saba/Model/MMD/PMXFile.h), same "engine-native,
// saba/glm-free copy of the third-party format's data" shape as
// SkeletonData/MeshData.
//
// A morph is a NAMED, WEIGHTED offset applied on top of the base mesh/
// skeleton/materials - PMX supports several different kinds of offset
// (vertex position, UV, bone rotation/translation, material color tweak,
// ...), all sharing this same "named + panel + weighted list of offsets"
// shape; which offset list is actually populated depends on `type` below
// (exactly one of the vectors is non-empty, matching PMXMorph's own "one
// struct, several parallel arrays, only one ever used" layout).
enum class MorphType : std::uint8_t {
    Group, // References other morphs by index, each at its own weight - a "meta-morph".
    Position, // Per-vertex position offset - the classic facial "blend shape".
    Bone, // Per-bone translation + rotation offset.
    Uv, // Per-vertex base-UV-channel offset.
    AddUv1,
    AddUv2,
    AddUv3,
    AddUv4,
    Material, // Per-material (or all-materials, materialIndex == -1) color/texture-factor tweak.
    Flip, // Like Group, but mutually-exclusive weight distribution (2.1 extension) - a "slider set".
    Impulse, // Applies an instantaneous velocity/torque to a rigid body (2.1 extension) - see PhysicsData.h.
};

struct Morph {
    std::string name;
    std::string englishName;

    // Which on-screen morph "panel"/category this belongs to in an MMD-style
    // rig UI - 0 == system-reserved/none, 1 == eyebrow, 2 == eye, 3 == mouth,
    // 4 == other. Purely a UI grouping hint, never affects evaluation.
    std::uint8_t controlPanel = 0;

    MorphType type = MorphType::Position;

    // --- MorphType::Position -----------------------------------------------
    struct PositionOffset {
        std::int32_t vertexIndex = -1; // Index into MeshData::positions.
        Vec3 offset = Vec3::Zero(); // Added to the base position at weight == 1.
    };
    std::vector<PositionOffset> positionOffsets;

    // --- MorphType::Uv/AddUv1..4 --------------------------------------------
    struct UvOffset {
        std::int32_t vertexIndex = -1;
        Vec4 offset = Vec4::Zero(); // xy (and zw for the "add UV" channels) offset.
    };
    std::vector<UvOffset> uvOffsets;

    // --- MorphType::Bone -----------------------------------------------------
    struct BoneOffset {
        std::int32_t boneIndex = -1; // Index into SkeletonData::bones.
        Vec3 translation = Vec3::Zero();
        Quat rotation = Quat::Identity();
    };
    std::vector<BoneOffset> boneOffsets;

    // --- MorphType::Material ---------------------------------------------
    struct MaterialOffset {
        enum class OpType : std::uint8_t {
            Multiply, // Multiplies the material's own color/factor values.
            Add, // Adds to the material's own color/factor values.
        };

        // -1 means "applies to every material" (PMX's own convention).
        std::int32_t materialIndex = -1;
        OpType op = OpType::Multiply;

        Vec4 diffuse = Vec4::Zero();
        Vec3 specular = Vec3::Zero();
        float specularPower = 0.0f;
        Vec3 ambient = Vec3::Zero();
        Vec4 edgeColor = Vec4::Zero();
        float edgeSize = 0.0f;
        Vec4 textureFactor = Vec4::Zero();
        Vec4 sphereTextureFactor = Vec4::Zero();
        Vec4 toonTextureFactor = Vec4::Zero();
    };
    std::vector<MaterialOffset> materialOffsets;

    // --- MorphType::Group ----------------------------------------------------
    struct GroupOffset {
        std::int32_t morphIndex = -1; // Index into the owning MorphData::morphs.
        float weight = 0.0f; // Applied morph weight when this group morph is at weight 1.
    };
    std::vector<GroupOffset> groupOffsets;

    // --- MorphType::Flip -----------------------------------------------------
    struct FlipOffset {
        std::int32_t morphIndex = -1;
        float weight = 0.0f;
    };
    std::vector<FlipOffset> flipOffsets;

    // --- MorphType::Impulse --------------------------------------------------
    struct ImpulseOffset {
        std::int32_t rigidBodyIndex = -1; // Index into PhysicsData::rigidBodies.
        bool isLocal = false; // True: velocity/torque are in the rigid body's own local space.
        Vec3 velocity = Vec3::Zero();
        Vec3 torque = Vec3::Zero();
    };
    std::vector<ImpulseOffset> impulseOffsets;
};

struct MorphData {
    std::vector<Morph> morphs;
};

} // namespace gte
