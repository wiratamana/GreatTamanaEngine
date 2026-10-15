#include "AnimationSystem.h"

#include "../../Animation/AnimationPoseEvaluator.h"
#include "../../Animation/SkeletonPose.h"
#include "../../Animation/VertexSkinning.h"
#include "../../ECS/Components/DynamicChainRig.h"
#include "../../ECS/Components/MeshAssetSource.h"
#include "../../ECS/Components/MeshRenderer.h"
#include "../../ECS/Components/ResolvedAnimationPose.h"
#include "../../ECS/Components/SkeletalAnimator.h"
#include "../../ECS/TransformHierarchy.h"
#include "../../Jobs/JobDispatch.h"
#include "../../Jobs/JobSystem.h"
#include "../../Profiling/JobScopeTimer.h"
#include "../../Profiling/ScopeTimer.h"
#include "../../Renderer/Mesh.h"
#include "../../Renderer/MeshVertex.h"
#include "../Instantiation/MeshInstantiationSystem.h"
#include "../Instantiation/MeshAssetPartGrouping.h"
#include "../Instantiation/MeshVertexPacking.h"
#include "../RenderSystem.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <unordered_set>

namespace gte {

namespace {

// VMD motions use a fixed 30 frames per second grid.
constexpr float kVmdFramesPerSecond = 30.0f;

// Below this vertex count skinning and packing run inline: a job dispatch would cost more than the work.
constexpr std::size_t kMinVerticesToParallelize = 512;

// Never split a model into batches smaller than this many vertices.
constexpr std::uint32_t kMinVerticesPerBatch = 256;

// Per-batch job context: pointers into data owned by the caller, which outlives the dispatch and its wait.
struct SkinningBatchContext {
    const std::vector<Vec3>* bindPositions;
    const std::vector<Vec3>* bindNormals;
    const std::vector<VertexSkinWeights>* skinWeights;
    const std::vector<Mat4>* skinningMatrices;
    std::vector<Vec3>* outPositions;
    std::vector<Vec3>* outNormals;
};

// Job body: skins one batch's slice of vertices. Inputs are read-only and shared; each batch writes a disjoint slice.
void RunSkinningBatch(std::uint32_t beginIndex, std::uint32_t endIndex, void* payload)
{
    // Job bodies are profiled only with the job scope macro.
    GTE_PROFILE_JOB_SCOPE("SkinVertices");

    SkinningBatchContext* context = static_cast<SkinningBatchContext*>(payload);
    SkinVertexRange(beginIndex, endIndex, *context->bindPositions, *context->bindNormals, *context->skinWeights,
        *context->skinningMatrices, *context->outPositions, *context->outNormals);
}

// Packing is parallel per vertex like the skin blend; these mirror SkinningBatchContext for the pack functions.
struct PackUntexturedBatchContext {
    const std::vector<Vec3>* positions;
    const std::vector<Vec3>* normals;
    std::vector<MeshVertex>* out;
};

void RunPackUntexturedBatch(std::uint32_t beginIndex, std::uint32_t endIndex, void* payload)
{
    GTE_PROFILE_JOB_SCOPE("PackMeshVertices");
    PackUntexturedBatchContext* context = static_cast<PackUntexturedBatchContext*>(payload);
    PackMeshVertexRange(beginIndex, endIndex, *context->positions, *context->normals, *context->out);
}

struct PackTexturedBatchContext {
    const std::vector<Vec3>* positions;
    const std::vector<Vec3>* normals;
    const std::vector<Vec2>* uvs;
    std::vector<MeshVertexUv>* out;
};

void RunPackTexturedBatch(std::uint32_t beginIndex, std::uint32_t endIndex, void* payload)
{
    GTE_PROFILE_JOB_SCOPE("PackMeshVertexUvs");
    PackTexturedBatchContext* context = static_cast<PackTexturedBatchContext*>(payload);
    PackMeshVertexUvRange(beginIndex, endIndex, *context->positions, *context->normals, *context->uvs, *context->out);
}

// Points every MeshRenderer on the entity's direct children (a model's parts) at the CPU- or GPU-skinned mesh that
// matches `mode`. Idempotent: a renderer already on the right mesh is left alone.
void ApplyMeshHandleForSkinningMode(Registry& registry, Entity animatorEntity,
    const GpuSkinningRigCache::GpuModelEntry& gpuEntry, AnimationSystem::SkinningMode mode)
{
    const std::vector<Entity> children = GetChildren(registry, animatorEntity);
    for (const Entity child : children) {
        MeshRenderer* meshRenderer = registry.TryGetComponent<MeshRenderer>(child);
        if (meshRenderer == nullptr) {
            continue;
        }

        if (mode == AnimationSystem::SkinningMode::GpuCompute) {
            const MeshHandle gpuHandle = gpuEntry.TryGetGpuMeshHandle(meshRenderer->mesh);
            if (gpuHandle.IsValid()) {
                meshRenderer->mesh = gpuHandle;
            }
        } else {
            const MeshHandle cpuHandle = gpuEntry.TryGetCpuMeshHandle(meshRenderer->mesh);
            if (cpuHandle.IsValid()) {
                meshRenderer->mesh = cpuHandle;
            }
        }
    }
}

} // namespace

bool AnimationSystem::Play(Registry& registry, Entity targetEntity, const std::string& absoluteAnimationGtaPath)
{
    if (!registry.IsAlive(targetEntity)) {
        return false;
    }

    const MeshAssetSource* source = registry.TryGetComponent<MeshAssetSource>(targetEntity);
    if (source == nullptr) {
        return false; // Not a model root spawned by MeshInstantiationSystem::SpawnMeshAsset().
    }

    const SkinnedMeshData* skinData = m_rigCache.TryGet(source->gtaPath);
    if (skinData == nullptr || skinData->skeleton.bones.empty()) {
        return false; // A boneless/riggless model - nothing to animate.
    }

    if (m_clipCache.GetOrLoad(absoluteAnimationGtaPath) == nullptr) {
        return false;
    }

    SkeletalAnimator& animator = registry.AddComponent<SkeletalAnimator>(targetEntity);
    animator.meshGtaPath = source->gtaPath;
    animator.animationGtaPath = absoluteAnimationGtaPath;
    animator.frame = 0.0f;
    animator.speed = 1.0f;
    animator.playing = true;
    animator.loop = true;
    return true;
}

void AnimationSystem::EvaluatePoses(Registry& registry, double deltaSeconds)
{
    GTE_PROFILE_SCOPE("AnimationSystem::EvaluatePoses");

    ComponentStorage<SkeletalAnimator>& animators = registry.Storage<SkeletalAnimator>();

    // Entities given an animated pose below; the baseline pass must not overwrite them.
    std::unordered_set<Entity> animatedThisFrame;

    // This loop touches no GPU state.
    for (std::size_t i = 0; i < animators.Size(); ++i) {
        SkeletalAnimator& animator = animators.ComponentAt(i);
        const Entity animatorEntity = animators.EntityAt(i);
        if (!animator.playing || animator.animationGtaPath.empty()) {
            continue;
        }

        const SkinnedMeshData* skinData = m_rigCache.TryGet(animator.meshGtaPath);
        if (skinData == nullptr) {
            continue; // Its model's own skinning data isn't (or is no longer) cached - nothing to do.
        }

        const MotionData* motion = m_clipCache.TryGet(animator.animationGtaPath);
        if (motion == nullptr) {
            continue; // Its clip isn't (or is no longer) cached.
        }

        // Resolved once per distinct (mesh, animation) pair, then reused
        // every frame afterwards - see ResolvedAnimationBindingCache.h.
        const AnimationBindingKey bindingKey{ animator.meshGtaPath, animator.animationGtaPath };
        const ResolvedAnimationBinding& binding = m_bindingCache.GetOrCompute(bindingKey, skinData->skeleton, *motion);

        animator.frame += static_cast<float>(deltaSeconds) * kVmdFramesPerSecond * animator.speed;
        if (binding.lastFrame > 0) {
            const float loopLength = static_cast<float>(binding.lastFrame) + 1.0f;
            if (animator.loop) {
                animator.frame = std::fmod(animator.frame, loopLength);
                if (animator.frame < 0.0f) {
                    animator.frame += loopLength;
                }
            } else if (animator.frame > static_cast<float>(binding.lastFrame)) {
                animator.frame = static_cast<float>(binding.lastFrame);
                animator.playing = false;
            }
        }

        // Sample, IK, then append, in that fixed order. The pose stops before the skinning matrices so physics can
        // adjust it; GPU mode also evaluates the pose on the CPU.
        std::vector<BoneLocalOffset> pose = EvaluateAnimatedPoseBeforePhysics(skinData->skeleton, binding, animator.frame);

        ResolvedAnimationPose* resolvedPose = registry.TryGetComponent<ResolvedAnimationPose>(animatorEntity);
        if (resolvedPose == nullptr) {
            resolvedPose = &registry.AddComponent<ResolvedAnimationPose>(animatorEntity);
        }
        resolvedPose->pose = std::move(pose);
        animatedThisFrame.insert(animatorEntity);
    }

    // Entities with an enabled DynamicChainRig and no animated pose get a bind-pose baseline, so physics also runs on
    // models that never played a clip. Always a full overwrite; it runs before physics every frame.
    ComponentStorage<DynamicChainRig>& rigs = registry.Storage<DynamicChainRig>();
    for (std::size_t i = 0; i < rigs.Size(); ++i) {
        DynamicChainRig& rig = rigs.ComponentAt(i);
        if (!rig.enabled) {
            continue; // Mirrors PhysicsSystem::Update()'s own "disabled -> skip entirely" convention - no wasted work.
        }
        const Entity entity = rigs.EntityAt(i);
        if (animatedThisFrame.count(entity) > 0) {
            continue; // Already given a fresh, genuinely-ANIMATED pose above this same call - never clobber it.
        }

        const SkinnedMeshData* skinData = m_rigCache.TryGet(rig.meshGtaPath);
        if (skinData == nullptr) {
            continue; // Not (yet) registered - degrade gracefully, same convention as the loop above.
        }

        ResolvedAnimationPose* resolvedPose = registry.TryGetComponent<ResolvedAnimationPose>(entity);
        if (resolvedPose == nullptr) {
            resolvedPose = &registry.AddComponent<ResolvedAnimationPose>(entity);
        }
        resolvedPose->pose.assign(skinData->skeleton.bones.size(), BoneLocalOffset{});
    }
}

// Skins and uploads one entity's model, found through a playing animator or an enabled DynamicChainRig.
void AnimationSystem::SkinAndUploadOneEntity(
    Registry& registry, Entity entity, const std::string& meshGtaPath, SkinningMode mode)
{
    const SkinnedMeshData* skinData = m_rigCache.TryGet(meshGtaPath);
    if (skinData == nullptr) {
        return; // Its model's own skinning data isn't (or is no longer) cached - nothing to do.
    }

    // Reads the pose EvaluatePoses() (and physics in between) left; skips an entity with none.
    const ResolvedAnimationPose* resolvedPose = registry.TryGetComponent<ResolvedAnimationPose>(entity);
    if (resolvedPose == nullptr) {
        return;
    }
    const std::vector<Mat4> skinningMatrices = ComputeSkinningMatrices(skinData->skeleton, resolvedPose->pose);

    // Keep the model's renderers on the mesh that matches the current mode, so a mode switch applies next frame.
    const GpuSkinningRigCache::GpuModelEntry* gpuEntry = m_gpuRigCache.TryGet(meshGtaPath);
    if (gpuEntry != nullptr) {
        ApplyMeshHandleForSkinningMode(registry, entity, *gpuEntry, mode);
    }

    if (mode == SkinningMode::GpuCompute) {
        // The whole CPU cost in GPU mode is one upload; the dispatch is recorded later by the skinning pass.
        if (gpuEntry == nullptr) {
            return; // Never registered for GPU skinning.
        }
        gpuEntry->boneMatricesBuffer.Upload(skinningMatrices.data(), skinningMatrices.size() * sizeof(Mat4));

        if (std::find(m_gpuModelsNeedingDispatchThisFrame.begin(), m_gpuModelsNeedingDispatchThisFrame.end(),
                meshGtaPath)
            == m_gpuModelsNeedingDispatchThisFrame.end()) {
            m_gpuModelsNeedingDispatchThisFrame.push_back(meshGtaPath);
        }
        return;
    }

    // CpuJobSystem mode. The scratch arrays are kept per model between frames; resize() allocates only the first time.
    const std::size_t vertexCount = skinData->bindPositions.size();
    AnimatorScratchBuffers& scratch = m_scratchBuffers[meshGtaPath];
    std::vector<Vec3>& skinnedPositions = scratch.skinnedPositions;
    std::vector<Vec3>& skinnedNormals = scratch.skinnedNormals;
    skinnedPositions.resize(vertexCount);
    skinnedNormals.resize(vertexCount);

    // CPU skinning runs on the worker pool for models large enough to benefit, inline otherwise; the result is the same.
    if (vertexCount < kMinVerticesToParallelize) {
        SkinVertexRange(0, static_cast<std::uint32_t>(vertexCount), skinData->bindPositions,
            skinData->bindNormals, skinData->skinWeights, skinningMatrices, skinnedPositions, skinnedNormals);
    } else {
        SkinningBatchContext context{ &skinData->bindPositions, &skinData->bindNormals, &skinData->skinWeights,
            &skinningMatrices, &skinnedPositions, &skinnedNormals };
        Jobs::JobHandle skinningHandle;
        Jobs::Dispatch(&RunSkinningBatch, static_cast<std::uint32_t>(vertexCount), &context, skinningHandle,
            kMinVerticesPerBatch);
        // One wait per model: the next model's dispatch must not start before this one finishes.
        Jobs::JobSystem::Instance().WaitForJobs(skinningHandle);
    }

    const std::vector<MeshAssetPart>* parts = m_meshInstantiationSystem.TryGetMeshAssetParts(meshGtaPath);
    if (parts == nullptr) {
        return;
    }

    // Parts that share one GPU vertex buffer are grouped so each distinct buffer is packed and uploaded once.
    const std::vector<MeshAssetPartGroup> groups = GroupMeshAssetPartsBySharedVertexBuffer(m_renderSystem, *parts);

    // For each distinct vertex buffer: pack from the freshly skinned arrays (on the worker pool for large models)
    // and upload once. The upload stays on the main thread.
    for (const MeshAssetPartGroup& group : groups) {
        if (group.textured) {
            std::vector<MeshVertexUv>& packed = scratch.packedTextured;
            packed.resize(vertexCount);

            if (vertexCount < kMinVerticesToParallelize) {
                PackMeshVertexUvRange(
                    0, static_cast<std::uint32_t>(vertexCount), skinnedPositions, skinnedNormals, skinData->uvs, packed);
            } else {
                PackTexturedBatchContext context{ &skinnedPositions, &skinnedNormals, &skinData->uvs, &packed };
                Jobs::JobHandle packHandle;
                Jobs::Dispatch(&RunPackTexturedBatch, static_cast<std::uint32_t>(vertexCount), &context,
                    packHandle, kMinVerticesPerBatch);
                Jobs::JobSystem::Instance().WaitForJobs(packHandle);
            }

            group.representativeMesh->UpdateVertexData(packed.data(), packed.size() * sizeof(MeshVertexUv));
        } else {
            std::vector<MeshVertex>& packed = scratch.packedUntextured;
            packed.resize(vertexCount);

            if (vertexCount < kMinVerticesToParallelize) {
                PackMeshVertexRange(
                    0, static_cast<std::uint32_t>(vertexCount), skinnedPositions, skinnedNormals, packed);
            } else {
                PackUntexturedBatchContext context{ &skinnedPositions, &skinnedNormals, &packed };
                Jobs::JobHandle packHandle;
                Jobs::Dispatch(&RunPackUntexturedBatch, static_cast<std::uint32_t>(vertexCount), &context,
                    packHandle, kMinVerticesPerBatch);
                Jobs::JobSystem::Instance().WaitForJobs(packHandle);
            }

            group.representativeMesh->UpdateVertexData(packed.data(), packed.size() * sizeof(MeshVertex));
        }
    }
}

void AnimationSystem::SkinAndUpload(Registry& registry)
{
    GTE_PROFILE_SCOPE("AnimationSystem::SkinAndUpload");

    // The mode is captured once so one model is never processed under two modes.
    const SkinningMode mode = m_mode;
    m_gpuModelsNeedingDispatchThisFrame.clear();

    // Models must be processed strictly one at a time, whatever source found them: instances of one model share
    // GPU mesh buffers, so overlapping two models' dispatches would be a data race. This holds in GPU mode too.
    std::unordered_set<Entity> processedThisFrame;

    // Source 1: every playing SkeletalAnimator.
    ComponentStorage<SkeletalAnimator>& animators = registry.Storage<SkeletalAnimator>();
    for (std::size_t i = 0; i < animators.Size(); ++i) {
        SkeletalAnimator& animator = animators.ComponentAt(i);
        const Entity animatorEntity = animators.EntityAt(i);
        if (!animator.playing || animator.animationGtaPath.empty()) {
            continue;
        }
        SkinAndUploadOneEntity(registry, animatorEntity, animator.meshGtaPath, mode);
        processedThisFrame.insert(animatorEntity);
    }

    // Source 2: enabled DynamicChainRig entities with a fresh pose that source 1 did not process (an entity may
    // carry both components and must be uploaded only once).
    ComponentStorage<DynamicChainRig>& rigs = registry.Storage<DynamicChainRig>();
    for (std::size_t i = 0; i < rigs.Size(); ++i) {
        DynamicChainRig& rig = rigs.ComponentAt(i);
        if (!rig.enabled) {
            continue;
        }
        const Entity entity = rigs.EntityAt(i);
        if (processedThisFrame.count(entity) > 0) {
            continue;
        }
        if (!registry.HasComponent<ResolvedAnimationPose>(entity)) {
            continue; // EvaluatePoses() produced no pose for it this frame.
        }
        SkinAndUploadOneEntity(registry, entity, rig.meshGtaPath, mode);
    }
}

std::vector<AnimationSystem::GpuSkinningDispatchRequest> AnimationSystem::CollectModelsNeedingGpuSkinningThisFrame() const
{
    std::vector<GpuSkinningDispatchRequest> requests;

    for (const std::string& absoluteGtaPath : m_gpuModelsNeedingDispatchThisFrame) {
        const GpuSkinningRigCache::GpuModelEntry* entry = m_gpuRigCache.TryGet(absoluteGtaPath);
        if (entry == nullptr) {
            continue; // Shouldn't normally happen (it was registered when the upload above succeeded), but degrade gracefully.
        }

        for (const GpuSkinningRigCache::OutputGroup& group : entry->outputGroups) {
            if (!group.outputVertexBuffer.IsValid()) {
                continue;
            }
            if (group.isTextured && !entry->uvBuffer.has_value()) {
                continue; // The textured shader needs the UV input; the untextured one would write the wrong stride.
            }

            GpuSkinningDispatchRequest request;
            request.name = group.debugName.c_str();
            request.dispatch.bindPose = &entry->bindPoseBuffer;
            request.dispatch.skinWeights = &entry->skinWeightsBuffer;
            request.dispatch.boneMatrices = &entry->boneMatricesBuffer;
            request.dispatch.uv = group.isTextured && entry->uvBuffer.has_value() ? &*entry->uvBuffer : nullptr;
            request.dispatch.output = &group.outputVertexBuffer;
            request.dispatch.vertexCount = group.vertexCount;
            request.dispatch.instance = group.instanceKey;
            requests.push_back(request);
        }
    }

    return requests;
}

} // namespace gte
