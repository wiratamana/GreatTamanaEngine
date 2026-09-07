// Unit tests for AnimationSystem::EvaluatePoses()/SkinAndUpload() (Phase 3,
// task_manager/verlet-integration-1/PHASE3_PIPELINE_INTEGRATION_AND_FIXED_TIMESTEP.md)
// - the split of the old, single AnimationSystem::Update() into two
// independent stages communicating only through the new
// ResolvedAnimationPose ECS component. Touches a real temp directory (same
// convention as Game/AnimationClipCacheTests.cpp, to feed a real *.gta
// AssetType::Animation file through AnimationSystem::Play()'s own
// AnimationClipCache), but no GPU/SDL/ImGui - Tier 1.

#include "Game/Animation/AnimationSystem.h"

#include "Animation/AnimationPoseEvaluator.h"
#include "Animation/MotionSampler.h"
#include "Animation/SkeletonPose.h"
#include "Assets/AssetTypes.h"
#include "Assets/GtaFile.h"
#include "Assets/MotionFile.h"
#include "ECS/Components/DynamicChainRig.h"
#include "ECS/Components/MeshAssetSource.h"
#include "ECS/Components/ResolvedAnimationPose.h"
#include "ECS/Components/SkeletalAnimator.h"
#include "ECS/Registry.h"
#include "Game/Instantiation/MeshInstantiationSystem.h"
#include "Game/RenderSystem.h"

#include <filesystem>
#include <fstream>

#include <gtest/gtest.h>

namespace gte {
namespace {

class AnimationSystemEvaluatePosesTest : public ::testing::Test {
protected:
    void SetUp() override
    {
        const ::testing::TestInfo* info = ::testing::UnitTest::GetInstance()->current_test_info();
        m_root = std::filesystem::temp_directory_path()
            / (std::string("GteAnimationSystemEvaluatePosesTest_") + info->test_suite_name() + "_" + info->name());

        std::error_code ec;
        std::filesystem::remove_all(m_root, ec);
        std::filesystem::create_directories(m_root, ec);
    }

    void TearDown() override
    {
        std::error_code ec;
        std::filesystem::remove_all(m_root, ec);
    }

    // A minimal 2-bone skeleton (root -> child), no IK/append bones - keeps
    // the "expected pose" computable independently in the test itself via
    // Animation/MotionSampler.h's ResolveBoneTracksToSkeleton() +
    // EvaluateAnimatedPoseBeforePhysics(), without needing to reach into
    // AnimationSystem's own private caches.
    static SkeletonData BuildTwoBoneSkeleton()
    {
        SkeletonData skeleton;
        Bone root;
        root.name = "root";
        root.position = Vec3::Zero();
        root.parentBoneIndex = -1;
        skeleton.bones.push_back(root);

        Bone child;
        child.name = "child";
        child.position = Vec3{ 0.0f, 1.0f, 0.0f };
        child.parentBoneIndex = 0;
        skeleton.bones.push_back(child);

        return skeleton;
    }

    static MotionData BuildRootTranslationMotion()
    {
        MotionData motion;
        BoneKeyframe keyframe;
        keyframe.boneName = "root";
        keyframe.frame = 0;
        keyframe.translation = Vec3{ 0.5f, 0.25f, 0.0f };
        keyframe.rotation = Quat::Identity();
        motion.boneKeyframes.push_back(keyframe);
        return motion;
    }

