#pragma once

#include "Animation/BoneWorldMatrixQuery.h"
#include "ECS/Components/DynamicChainRig.h"
#include "ECS/Components/ResolvedAnimationPose.h"
#include "ECS/Registry.h"
#include "Game/Animation/SkeletalRigCache.h"
#include "Game/Physics/PhysicsSystem.h"

#include <cstdint>
#include <string>
#include <utility>

namespace gte::physicstest {

constexpr std::int32_t kAnchorBone = 1;

// Straight chain along +X: root(0) -> anchor(1, Static body) -> joint bones 2..jointCount+1 (Dynamic bodies).
// Every PMX joint between neighbours has rotation limits of +-axisLimitRadians on X and Z.
inline SkinnedMeshData BuildChainModel(int jointCount, float axisLimitRadians)
{
    SkinnedMeshData data;
    Bone root;
    root.name = "root";
    root.parentBoneIndex = -1;
    data.skeleton.bones.push_back(root);

    PhysicsData physics;
    for (int i = 0; i <= jointCount; ++i) {
        Bone bone;
        bone.name = i == 0 ? "anchor" : "joint" + std::to_string(i);
        bone.position = Vec3(1.0f + static_cast<float>(i), 0.0f, 0.0f);
        bone.parentBoneIndex = i;
        data.skeleton.bones.push_back(bone);

        RigidBody body;
        body.boneIndex = i + 1;
        body.motionType = i == 0 ? RigidBodyMotionType::Static : RigidBodyMotionType::Dynamic;
        physics.rigidBodies.push_back(body);

        if (i > 0) {
            Joint joint;
            joint.rigidBodyAIndex = i - 1;
            joint.rigidBodyBIndex = i;
            joint.rotateLowerLimit = Vec3(-axisLimitRadians, 0.0f, -axisLimitRadians);
            joint.rotateUpperLimit = Vec3(axisLimitRadians, 0.0f, axisLimitRadians);
            physics.joints.push_back(joint);
        }
    }
    data.physics = std::move(physics);
    return data;
}

// One registered model, one entity with a rig and a bind-pose ResolvedAnimationPose (identity entity transform).
struct ChainScene {
    PhysicsSystem physics;
    Registry registry;
    SkinnedMeshData data;
    std::string path;
    Entity entity{};

    explicit ChainScene(SkinnedMeshData model, std::string modelPath = "TestChain.gta")
        : data(std::move(model))
        , path(std::move(modelPath))
    {
        physics.RegisterDynamicChains(path, data);
        entity = registry.CreateEntity();
        physics.AttachDynamicChainRigIfNeeded(registry, entity, path);
        registry.AddComponent<ResolvedAnimationPose>(entity).pose.resize(data.skeleton.bones.size());
    }

    ResolvedAnimationPose& Pose() { return *registry.TryGetComponent<ResolvedAnimationPose>(entity); }
    DynamicChainRig& Rig() { return *registry.TryGetComponent<DynamicChainRig>(entity); }

    // Fresh animated pose: bind pose with the anchor bone shifted along X (what pose evaluation hands to physics).
    void SetAnimatedAnchorX(float x)
    {
        std::vector<BoneLocalOffset>& pose = Pose().pose;
        pose.assign(data.skeleton.bones.size(), BoneLocalOffset{});
        pose[kAnchorBone].translation = Vec3(x, 0.0f, 0.0f);
    }

    Vec3 BoneWorld(std::int32_t bone) { return ComputeBoneWorldMatrix(data.skeleton, Pose().pose, bone).TransformPoint(Vec3::Zero()); }

    // Animated (pre-physics) world position of a bone for an anchor shifted by `anchorX`.
    Vec3 AnimatedWorld(std::int32_t bone, float anchorX) const
    {
        return data.skeleton.bones[static_cast<std::size_t>(bone)].position + Vec3(anchorX, 0.0f, 0.0f);
    }
};

} // namespace gte::physicstest
