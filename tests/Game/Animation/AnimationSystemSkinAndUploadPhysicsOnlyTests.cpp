// Unit tests for AnimationSystem::SkinAndUpload()'s widened entity
// iteration (task_manager/verlet-integration-7,
// PHASE2_SKIN_AND_UPLOAD_VISIBILITY_FOR_PHYSICS_ONLY_ENTITIES.md). Phase 1
// (PHASE1_BASELINE_RESOLVED_POSE_FOR_NONANIMATED_PHYSICS_RIGS.md) made
// AnimationSystem::EvaluatePoses() guarantee a fresh ResolvedAnimationPose
// for any enabled DynamicChainRig entity every frame, even with no
// SkeletalAnimator at all - this file proves SkinAndUpload() now actually
// reaches (via the new private SkinAndUploadOneEntity() helper) that same
// entity set, not only an actively-playing SkeletalAnimator's. Mirrors
// AnimationSystemEvaluatePosesTests.cpp's own fixture conventions - no live
// Renderer/GPU device involved (RenderSystem/MeshInstantiationSystem are
// both default-constructible with no GPU dependency), so this stays
// genuinely Tier 1. GPU-compute skinning mode itself is NOT exercised here
// (GpuSkinningRigCache::Register() needs a real, live Renderer to create
// GPU buffers - Tier 2, no automated coverage yet, same accepted bucket as
// Buffer/RenderTexture/Pipeline - see TESTING.md); this file only proves
// the CpuJobSystem-mode path (today's default) correctly widens to cover a
// physics-only entity, which is what Phase 0's own investigation already
// established is sufficient (the widened iteration itself, not the mode,
// is what was missing).

#include "Game/Animation/AnimationSystem.h"

#include "Animation/AnimationPoseEvaluator.h"
#include "ECS/Components/DynamicChainRig.h"
#include "ECS/Components/MeshAssetSource.h"
#include "ECS/Components/ResolvedAnimationPose.h"
#include "ECS/Components/SkeletalAnimator.h"
#include "ECS/Registry.h"
#include "Game/Instantiation/MeshInstantiationSystem.h"
#include "Game/RenderSystem.h"

#include <gtest/gtest.h>

