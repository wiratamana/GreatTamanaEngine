#include "DynamicChainDetection.h"

#include "../Math/MathTypes.h" // kEpsilon
#include "../Math/Vec3.h"

namespace gte {

namespace {

bool IsPhysicsBone(const SkeletonData& skeleton, std::int32_t boneIndex)
{
    return boneIndex >= 0 && static_cast<std::size_t>(boneIndex) < skeleton.bones.size()
        && skeleton.bones[static_cast<std::size_t>(boneIndex)].deformAfterPhysics;
}

int CountPhysicsChildren(const std::vector<std::vector<std::int32_t>>& childrenByParent, std::int32_t boneIndex,
    const SkeletonData& skeleton)
{
    if (boneIndex < 0 || static_cast<std::size_t>(boneIndex) >= childrenByParent.size()) {
        return 0;
    }
    int count = 0;
    for (const std::int32_t child : childrenByParent[static_cast<std::size_t>(boneIndex)]) {
        if (IsPhysicsBone(skeleton, child)) {
            ++count;
        }
    }
    return count;
}

// Walks forward from `startJointIndex` (already confirmed to be a valid
// chain-start boundary by the caller) following the run only while there is
// EXACTLY ONE deformAfterPhysics child - stops at a leaf (zero
// deformAfterPhysics children) or a branch (more than one), leaving each
// branch child to be discovered as its OWN chain start by the caller's own
// outer loop.
std::vector<std::int32_t> WalkChainRun(
    const SkeletonData& skeleton, const std::vector<std::vector<std::int32_t>>& childrenByParent, std::int32_t startJointIndex)
{
    std::vector<std::int32_t> joints;
    std::int32_t current = startJointIndex;
    while (current >= 0) {
        joints.push_back(current);

        std::int32_t onlyPhysicsChild = -1;
        int physicsChildCount = 0;
        for (const std::int32_t child : childrenByParent[static_cast<std::size_t>(current)]) {
            if (IsPhysicsBone(skeleton, child)) {
                ++physicsChildCount;
                onlyPhysicsChild = child;
            }
        }

        if (physicsChildCount == 1) {
            current = onlyPhysicsChild;
        } else {
            break; // Leaf (0) or branch (>1) - this run ends here either way.
        }
    }
    return joints;
}

} // namespace

std::vector<DynamicChainDefinition> DetectDynamicChains(
    const SkeletonData& skeleton, const PhysicsData* physics, const DynamicChainDetectionDefaults& defaults)
{
    std::vector<DynamicChainDefinition> result;

    const std::size_t boneCount = skeleton.bones.size();
    if (boneCount == 0) {
        return result;
    }

    std::vector<std::vector<std::int32_t>> childrenByParent(boneCount);
    for (std::size_t i = 0; i < boneCount; ++i) {
        const std::int32_t parent = skeleton.bones[i].parentBoneIndex;
        if (parent >= 0 && static_cast<std::size_t>(parent) < boneCount) {
            childrenByParent[static_cast<std::size_t>(parent)].push_back(static_cast<std::int32_t>(i));
        }
    }

    for (std::size_t i = 0; i < boneCount; ++i) {
        const Bone& bone = skeleton.bones[i];
        if (!bone.deformAfterPhysics) {
            continue;
        }

        const std::int32_t parentIndex = bone.parentBoneIndex;
        if (parentIndex < 0) {
            // Degenerate: this physics bone has no ancestor to anchor to at
            // all - discard the whole run (see this header's own doc
            // comment). Never treated as a chain-start boundary.
            continue;
        }

        const bool parentIsPhysics = IsPhysicsBone(skeleton, parentIndex);
        bool isChainStartBoundary = false;
        if (!parentIsPhysics) {
            isChainStartBoundary = true; // The ordinary case: run begins right after a non-physics ancestor.
        } else if (CountPhysicsChildren(childrenByParent, parentIndex, skeleton) > 1) {
            isChainStartBoundary = true; // A branch point - each of the branch's children starts its OWN new chain.
        }

        if (!isChainStartBoundary) {
            continue; // A plain continuation of an already-walked run - handled by an earlier boundary's own walk.
        }

        const std::vector<std::int32_t> jointIndices
            = WalkChainRun(skeleton, childrenByParent, static_cast<std::int32_t>(i));
        if (jointIndices.size() < defaults.minimumChainLength) {
            continue;
        }

        DynamicChainDefinition chain;
        chain.rootBoneIndex = parentIndex;
        chain.jointBoneIndices = jointIndices;
        chain.gravityScale = defaults.defaultGravityScale;
        chain.windScale = defaults.defaultWindScale;
        chain.constraintIterations = defaults.defaultConstraintIterations;
        chain.jointSettings.assign(jointIndices.size(), defaults.defaultJointSettings);
        chain.restLengths.resize(jointIndices.size());

        Vec3 previousBindPosition = skeleton.bones[static_cast<std::size_t>(parentIndex)].position;
        float sumRestLengths = 0.0f;
        for (std::size_t j = 0; j < jointIndices.size(); ++j) {
            const Vec3 jointBindPosition = skeleton.bones[static_cast<std::size_t>(jointIndices[j])].position;
            chain.restLengths[j] = Length(jointBindPosition - previousBindPosition);
            sumRestLengths += chain.restLengths[j];
            previousBindPosition = jointBindPosition;
        }

        // PHASE5 (task_manager/verlet-integration-1/
        // PHASE5_COLLISION_STABILITY_AND_PERFORMANCE_HARDENING.md, Step 5,
        // item 2) - a generous root-teleport threshold derived from this
        // chain's own actual combined rest length (several times its total
        // extent), rather than DynamicChainDefinition's own hand-built-test
        // default. Left at that default (never zero/negative) for the
        // degenerate case of a chain with zero combined rest length.
        if (sumRestLengths > kEpsilon) {
            chain.maxPlausibleRootDelta = sumRestLengths * 5.0f;
        }

        // PHASE5, Step 5 item 2 - pre-fill a reasonable head-collider
        // starting point (the chain's own root bone, with a small heuristic
        // radius derived from its average joint spacing) but leave it
        // DISABLED (hasHeadCollider = false) until a human opts in via the
        // Editor Inspector's "Dynamic Chain Physics" section.
        chain.hasHeadCollider = false;
        chain.headColliderBoneIndex = chain.rootBoneIndex;
        chain.headColliderRadius = jointIndices.empty() ? 0.0f : (sumRestLengths / static_cast<float>(jointIndices.size())) * 0.5f;

        if (physics != nullptr) {
            for (std::size_t j = 0; j < jointIndices.size(); ++j) {
                for (const RigidBody& body : physics->rigidBodies) {
                    if (body.boneIndex == jointIndices[j] && body.motionType != RigidBodyMotionType::Static) {
                        chain.jointSettings[j].mass = body.mass;
                        chain.jointSettings[j].damping = body.linearDamping;
                        break;
                    }
                }
            }
        }

        result.push_back(std::move(chain));
    }

    return result;
}

} // namespace gte
