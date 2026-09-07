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

// VMD's own fixed frame grid - see Assets/MotionData.h's own file comment.
constexpr float kVmdFramesPerSecond = 30.0f;

// Job System Phase 6 (First Production Consumer - Animation / Vertex
// Skinning - see AGENTS.md, "Job System", and
// task_manager/job_system/JOB_SYSTEM_PHASE6_COMPLETION_REPORT.md): below
// this vertex count, CPU vertex skinning (and, as of the multithreaded
// CPU-skinning optimization below, vertex PACKING too) runs inline,
// serially, on the calling (main) thread - scheduling a
// gte::Jobs::Dispatch() for a genuinely tiny model would spend more time
// on the Job System's own per-Dispatch() scheduling overhead than the
// actual work itself (see
// JOBSYSTEM_PHASE6_FIRST_PRODUCTION_CONSUMER_ANIMATION_SKINNING_v2.md,
// Step 2, point 1 in the wider campaign's own Phase 2 rationale).
constexpr std::size_t kMinVerticesToParallelize = 512;

// The floor gte::Jobs::Dispatch() itself uses when splitting a large
// model's vertex array into batches - never split smaller than this many
// vertices into their own batch, even if that means fewer batches than
// there are workers.
constexpr std::uint32_t kMinVerticesPerBatch = 256;

// The per-batch job context handed through gte::Jobs::Dispatch()'s opaque
// payload pointer. Every field is a plain pointer into data owned by THIS
// call's own SkeletalAnimator iteration (skinData's cached bind-pose
// arrays, the freshly-computed skinningMatrices, and this call's own
// skinnedPositions/skinnedNormals output vectors) - all of it outlives the
// whole Dispatch()+WaitForJobs() bracket below, since nothing else touches
// it until WaitForJobs() returns. Never copied or freed by this struct
// itself - see JobDispatch.h's own "payload lifetime is the caller's
// responsibility" convention.
struct SkinningBatchContext {
    const std::vector<Vec3>* bindPositions;
    const std::vector<Vec3>* bindNormals;
    const std::vector<VertexSkinWeights>* skinWeights;
    const std::vector<Mat4>* skinningMatrices;
    std::vector<Vec3>* outPositions;
    std::vector<Vec3>* outNormals;
};

// The job-body trampoline gte::Jobs::Dispatch() actually schedules - skins
// exactly this batch's own [beginIndex, endIndex) slice of vertices via
// SkinVertexRange() (src/Animation/VertexSkinning.h), writing into this
// batch's own disjoint slice of the shared output vectors. Reads shared,
// read-only input data (bindPositions/bindNormals/skinWeights/
// skinningMatrices) - safe for any number of concurrent batches to read at
// once, per Phase 4's own thread-safety audit of the pure Animation/
// modules (see AGENTS.md, "Job System").
void RunSkinningBatch(std::uint32_t beginIndex, std::uint32_t endIndex, void* payload)
{
    // The ONE sanctioned way to profile code running inside a job body -
    // see AGENTS.md, "Job System" (Phase 5) - never GTE_PROFILE_SCOPE here.
    GTE_PROFILE_JOB_SCOPE("SkinVertices");

    SkinningBatchContext* context = static_cast<SkinningBatchContext*>(payload);
    SkinVertexRange(beginIndex, endIndex, *context->bindPositions, *context->bindNormals, *context->skinWeights,
        *context->skinningMatrices, *context->outPositions, *context->outNormals);
}

// Multithreaded CPU-skinning optimization, Stage 2 (parallelize the
// PACKING step too - see
// task_manager/optimizing_multi_thread_cpu_skinning/
// MULTITHREAD_CPU_SKINNING_OPTIMIZATION_STRATEGY_v1.md): mirrors
// SkinningBatchContext/RunSkinningBatch above exactly, but for
// MeshVertexPacking.h's PackMeshVertexRange()/PackMeshVertexUvRange()
// instead of SkinVertexRange(). Packing is just as embarrassingly parallel
// per-vertex as the skin blend it now runs alongside (via its own,
// separate Dispatch()+WaitForJobs() bracket - see RunPendingGroups() below)
// - previously this pack step ran single-threaded, on the main thread,
// once per MATERIAL PART (i.e. up to partCount times for the same data);
// Stage 1 (the shared vertex buffer - see MeshAssetGpuCatalog.cpp) already
// collapses that down to once per DISTINCT underlying GPU vertex buffer,
// and this Stage 2 addition further moves that one remaining pass onto the
// worker pool.
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