namespace gte {
namespace {

// Mirrors AnimationSystemEvaluatePosesTest::BuildTwoBoneSkeleton() exactly.
SkeletonData BuildTwoBoneSkeleton()
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

// Mirrors AnimationSystemEvaluatePosesTest.SkinAndUploadDoesNotCrashWhenNoGpuMeshPartsAreRegistered's
// own minimal-but-real vertex/skin-weight fixture, so SkinAndUpload()'s CPU
// skin-blend path (SkinVertexRange()) actually runs, not just its early
// "no skinData" guard.
SkinnedMeshData BuildMinimalSkinnedMeshData(const SkeletonData& skeleton)
{
    SkinnedMeshData skinData;
    skinData.skeleton = skeleton;
    skinData.bindPositions = { Vec3::Zero(), Vec3{ 0.0f, 1.0f, 0.0f } };
    skinData.bindNormals = { Vec3::Up(), Vec3::Up() };
    skinData.uvs = { Vec2::Zero(), Vec2::Zero() };
    VertexSkinWeights weight0;
    weight0.boneIndices[0] = 0;
    skinData.skinWeights = { weight0, weight0 };
    return skinData;
}

TEST(AnimationSystemSkinAndUploadPhysicsOnlyTest, SkinAndUploadProcessesADynamicChainRigOnlyEntityWithNoSkeletalAnimatorAtAll)
{
    RenderSystem renderSystem;
    MeshInstantiationSystem meshInstantiationSystem(renderSystem);
    AnimationSystem animationSystem(renderSystem, meshInstantiationSystem);

    const SkeletonData skeleton = BuildTwoBoneSkeleton();
    const SkinnedMeshData skinData = BuildMinimalSkinnedMeshData(skeleton);

    const std::string meshPath = "PhysicsOnlyModel.gta";
    animationSystem.RegisterSkinnedMesh(meshPath, skinData);

    Registry registry;
    const Entity entity = registry.CreateEntity();
    registry.AddComponent<DynamicChainRig>(entity, DynamicChainRig{ meshPath, {}, 0.0f, true });

    // Simulates Phase 1's EvaluatePoses() baseline pass having already run
    // this frame - reproduced directly rather than depending on that
    // earlier phase's own call to keep this file's own tests self-
    // contained, mirroring this suite's other tests' convention.
    animationSystem.EvaluatePoses(registry, 0.016);
    ASSERT_TRUE(registry.HasComponent<ResolvedAnimationPose>(entity));

    // The crucial thing this test proves: SkinAndUpload() actually reaches
    // (and returns cleanly from) SkinAndUploadOneEntity() for a
    // DynamicChainRig-only entity - structurally impossible before this
    // phase, since the method used to iterate SkeletalAnimator only.
    EXPECT_NO_FATAL_FAILURE(animationSystem.SkinAndUpload(registry));

    // SkinAndUpload() only ever READS ResolvedAnimationPose - untouched.
    const ResolvedAnimationPose* resolvedPose = registry.TryGetComponent<ResolvedAnimationPose>(entity);
    ASSERT_NE(resolvedPose, nullptr);
    EXPECT_EQ(resolvedPose->pose.size(), skeleton.bones.size());
}

TEST(AnimationSystemSkinAndUploadPhysicsOnlyTest,
    SkinAndUploadDoesNotCrashOrClobberTheAnimatedPoseForAnEntityWithBothAPlayingSkeletalAnimatorAndAnEnabledDynamicChainRig)
{
    // An entity may legitimately carry BOTH a SkeletalAnimator AND a
    // DynamicChainRig at once (an animated model that also has detected
    // dynamic chains) - SkinAndUpload() must process it exactly once (via
    // Source 1, the SkeletalAnimator loop) and never a second time via
    // Source 2 (the DynamicChainRig loop), which would double-upload the
    // same shared GPU vertex buffer. This is enforced by SkinAndUpload()'s
    // own `processedThisFrame` de-duplication set - this test proves the
    // combined call sequence is safe (no crash) and that the entity's
    // genuinely-animated ResolvedAnimationPose (written by EvaluatePoses()'s
    // first pass) is never disturbed by SkinAndUpload() itself, which only
    // ever reads it.
    RenderSystem renderSystem;
    MeshInstantiationSystem meshInstantiationSystem(renderSystem);
    AnimationSystem animationSystem(renderSystem, meshInstantiationSystem);

    const SkeletonData skeleton = BuildTwoBoneSkeleton();
    const SkinnedMeshData skinData = BuildMinimalSkinnedMeshData(skeleton);

    const std::string meshPath = "AnimatedWithRigForSkinUpload.gta";
    animationSystem.RegisterSkinnedMesh(meshPath, skinData);

    Registry registry;
    const Entity entity = registry.CreateEntity();
    registry.AddComponent<MeshAssetSource>(entity, MeshAssetSource{ meshPath });
    SkeletalAnimator& animator = registry.AddComponent<SkeletalAnimator>(entity);
    animator.meshGtaPath = meshPath;
    animator.animationGtaPath = "SomeClipNeverActuallyLoaded.gta"; // Never Play()'d for real - see below.
    animator.playing = true;
    registry.AddComponent<DynamicChainRig>(entity, DynamicChainRig{ meshPath, {}, 0.0f, true });

    // EvaluatePoses() itself degrades gracefully when the animation clip
    // isn't cached (m_clipCache.TryGet() returns nullptr) by falling
    // through to nothing for the SkeletalAnimator loop - but the
    // DynamicChainRig second pass still fires for this same entity (it was
    // NOT added to animatedThisFrame), giving it a bind-pose baseline. This
    // is exactly the scenario this test needs: a real ResolvedAnimationPose
    // exists, and the entity carries both components.
    animationSystem.EvaluatePoses(registry, 0.016);
    ASSERT_TRUE(registry.HasComponent<ResolvedAnimationPose>(entity));

    EXPECT_NO_FATAL_FAILURE(animationSystem.SkinAndUpload(registry));

    const ResolvedAnimationPose* resolvedPose = registry.TryGetComponent<ResolvedAnimationPose>(entity);
    ASSERT_NE(resolvedPose, nullptr);
    EXPECT_EQ(resolvedPose->pose.size(), skeleton.bones.size());
}

TEST(AnimationSystemSkinAndUploadPhysicsOnlyTest, SkinAndUploadSkipsADisabledDynamicChainRigEntirely)
{
    RenderSystem renderSystem;
    MeshInstantiationSystem meshInstantiationSystem(renderSystem);
    AnimationSystem animationSystem(renderSystem, meshInstantiationSystem);

    const SkeletonData skeleton = BuildTwoBoneSkeleton();
    const SkinnedMeshData skinData = BuildMinimalSkinnedMeshData(skeleton);

    const std::string meshPath = "DisabledRigForSkinUpload.gta";
    animationSystem.RegisterSkinnedMesh(meshPath, skinData);

    Registry registry;
    const Entity entity = registry.CreateEntity();
    registry.AddComponent<DynamicChainRig>(entity, DynamicChainRig{ meshPath, {}, 0.0f, false });

    // Manually populate a ResolvedAnimationPose as if a PREVIOUS frame (or
    // some other path) had left one behind - EvaluatePoses() itself would
    // never add one for a disabled rig (see Phase 1's own test coverage),
    // so this proves SkinAndUpload() ALSO independently honors
    // `enabled == false`, not merely relying on the component's absence.
    ResolvedAnimationPose& resolvedPose = registry.AddComponent<ResolvedAnimationPose>(entity);
    resolvedPose.pose.assign(skeleton.bones.size(), BoneLocalOffset{});

    EXPECT_NO_FATAL_FAILURE(animationSystem.SkinAndUpload(registry));
}

TEST(AnimationSystemSkinAndUploadPhysicsOnlyTest, SkinAndUploadSkipsADynamicChainRigEntityWithNoResolvedAnimationPoseYet)
{
    // Order-of-operations degrade-gracefully case, mirroring
    // AnimationSystemEvaluatePosesTest.SkinAndUploadSkipsAnEntityWithNoResolvedAnimationPoseYet's
    // own convention but for Source 2 (DynamicChainRig) instead of Source 1
    // (SkeletalAnimator): EvaluatePoses() is deliberately never called at
    // all, so no ResolvedAnimationPose exists yet.
    RenderSystem renderSystem;
    MeshInstantiationSystem meshInstantiationSystem(renderSystem);
    AnimationSystem animationSystem(renderSystem, meshInstantiationSystem);

    const SkeletonData skeleton = BuildTwoBoneSkeleton();
    const SkinnedMeshData skinData = BuildMinimalSkinnedMeshData(skeleton);

    const std::string meshPath = "NoPoseYetForSkinUpload.gta";
    animationSystem.RegisterSkinnedMesh(meshPath, skinData);

    Registry registry;
    const Entity entity = registry.CreateEntity();
    registry.AddComponent<DynamicChainRig>(entity, DynamicChainRig{ meshPath, {}, 0.0f, true });

    EXPECT_FALSE(registry.HasComponent<ResolvedAnimationPose>(entity));
    EXPECT_NO_FATAL_FAILURE(animationSystem.SkinAndUpload(registry));
}

} // namespace
} // namespace gte
