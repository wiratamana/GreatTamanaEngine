#pragma once

#include "../Math/Vec3.h"

#include <cstdint>
#include <string>
#include <vector>

namespace gte {

// Plain, engine-native skeleton (bone hierarchy) - the "Bones" half of this
// engine's MMD import support, alongside MeshData::VertexSkinWeights (see
// MeshData.h) for the "Bone weights / Skinning" half and MorphData.h/
// PhysicsData.h for the remaining two. Produced by PmxLoader.h's
// LoadPmxModel() today (from saba::PMXFile's m_bones - see
// third_party/saba/src/Saba/Model/MMD/PMXFile.h), the same "wrap a
// third-party format reader's data into a small, engine-native, saba/glm-
// free struct" shape as MeshData itself - no saba::/glm:: type crosses this
// header's own public API.
//
// Deliberately a plain data struct with no behavior of its own (no forward-
// kinematics/pose evaluation here) - same "ECS component"-style philosophy
// as MeshData (see AGENTS.md, "Entity-Component-System"). A future
// skeletal-animation system would consume this read-only to build a runtime
// pose hierarchy; nothing here mutates or evaluates it.
struct Bone {
    std::string name;
    std::string englishName;

    // Bind-pose (rest) position, in the same model-local space as
    // MeshData::positions - NOT relative to the parent bone (matches PMX's
    // own convention: PMXBone::m_position is already absolute/model-space).
    Vec3 position = Vec3::Zero();

    // Index into the owning SkeletonData::bones array below, or -1 for a
    // root bone with no parent.
    std::int32_t parentBoneIndex = -1;

    // Deformation/sort order hint (PMX's own "transform level" - lower
    // values deform first) - only meaningful relative to other bones in the
    // same SkeletonData, never an absolute unit.
    std::int32_t deformDepth = 0;

    // --- Bone flags (PMXBoneFlags bits, decoded into named bools) ---------
    bool rotatable = false; // AllowRotate
    bool translatable = false; // AllowTranslate
    bool visible = false; // Visible
    bool controllable = false; // AllowControl (editable by a user-facing rig UI)
    bool isIk = false; // IK (ikTargetBoneIndex/ikLinks below are only meaningful when true)
    bool deformAfterPhysics = false; // DeformAfterPhysics (physics-driven "jiggle" bones)

    // --- Tail (display-only "where does this bone point at" hint) --------
    // When tailIsBone is true, the tail is `tailBoneIndex`'s own position;
    // otherwise it's this bone's position + tailOffset. Matches PMX's own
    // "TargetShowMode" bit (0 == offset, 1 == bone) - purely cosmetic/rig-
    // display data, never affects deformation.
    bool tailIsBone = false;
    Vec3 tailOffset = Vec3::Zero();
    std::int32_t tailBoneIndex = -1;

    // --- Append (a.k.a. "grant"/inherit) rotation/translation -------------
    // When appendRotate and/or appendTranslate is true, this bone's final
    // transform additionally inherits `appendWeight` of `appendBoneIndex`'s
    // own rotation/translation (appendLocal selects "that bone's local
    // deform" vs. "that bone's total user/IK/multi-append deform" as the
    // source - see PMXBoneFlags::AppendLocal's own comment in PMXFile.h).
    bool appendRotate = false;
    bool appendTranslate = false;
    bool appendLocal = false;
    std::int32_t appendBoneIndex = -1;
    float appendWeight = 0.0f;

    // --- Fixed axis (this bone can only rotate around one fixed axis) ----
    bool hasFixedAxis = false;
    Vec3 fixedAxis = Vec3::Zero();

    // --- Local axis (a custom local coordinate frame for this bone) ------
    bool hasLocalAxis = false;
    Vec3 localXAxis = Vec3::Right();
    Vec3 localZAxis = Vec3::Forward();

    // --- External parent (deform driven by a value outside this model) ---
    bool hasExternalParent = false;
    std::int32_t externalParentKey = 0;

    // --- Inverse Kinematics (only meaningful when isIk is true) -----------
    struct IkLink {
        // Index into SkeletonData::bones - one link in the IK chain between
        // this bone and ikTargetBoneIndex.
        std::int32_t boneIndex = -1;
        bool hasAngleLimit = false;
        // Only meaningful when hasAngleLimit is true - radians, matching
        // PMX's own convention.
        Vec3 angleLimitMin = Vec3::Zero();
        Vec3 angleLimitMax = Vec3::Zero();
    };
    std::int32_t ikTargetBoneIndex = -1;
    std::int32_t ikIterationCount = 0;
    float ikAngleLimitRadians = 0.0f;
    std::vector<IkLink> ikLinks;
};

// A full skeleton - PMX bones are always stored as a flat array where each
// bone's parentBoneIndex points at an earlier or later sibling (never
// assumed sorted by hierarchy depth); consumers should build any tree/parent-
// child adjacency they need themselves.
struct SkeletonData {
    std::vector<Bone> bones;
};

} // namespace gte