// Phase 5 (GPU_SKINNING_PHASE5_RUNTIME_CPU_GPU_SWITCH_STRATEGY_v2.md) -
// keeps every MeshRenderer under `animatorEntity` (its direct children -
// a model's own submesh "parts" are always direct children of its root,
// see EntityInstantiator.cpp/MeshAssetGpuCatalog.cpp) pointed at whichever
// Mesh (CPU-skinned or GPU-skinned) matches `mode`. Idempotent and safe to
// call every frame regardless of whether `mode` actually changed since the
// last call - a MeshRenderer already pointing at the "right" handle for
// `mode` is simply left untouched (neither TryGetGpuMeshHandle() nor
// TryGetCpuMeshHandle() ever matches its OWN handle back to itself, so at
// most one of the two branches below ever does anything on a given frame).
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

    // Phase 1 (verlet-integration-7) - tracks which entities got a fresh,
    // genuinely-ANIMATED pose from the loop below this frame, so the second
    // loop (DynamicChainRig baseline pass, below) never clobbers it.
    std::unordered_set<Entity> animatedThisFrame;

    // No GPU/Renderer/Mesh state touched anywhere in this loop - safe to
    // reason about (and, per Phase 3's own "What We Will NOT Do", safe to
    // parallelize in a future phase) independently of SkinAndUpload()'s own
    // strictly-sequential shared-GPU-buffer constraint below.
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

        // Sample -> IK-solve -> append/grant-inherit, in that exact,
        // correctness-critical fixed order - see
        // Animation/AnimationPoseEvaluator.h's own file comment. This pure
        // math module is NOT touched by this refactor at all, regardless of
        // skinning mode - GPU mode still evaluates the pose entirely on the
        // CPU (see GPU_SKINNING_PHASE0_MASTER_STRATEGY_v2.md's own "What We
        // Will NOT Do": no GPU-side pose evaluation). Deliberately stops one
        // step short of ComputeSkinningMatrices() - see
        // EvaluateAnimatedPoseBeforePhysics()'s own doc comment - so
        // PhysicsSystem::Update() gets a genuine hook point between this and
        // SkinAndUpload().
        std::vector<BoneLocalOffset> pose = EvaluateAnimatedPoseBeforePhysics(skinData->skeleton, binding, animator.frame);

        ResolvedAnimationPose* resolvedPose = registry.TryGetComponent<ResolvedAnimationPose>(animatorEntity);
        if (resolvedPose == nullptr) {
            resolvedPose = &registry.AddComponent<ResolvedAnimationPose>(animatorEntity);
        }
        resolvedPose->pose = std::move(pose);
        animatedThisFrame.insert(animatorEntity);
    }

    // Phase 1 (verlet-integration-7 - PHASE1_BASELINE_RESOLVED_POSE_FOR_NONANIMATED_PHYSICS_RIGS.md):
    // second pass, narrow scope - only entities with an ENABLED
    // DynamicChainRig (per the user's own explicit "narrow" answer, see
    // PHASE0_MASTER_STRATEGY.md, Step 1) that did NOT already get a fresh,
    // genuinely-ANIMATED pose from the loop above this same call. This is
    // the ONLY thing that unblocks PhysicsSystem::Update() for a model that
    // has never had Game::PlayAnimationOnEntity() called on it at all - a
    // pure T-pose model - since PhysicsSystem::Update() itself unconditionally
    // skips any entity with no ResolvedAnimationPose component yet. The
    // baseline is always the model's own bind/T-pose (an all-default
    // BoneLocalOffset per bone - see Animation/SkeletonPose.h's own
    // documented convention), always a full fresh overwrite (never merged
    // with a previous frame's leftover value) - this runs BEFORE
    // PhysicsSystem::Update() every frame (see Game::Update()'s fixed call
    // order), so this is always exactly the "before physics touches it"
    // snapshot, whether the entity is animated or not. Touches no
    // Renderer/Mesh/GPU state, same as the loop above.
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