    std::filesystem::path m_root;
};

TEST_F(AnimationSystemEvaluatePosesTest, EvaluatePosesWritesResolvedAnimationPoseMatchingManualEvaluation)
{
    RenderSystem renderSystem;
    MeshInstantiationSystem meshInstantiationSystem(renderSystem);
    AnimationSystem animationSystem(renderSystem, meshInstantiationSystem);

    const SkeletonData skeleton = BuildTwoBoneSkeleton();
    const MotionData motion = BuildRootTranslationMotion();

    SkinnedMeshData skinData;
    skinData.skeleton = skeleton;
    // No real mesh geometry needed for pose evaluation itself.

    const std::string meshPath = "TestModel.gta";
    animationSystem.RegisterSkinnedMesh(meshPath, skinData);

    const std::filesystem::path animationPath = m_root / "motion.gta";
    const std::vector<std::uint8_t> payload = EncodeMotionDataToBytes(motion);
    ASSERT_TRUE(WriteGtaFile(animationPath, AssetType::Animation, Guid::Generate(), AssetFlags::None, {}, payload));

    Registry registry;
    const Entity entity = registry.CreateEntity();
    registry.AddComponent<MeshAssetSource>(entity, MeshAssetSource{ meshPath });

    ASSERT_TRUE(animationSystem.Play(registry, entity, animationPath.string()));

    // deltaSeconds == 0 keeps frame at exactly 0.0f, so the "expected" pose
    // below can be computed at the same fixed frame number deterministically.
    animationSystem.EvaluatePoses(registry, 0.0);

    ASSERT_TRUE(registry.HasComponent<ResolvedAnimationPose>(entity));
    const ResolvedAnimationPose* resolvedPose = registry.TryGetComponent<ResolvedAnimationPose>(entity);
    ASSERT_NE(resolvedPose, nullptr);
    ASSERT_EQ(resolvedPose->pose.size(), skeleton.bones.size());

    // Ground truth, computed independently of AnimationSystem's own private
    // caches - proves EvaluatePoses() produced EXACTLY the pose the old,
    // single EvaluateAnimatedSkinningPose() call would have used internally.
    const ResolvedAnimationBinding expectedBinding = ResolveBoneTracksToSkeleton(skeleton, motion);
    const std::vector<BoneLocalOffset> expectedPose = EvaluateAnimatedPoseBeforePhysics(skeleton, expectedBinding, 0.0f);

    ASSERT_EQ(resolvedPose->pose.size(), expectedPose.size());
    for (std::size_t i = 0; i < expectedPose.size(); ++i) {
        EXPECT_TRUE(ApproximatelyEqual(resolvedPose->pose[i].translation, expectedPose[i].translation))
            << "Mismatch at bone index " << i;
        EXPECT_TRUE(ApproximatelyEqual(resolvedPose->pose[i].rotation, expectedPose[i].rotation))
            << "Mismatch at bone index " << i;
    }

    // The whole point of this split: skinning matrices computed FROM the
    // ResolvedAnimationPose component must be byte-identical to what the old
    // single-call EvaluateAnimatedSkinningPose() would have produced.
    const std::vector<Mat4> fromComponent = ComputeSkinningMatrices(skeleton, resolvedPose->pose);
    const std::vector<Mat4> fromOldSingleCall = EvaluateAnimatedSkinningPose(skeleton, expectedBinding, 0.0f);
    ASSERT_EQ(fromComponent.size(), fromOldSingleCall.size());
    for (std::size_t i = 0; i < fromComponent.size(); ++i) {
        EXPECT_TRUE(ApproximatelyEqual(fromComponent[i], fromOldSingleCall[i])) << "Mismatch at bone index " << i;
    }
}

TEST_F(AnimationSystemEvaluatePosesTest, SkinAndUploadDoesNotCrashWhenNoGpuMeshPartsAreRegistered)
{
    // SkinAndUpload() must degrade gracefully (never crash) when a model's
    // skinning data was registered but its GPU mesh parts never were (e.g.
    // this test's own minimal fixture, which never spawns a real Mesh) -
    // exactly the same "not (yet) registered - do nothing" convention
    // EvaluatePoses() itself already follows for a missing skinData/clip.
    RenderSystem renderSystem;
    MeshInstantiationSystem meshInstantiationSystem(renderSystem);
    AnimationSystem animationSystem(renderSystem, meshInstantiationSystem);

    const SkeletonData skeleton = BuildTwoBoneSkeleton();
    const MotionData motion = BuildRootTranslationMotion();

    SkinnedMeshData skinData;
    skinData.skeleton = skeleton;
    skinData.bindPositions = { Vec3::Zero(), Vec3{ 0.0f, 1.0f, 0.0f } };
    skinData.bindNormals = { Vec3::Up(), Vec3::Up() };
    skinData.uvs = { Vec2::Zero(), Vec2::Zero() };
    VertexSkinWeights weight0;
    weight0.boneIndices[0] = 0;
    skinData.skinWeights = { weight0, weight0 };

    const std::string meshPath = "TestModelWithVerts.gta";
    animationSystem.RegisterSkinnedMesh(meshPath, skinData);

    const std::filesystem::path animationPath = m_root / "motion.gta";
    const std::vector<std::uint8_t> payload = EncodeMotionDataToBytes(motion);
    ASSERT_TRUE(WriteGtaFile(animationPath, AssetType::Animation, Guid::Generate(), AssetFlags::None, {}, payload));

    Registry registry;
    const Entity entity = registry.CreateEntity();
    registry.AddComponent<MeshAssetSource>(entity, MeshAssetSource{ meshPath });
    ASSERT_TRUE(animationSystem.Play(registry, entity, animationPath.string()));

    animationSystem.EvaluatePoses(registry, 0.0);
    EXPECT_NO_FATAL_FAILURE(animationSystem.SkinAndUpload(registry));

    // ResolvedAnimationPose must be untouched by SkinAndUpload() - it only
    // ever READS it.
    const ResolvedAnimationPose* resolvedPose = registry.TryGetComponent<ResolvedAnimationPose>(entity);
    ASSERT_NE(resolvedPose, nullptr);
    EXPECT_EQ(resolvedPose->pose.size(), skeleton.bones.size());
}

TEST_F(AnimationSystemEvaluatePosesTest, SkinAndUploadSkipsAnEntityWithNoResolvedAnimationPoseYet)
{
    // Order-of-operations degrade-gracefully case: SkinAndUpload() must not
    // crash if called for an entity EvaluatePoses() never produced a pose
    // for this frame (e.g. it wasn't playing).
    RenderSystem renderSystem;
    MeshInstantiationSystem meshInstantiationSystem(renderSystem);
    AnimationSystem animationSystem(renderSystem, meshInstantiationSystem);

    const SkeletonData skeleton = BuildTwoBoneSkeleton();
    SkinnedMeshData skinData;
    skinData.skeleton = skeleton;
    animationSystem.RegisterSkinnedMesh("Idle.gta", skinData);

    Registry registry;
    const Entity entity = registry.CreateEntity();
    registry.AddComponent<MeshAssetSource>(entity, MeshAssetSource{ "Idle.gta" });
    SkeletalAnimator& animator = registry.AddComponent<SkeletalAnimator>(entity);
    animator.meshGtaPath = "Idle.gta";
    animator.animationGtaPath = "SomeClip.gta"; // never Play()'d, so never actually loadable.
    animator.playing = false; // Not playing - EvaluatePoses() should skip it.

    animationSystem.EvaluatePoses(registry, 0.016);
    EXPECT_FALSE(registry.HasComponent<ResolvedAnimationPose>(entity));

    EXPECT_NO_FATAL_FAILURE(animationSystem.SkinAndUpload(registry));
}

// --- Phase 1 (task_manager/verlet-integration-7/
// PHASE1_BASELINE_RESOLVED_POSE_FOR_NONANIMATED_PHYSICS_RIGS.md) - baseline
// ResolvedAnimationPose for a DynamicChainRig entity with no (or no
// currently-playing) SkeletalAnimator. ---

TEST_F(AnimationSystemEvaluatePosesTest, EvaluatePosesWritesBindPoseForADynamicChainRigOnlyEntityWithNoSkeletalAnimatorAtAll)
{
    RenderSystem renderSystem;
    MeshInstantiationSystem meshInstantiationSystem(renderSystem);
    AnimationSystem animationSystem(renderSystem, meshInstantiationSystem);

    const SkeletonData skeleton = BuildTwoBoneSkeleton();
    SkinnedMeshData skinData;
    skinData.skeleton = skeleton;

    const std::string meshPath = "TPoseOnly.gta";
    animationSystem.RegisterSkinnedMesh(meshPath, skinData);

    Registry registry;
    const Entity entity = registry.CreateEntity();
    registry.AddComponent<DynamicChainRig>(entity, DynamicChainRig{ meshPath, {}, 0.0f, true });

    animationSystem.EvaluatePoses(registry, 0.016);

    ASSERT_TRUE(registry.HasComponent<ResolvedAnimationPose>(entity));
    const ResolvedAnimationPose* resolvedPose = registry.TryGetComponent<ResolvedAnimationPose>(entity);
    ASSERT_NE(resolvedPose, nullptr);
    ASSERT_EQ(resolvedPose->pose.size(), skeleton.bones.size());
    for (std::size_t i = 0; i < resolvedPose->pose.size(); ++i) {
        EXPECT_TRUE(ApproximatelyEqual(resolvedPose->pose[i].translation, Vec3::Zero())) << "Mismatch at bone index " << i;
        EXPECT_TRUE(RepresentSameRotation(resolvedPose->pose[i].rotation, Quat::Identity())) << "Mismatch at bone index " << i;
    }
}

TEST_F(AnimationSystemEvaluatePosesTest,
    EvaluatePosesDoesNotClobberAFreshlyAnimatedPoseOnAnEntityThatHasBothAPlayingSkeletalAnimatorAndADynamicChainRig)
{
    RenderSystem renderSystem;
    MeshInstantiationSystem meshInstantiationSystem(renderSystem);
    AnimationSystem animationSystem(renderSystem, meshInstantiationSystem);

    const SkeletonData skeleton = BuildTwoBoneSkeleton();
    const MotionData motion = BuildRootTranslationMotion();

    SkinnedMeshData skinData;
    skinData.skeleton = skeleton;

    const std::string meshPath = "AnimatedWithRig.gta";
    animationSystem.RegisterSkinnedMesh(meshPath, skinData);

    const std::filesystem::path animationPath = m_root / "motion.gta";
    const std::vector<std::uint8_t> payload = EncodeMotionDataToBytes(motion);
    ASSERT_TRUE(WriteGtaFile(animationPath, AssetType::Animation, Guid::Generate(), AssetFlags::None, {}, payload));

    Registry registry;
    const Entity entity = registry.CreateEntity();
    registry.AddComponent<MeshAssetSource>(entity, MeshAssetSource{ meshPath });
    ASSERT_TRUE(animationSystem.Play(registry, entity, animationPath.string()));
    registry.AddComponent<DynamicChainRig>(entity, DynamicChainRig{ meshPath, {}, 0.0f, true });

    animationSystem.EvaluatePoses(registry, 0.0);

    const ResolvedAnimationPose* resolvedPose = registry.TryGetComponent<ResolvedAnimationPose>(entity);
    ASSERT_NE(resolvedPose, nullptr);

    const ResolvedAnimationBinding expectedBinding = ResolveBoneTracksToSkeleton(skeleton, motion);
    const std::vector<BoneLocalOffset> expectedPose = EvaluateAnimatedPoseBeforePhysics(skeleton, expectedBinding, 0.0f);

    ASSERT_EQ(resolvedPose->pose.size(), expectedPose.size());
    for (std::size_t i = 0; i < expectedPose.size(); ++i) {
        EXPECT_TRUE(ApproximatelyEqual(resolvedPose->pose[i].translation, expectedPose[i].translation))
            << "Mismatch at bone index " << i;
        EXPECT_TRUE(ApproximatelyEqual(resolvedPose->pose[i].rotation, expectedPose[i].rotation))
            << "Mismatch at bone index " << i;
    }
}

TEST_F(AnimationSystemEvaluatePosesTest,
    EvaluatePosesFallsBackToBindPoseForADynamicChainRigEntityWhoseSkeletalAnimatorExistsButIsNotPlaying)
{
    RenderSystem renderSystem;
    MeshInstantiationSystem meshInstantiationSystem(renderSystem);
    AnimationSystem animationSystem(renderSystem, meshInstantiationSystem);

    const SkeletonData skeleton = BuildTwoBoneSkeleton();
    const MotionData motion = BuildRootTranslationMotion();

    SkinnedMeshData skinData;
    skinData.skeleton = skeleton;

    const std::string meshPath = "StoppedAnimatorWithRig.gta";
    animationSystem.RegisterSkinnedMesh(meshPath, skinData);

    const std::filesystem::path animationPath = m_root / "motion.gta";
    const std::vector<std::uint8_t> payload = EncodeMotionDataToBytes(motion);
    ASSERT_TRUE(WriteGtaFile(animationPath, AssetType::Animation, Guid::Generate(), AssetFlags::None, {}, payload));

    Registry registry;
    const Entity entity = registry.CreateEntity();
    registry.AddComponent<MeshAssetSource>(entity, MeshAssetSource{ meshPath });
    ASSERT_TRUE(animationSystem.Play(registry, entity, animationPath.string()));
    registry.AddComponent<DynamicChainRig>(entity, DynamicChainRig{ meshPath, {}, 0.0f, true });

    // First call: animator is playing a real clip - proves the fixture
    // actually animates (a non-identity pose).
    animationSystem.EvaluatePoses(registry, 0.0);
    const ResolvedAnimationPose* animatedPose = registry.TryGetComponent<ResolvedAnimationPose>(entity);
    ASSERT_NE(animatedPose, nullptr);
    bool anyNonIdentity = false;
    for (const BoneLocalOffset& offset : animatedPose->pose) {
        if (!ApproximatelyEqual(offset.translation, Vec3::Zero())) {
            anyNonIdentity = true;
            break;
        }
    }
    ASSERT_TRUE(anyNonIdentity) << "Fixture did not actually animate - test premise invalid.";

    // Second call: animator has stopped playing - the entity must fall back
    // to a clean bind-pose baseline, not get stuck holding stale animated
    // data.
    SkeletalAnimator* animator = registry.TryGetComponent<SkeletalAnimator>(entity);
    ASSERT_NE(animator, nullptr);
    animator->playing = false;

    animationSystem.EvaluatePoses(registry, 0.0);
    const ResolvedAnimationPose* fallbackPose = registry.TryGetComponent<ResolvedAnimationPose>(entity);
    ASSERT_NE(fallbackPose, nullptr);
    ASSERT_EQ(fallbackPose->pose.size(), skeleton.bones.size());
    for (std::size_t i = 0; i < fallbackPose->pose.size(); ++i) {
        EXPECT_TRUE(ApproximatelyEqual(fallbackPose->pose[i].translation, Vec3::Zero())) << "Mismatch at bone index " << i;
        EXPECT_TRUE(RepresentSameRotation(fallbackPose->pose[i].rotation, Quat::Identity())) << "Mismatch at bone index " << i;
    }
}

TEST_F(AnimationSystemEvaluatePosesTest, EvaluatePosesSkipsADisabledDynamicChainRigEntirely)
{
    RenderSystem renderSystem;
    MeshInstantiationSystem meshInstantiationSystem(renderSystem);
    AnimationSystem animationSystem(renderSystem, meshInstantiationSystem);

    const SkeletonData skeleton = BuildTwoBoneSkeleton();
    SkinnedMeshData skinData;
    skinData.skeleton = skeleton;

    const std::string meshPath = "DisabledRig.gta";
    animationSystem.RegisterSkinnedMesh(meshPath, skinData);

    Registry registry;
    const Entity entity = registry.CreateEntity();
    registry.AddComponent<DynamicChainRig>(entity, DynamicChainRig{ meshPath, {}, 0.0f, false });

    animationSystem.EvaluatePoses(registry, 0.016);

    EXPECT_FALSE(registry.HasComponent<ResolvedAnimationPose>(entity));
}

TEST_F(AnimationSystemEvaluatePosesTest, EvaluatePosesDegradesGracefullyForADynamicChainRigWhoseModelWasNeverRegistered)
{
    RenderSystem renderSystem;
    MeshInstantiationSystem meshInstantiationSystem(renderSystem);
    AnimationSystem animationSystem(renderSystem, meshInstantiationSystem);

    Registry registry;
    const Entity entity = registry.CreateEntity();
    registry.AddComponent<DynamicChainRig>(entity, DynamicChainRig{ "NeverRegistered.gta", {}, 0.0f, true });

    EXPECT_NO_FATAL_FAILURE(animationSystem.EvaluatePoses(registry, 0.016));

    EXPECT_FALSE(registry.HasComponent<ResolvedAnimationPose>(entity));
}

} // namespace
} // namespace gte
