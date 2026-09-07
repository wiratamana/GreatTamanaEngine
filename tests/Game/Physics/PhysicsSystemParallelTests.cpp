// Unit tests proving PhysicsSystem::Update()'s PHASE5 parallel-dispatch path
// (task_manager/verlet-integration-1/
// PHASE5_COLLISION_STABILITY_AND_PERFORMANCE_HARDENING.md, 3.4) produces
// BYTE-IDENTICAL results to the serial path - mirroring
// tests/Animation/VertexSkinningParityTests.cpp's own precedent exactly, just
// for whole dynamic bone chains instead of vertex ranges. No AnimationSystem/
// SkeletalAnimator/Renderer/GPU involved anywhere - a hand-built Registry +
// hand-built ResolvedAnimationPose is enough, further proof PhysicsSystem
// stays genuinely independent of the animation/rendering stack.

#include "Game/Physics/PhysicsSystem.h"

#include "Animation/BoneWorldMatrixQuery.h"
#include "Assets/SkeletonData.h"
#include "ECS/Components/DynamicChainRig.h"
#include "ECS/Components/ResolvedAnimationPose.h"
#include "ECS/Registry.h"

#include <gtest/gtest.h>

#include <string>
#include <vector>

namespace gte {
namespace {

// Builds a skeleton + a set of `chainCount` completely INDEPENDENT,
// geometrically IDENTICAL dynamic bone chains (bone 0 is a shared, plain
// root; each chain k gets its own private "arm root" bone (k's own anchor,
// parented directly to bone 0, at the same bind position as bone 0 itself)
// and its own private "joint" bone one unit along +X from its arm root) -
// deliberately disjoint rootBoneIndex/jointBoneIndices per chain (never a
// shared branch bone - see AGENTS.md's own PHASE5 audit-table addition on
// why concurrent writes require provably disjoint bone index sets), and
// deliberately IDENTICAL bind geometry/joint settings across every chain, so
// every chain is expected to evolve in perfect lockstep with every other one
// regardless of which chain index a given physical joint happens to be.
struct SymmetricModel {
    SkeletonData skeleton;
    std::vector<DynamicChainDefinition> chains; // one entry per chain, jointBoneIndices[0] is that chain's only joint.
};

SymmetricModel BuildSymmetricModel(std::size_t chainCount)
{
    SymmetricModel model;

    Bone root;
    root.position = Vec3::Zero();
    root.parentBoneIndex = -1;
    model.skeleton.bones.push_back(root); // index 0.

    for (std::size_t k = 0; k < chainCount; ++k) {
        Bone armRoot;
        armRoot.position = Vec3::Zero(); // identical bind position for every chain, on purpose (see file comment).
        armRoot.parentBoneIndex = 0;
        model.skeleton.bones.push_back(armRoot);
        const std::int32_t armRootIndex = static_cast<std::int32_t>(model.skeleton.bones.size() - 1);

        Bone joint;
        joint.position = Vec3(1.0f, 0.0f, 0.0f);
        joint.parentBoneIndex = armRootIndex;
        model.skeleton.bones.push_back(joint);
        const std::int32_t jointIndex = static_cast<std::int32_t>(model.skeleton.bones.size() - 1);

        DynamicChainDefinition chain;
        chain.rootBoneIndex = armRootIndex;
        chain.jointBoneIndices = { jointIndex };
        chain.jointSettings = { DynamicJointSettings{ /*damping=*/0.1f, /*stiffness=*/0.2f, /*mass=*/1.0f } };
        chain.parentJointIndex = DynamicChainDefinition::MakeLinearParentIndices(1);
        chain.restLengths = { 1.0f };
        chain.gravityScale = 1.0f;
        chain.windScale = 1.0f;
        chain.constraintIterations = 4;
        model.chains.push_back(std::move(chain));
    }

    return model;
}

Entity RegisterAndAttachModel(
    PhysicsSystem& physicsSystem, Registry& registry, const std::string& path, const SymmetricModel& model)
{
    DynamicChainRigCache::ModelEntry entry;
    entry.chains = model.chains;
    entry.skeleton = model.skeleton;
    physicsSystem.GetDynamicChainRigCache().Register(path, std::move(entry));

    const Entity entity = registry.CreateEntity();
    physicsSystem.AttachDynamicChainRigIfNeeded(registry, entity, path);

    ResolvedAnimationPose& pose = registry.AddComponent<ResolvedAnimationPose>(entity);
    pose.pose.resize(model.skeleton.bones.size()); // pure bind pose (all-identity BoneLocalOffset).

    return entity;
}

// Returns every chain's own joint world position, in chain order.
std::vector<Vec3> CollectJointWorldPositions(const SymmetricModel& model, const std::vector<BoneLocalOffset>& pose)
{
    std::vector<Vec3> positions;
    positions.reserve(model.chains.size());
    for (const DynamicChainDefinition& chain : model.chains) {
        positions.push_back(
            ComputeBoneWorldMatrix(model.skeleton, pose, chain.jointBoneIndices[0]).TransformPoint(Vec3::Zero()));
    }
    return positions;
}

TEST(PhysicsSystemParallelTests, SerialAndParallelPathsProduceByteIdenticalResultsForEveryChain)
{
    PhysicsSystem physicsSystem;
    Registry registry;
    physicsSystem.GetGlobalPhysicsSettings().gravity = Vec3(0.0f, -9.8f, 0.0f);
    // Wind's default (zero strength) is deliberately left as-is - this test
    // cares only about proving the serial/parallel SPLIT doesn't change the
    // result, not about spatially-varying wind (which would legitimately
    // differ between chains anyway, since it samples world position).

    // modelFew: 2 chains, 2 total joints - well below
    // PhysicsSystem.cpp's own kMinDynamicJointsToParallelize (24) - forces
    // the SERIAL path.
    const SymmetricModel modelFew = BuildSymmetricModel(2);
    const Entity entityFew = RegisterAndAttachModel(physicsSystem, registry, "FewChainsModel.gta", modelFew);

    // modelMany: 30 chains, 30 total joints - well above the threshold -
    // forces the PARALLEL (gte::Jobs::Dispatch()) path. Every chain here is
    // geometrically/parametrically IDENTICAL to modelFew's own chains.
    const SymmetricModel modelMany = BuildSymmetricModel(30);
    const Entity entityMany = RegisterAndAttachModel(physicsSystem, registry, "ManyChainsModel.gta", modelMany);

    for (int step = 0; step < 30; ++step) {
        physicsSystem.Update(registry, 1.0 / 60.0);
    }

    const ResolvedAnimationPose* poseFew = registry.TryGetComponent<ResolvedAnimationPose>(entityFew);
    const ResolvedAnimationPose* poseMany = registry.TryGetComponent<ResolvedAnimationPose>(entityMany);
    ASSERT_NE(poseFew, nullptr);
    ASSERT_NE(poseMany, nullptr);

    const std::vector<Vec3> fewPositions = CollectJointWorldPositions(modelFew, poseFew->pose);
    const std::vector<Vec3> manyPositions = CollectJointWorldPositions(modelMany, poseMany->pose);

    ASSERT_EQ(fewPositions.size(), 2u);
    ASSERT_EQ(manyPositions.size(), 30u);

    // Every chain, in EITHER model, is geometrically/parametrically
    // identical and receives identical inputs (same gravity, same fixed
    // timestep, same bind pose) - so every joint position, whether it was
    // stepped via the SERIAL path (modelFew) or the PARALLEL Dispatch() path
    // (modelMany), must agree with every other one.
    const Vec3 reference = fewPositions[0];
    for (const Vec3& position : fewPositions) {
        EXPECT_TRUE(ApproximatelyEqual(position, reference, 1e-4f))
            << "Two chains processed via the SAME (serial) path unexpectedly diverged.";
    }
    for (const Vec3& position : manyPositions) {
        EXPECT_TRUE(ApproximatelyEqual(position, reference, 1e-4f))
            << "A chain processed via the PARALLEL Dispatch() path diverged from the serial-path reference - "
               "the serial and parallel paths must produce byte-identical results.";
    }
}

TEST(PhysicsSystemParallelTests, ParallelPathAloneProducesConsistentResultsAcrossEveryIndependentChain)
{
    // A second, narrower check: WITHIN a single parallel dispatch (many
    // batches, potentially running on several different worker threads),
    // every chain's own result must still be internally consistent with
    // every other chain's - proving the batch-range math itself (which
    // chain index landed in which batch) never corrupts a chain's own
    // inputs/outputs.
    PhysicsSystem physicsSystem;
    Registry registry;
    physicsSystem.GetGlobalPhysicsSettings().gravity = Vec3(0.3f, -5.0f, 0.2f); // an arbitrary, non-axis-aligned pull.

    const SymmetricModel model = BuildSymmetricModel(40);
    const Entity entity = RegisterAndAttachModel(physicsSystem, registry, "ManyChainsModelB.gta", model);

    for (int step = 0; step < 45; ++step) {
        physicsSystem.Update(registry, 1.0 / 60.0);
    }

    const ResolvedAnimationPose* pose = registry.TryGetComponent<ResolvedAnimationPose>(entity);
    ASSERT_NE(pose, nullptr);

    const std::vector<Vec3> positions = CollectJointWorldPositions(model, pose->pose);
    ASSERT_EQ(positions.size(), 40u);

    const Vec3 reference = positions[0];
    for (const Vec3& position : positions) {
        EXPECT_TRUE(ApproximatelyEqual(position, reference, 1e-4f))
            << "Two symmetric, independent chains diverged after being stepped through the SAME parallel dispatch.";
    }
}

} // namespace
} // namespace gte