// Phase 2 (task_manager/verlet-integration-7/
// PHASE2_SKIN_AND_UPLOAD_VISIBILITY_FOR_PHYSICS_ONLY_ENTITIES.md) - the
// ENTIRE per-entity skin/pack/upload body SkinAndUpload() used to run
// inline for its own SkeletalAnimator loop, extracted verbatim (zero logic
// change - see AnimationSystem.h's own doc comment on this method). Called
// for an entity discovered via EITHER a playing SkeletalAnimator OR an
// enabled DynamicChainRig.
void AnimationSystem::SkinAndUploadOneEntity(
    Registry& registry, Entity entity, const std::string& meshGtaPath, SkinningMode mode)
{
    const SkinnedMeshData* skinData = m_rigCache.TryGet(meshGtaPath);
    if (skinData == nullptr) {
        return; // Its model's own skinning data isn't (or is no longer) cached - nothing to do.
    }

    // Reads whatever EvaluatePoses() (and, in between, PhysicsSystem::
    // Update()) left in ResolvedAnimationPose::pose this frame - this
    // method does not, and must not, care whether physics touched it.
    // An entity with none yet (EvaluatePoses() skipped it this frame -
    // e.g. it wasn't playing) is simply skipped here too, the identical
    // guard EvaluatePoses() itself applies.
    const ResolvedAnimationPose* resolvedPose = registry.TryGetComponent<ResolvedAnimationPose>(entity);
    if (resolvedPose == nullptr) {
        return;
    }
    const std::vector<Mat4> skinningMatrices = ComputeSkinningMatrices(skinData->skeleton, resolvedPose->pose);

    // Phase 5 - keep this model's own MeshRenderers pointed at whichever
    // Mesh (CPU or GPU) matches the CURRENT mode, regardless of which
    // branch below actually runs this frame - this is what makes a
    // mid-session mode switch take effect on the very next frame.
    const GpuSkinningRigCache::GpuModelEntry* gpuEntry = m_gpuRigCache.TryGet(meshGtaPath);
    if (gpuEntry != nullptr) {
        ApplyMeshHandleForSkinningMode(registry, entity, *gpuEntry, mode);
    }

    if (mode == SkinningMode::GpuCompute) {
        // Phase 5's ENTIRE per-frame CPU cost for this model: one
        // Buffer::Upload() call, main-thread-only, no Jobs::Dispatch()
        // involved at all - see this phase's own strategy document,
        // Step 3.2/"What We Will NOT Do". The real vkCmdDispatch is
        // recorded later, from src/Application/RenderPasses.cpp's
        // AddGpuSkinningPasses(), once CollectModelsNeedingGpuSkinningThisFrame()
        // is called after this whole SkinAndUpload() has returned.
        if (gpuEntry == nullptr) {
            return; // Never registered for GPU skinning (see Phase 4) - nothing to dispatch.
        }
        gpuEntry->boneMatricesBuffer.Upload(skinningMatrices.data(), skinningMatrices.size() * sizeof(Mat4));

        if (std::find(m_gpuModelsNeedingDispatchThisFrame.begin(), m_gpuModelsNeedingDispatchThisFrame.end(),
                meshGtaPath)
            == m_gpuModelsNeedingDispatchThisFrame.end()) {
            m_gpuModelsNeedingDispatchThisFrame.push_back(meshGtaPath);
        }
        return;
    }

    // --- CpuJobSystem mode: existing CPU skinning path, UNCHANGED. ---

    // Stage 3 (reuse scratch buffers across frames, per model - see
    // MULTITHREAD_CPU_SKINNING_OPTIMIZATION_STRATEGY_v1.md): owned by
    // this AnimationSystem instance, keyed by mesh path, instead of a
    // fresh std::vector allocated on every SkinAndUpload() call for
    // every entity. resize() is a no-op once a buffer's capacity
    // already covers `vertexCount`, which holds true for every frame
    // after the first (a model's own vertex count never changes after
    // load).
    const std::size_t vertexCount = skinData->bindPositions.size();
    AnimatorScratchBuffers& scratch = m_scratchBuffers[meshGtaPath];
    std::vector<Vec3>& skinnedPositions = scratch.skinnedPositions;
    std::vector<Vec3>& skinnedNormals = scratch.skinnedNormals;
    skinnedPositions.resize(vertexCount);
    skinnedNormals.resize(vertexCount);

    // Job System Phase 6: CPU vertex skinning itself - dispatched across
    // the worker pool for a model with enough vertices to be worth it,
    // otherwise run inline. Either way, `skinnedPositions`/
    // `skinnedNormals` hold the exact same values SkinVertices() alone
    // would have produced (see tests/Animation/VertexSkinningParityTests.cpp) -
    // this is purely a "where/how" change, never a "what" change.
    if (vertexCount < kMinVerticesToParallelize) {
        SkinVertexRange(0, static_cast<std::uint32_t>(vertexCount), skinData->bindPositions,
            skinData->bindNormals, skinData->skinWeights, skinningMatrices, skinnedPositions, skinnedNormals);
    } else {
        SkinningBatchContext context{ &skinData->bindPositions, &skinData->bindNormals, &skinData->skinWeights,
            &skinningMatrices, &skinnedPositions, &skinnedNormals };
        Jobs::JobHandle skinningHandle;
        Jobs::Dispatch(&RunSkinningBatch, static_cast<std::uint32_t>(vertexCount), &context, skinningHandle,
            kMinVerticesPerBatch);
        // Exactly ONE wait, for THIS ONE model's entire skinning
        // dispatch, before this call's packing/GPU-upload work below
        // runs - see SkinAndUpload()'s own header comment on why the
        // NEXT entity's own Dispatch() must never begin before this
        // WaitForJobs() call returns.
        Jobs::JobSystem::Instance().WaitForJobs(skinningHandle);
    }

    const std::vector<MeshAssetPart>* parts = m_meshInstantiationSystem.TryGetMeshAssetParts(meshGtaPath);
    if (parts == nullptr) {
        return;
    }

    // Multithreaded CPU-skinning optimization, Stage 1 (see
    // MULTITHREAD_CPU_SKINNING_OPTIMIZATION_STRATEGY_v1.md): several of
    // this model's own MeshAssetParts may now point at the exact SAME
    // underlying GPU vertex buffer (e.g. every textured-material
    // submesh, built via Renderer::CreateMeshFromSharedVertexBuffer() -
    // see MeshAssetGpuCatalog.cpp) - previously every part re-packed
    // and re-uploaded a FULL copy of the whole model's vertex data,
    // unconditionally, turning this loop's true cost into
    // O(vertexCount x partCount). Group parts by their Mesh's own
    // VertexBufferIdentity() first (via the SHARED
    // GroupMeshAssetPartsBySharedVertexBuffer() helper - GPU Vertex
    // Skinning campaign, Phase 4, also used by GpuSkinningRigCache - see
    // MeshAssetPartGrouping.h), so each DISTINCT underlying buffer is
    // packed/uploaded exactly ONCE per frame, no matter how many parts
    // reference it.
    const std::vector<MeshAssetPartGroup> groups = GroupMeshAssetPartsBySharedVertexBuffer(m_renderSystem, *parts);

    // For each distinct vertex buffer: pack (Stage 2 - parallelized via
    // the worker pool exactly like the skin blend above, for a model
    // large enough for it to be worth it) directly from this frame's
    // freshly-skinned positions/normals into a reused scratch vector
    // (Stage 3), then upload it ONCE (Stage 1) - this GPU upload step
    // stays main-thread-only, unconditionally, exactly matching
    // AGENTS.md's Job System Phase 4 audit table's `Renderer`/`Mesh`
    // row (NEVER for a job body to touch).
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

    // Phase 5 - snapshotted ONCE, at the very top, so one model's entire
    // per-frame processing below is never torn between two different modes
    // mid-iteration - see this phase's own strategy document, Step 3.4.
    const SkinningMode mode = m_mode;
    m_gpuModelsNeedingDispatchThisFrame.clear();

    // Job System Phase 6 (First Production Consumer - see
    // task_manager/job_system/JOBSYSTEM_PHASE6_FIRST_PRODUCTION_CONSUMER_ANIMATION_SKINNING_v2.md,
    // Section 3.6, and JOB_SYSTEM_PHASE6_COMPLETION_REPORT.md), extended by
    // Phase 2 of task_manager/verlet-integration-7 to cover a second entity
    // source (below):
    //
    // *** THIS OUTER PROCESSING MUST REMAIN STRICTLY SEQUENTIAL, ONE MODEL
    // AT A TIME, REGARDLESS OF WHICH SOURCE DISCOVERED IT - NEVER
    // "HELPFULLY" RESTRUCTURED TO FIRE OFF EVERY ENTITY'S OWN Dispatch()
    // CALL UP FRONT AND WAIT ON ALL OF THEM TOGETHER. ***
    //
    // Two entities spawned from the SAME *.gta file share one underlying
    // Mesh (see README.md's own documented limitation, "A spawned MMD
    // model can now actually be ANIMATED..."), including the very GPU
    // vertex buffer(s) SkinAndUploadOneEntity() is about to upload into.
    // Today that sharing is safe ONLY because this loop processes one
    // entity's ENTIRE per-model sequence (every part's skinning + GPU
    // upload) to full completion before the next entity's own sequence
    // begins - at any given instant, at most one entity is ever touching
    // that shared memory. Overlapping two entities' own Dispatch()/
    // WaitForJobs() work on the worker pool at the same time would turn
    // this into a genuine, unsynchronized DATA RACE on that shared buffer,
    // not merely today's harmless "last write wins" visual bug. This rule
    // may only be lifted once every spawned model instance owns its own
    // private GPU mesh buffers - a separate, unstarted piece of engine work
    // (see README.md/TODO.md). GPU mode is no exception to this rule
    // either - see GPU_SKINNING_PHASE5_RUNTIME_CPU_GPU_SWITCH_STRATEGY_v2.md.
    // (Phase 3, verlet-integration-1: this constraint applies ONLY to this
    // method now - EvaluatePoses() above touches no Renderer/Mesh state at
    // all, so it is not bound by it - see this method's own header comment,
    // AnimationSystem.h.)

    std::unordered_set<Entity> processedThisFrame;

    // Source 1 - every actively-playing SkeletalAnimator, EXACT existing
    // order/logic, now delegated to SkinAndUploadOneEntity().
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

    // Source 2 (task_manager/verlet-integration-7, Phase 2 -
    // PHASE2_SKIN_AND_UPLOAD_VISIBILITY_FOR_PHYSICS_ONLY_ENTITIES.md) -
    // every enabled DynamicChainRig entity Phase 1's EvaluatePoses()
    // guaranteed a fresh ResolvedAnimationPose for THIS SAME frame, that
    // Source 1 above did NOT already process (an entity may legitimately
    // carry BOTH components - it must be skinned/uploaded exactly ONCE per
    // frame, never twice, which would double-upload the same shared GPU
    // vertex buffer).
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
            continue; // Phase 1 didn't (or couldn't - e.g. unregistered model) produce one this frame.
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
            if (!group.outputVertexBuffer) {
                continue;
            }

            GpuSkinningDispatchRequest request;
            request.name = group.debugName.c_str();
            request.outputBuffer = group.outputVertexBuffer->Native();
            request.outputBufferSize = group.outputVertexBuffer->Size();
            request.descriptorSet = group.descriptorSet.Native();
            request.vertexCount = group.vertexCount;
            request.textured = group.isTextured;
            requests.push_back(request);
        }
    }

    return requests;
}

} // namespace gte
