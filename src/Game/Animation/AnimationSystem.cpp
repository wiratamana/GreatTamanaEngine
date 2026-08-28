#include "AnimationSystem.h"

#include "../../Animation/AnimationPoseEvaluator.h"
#include "../../Animation/VertexSkinning.h"
#include "../../ECS/Components/MeshAssetSource.h"
#include "../../ECS/Components/SkeletalAnimator.h"
#include "../../Jobs/JobDispatch.h"
#include "../../Jobs/JobSystem.h"
#include "../../Profiling/JobScopeTimer.h"
#include "../../Profiling/ScopeTimer.h"
#include "../../Renderer/Mesh.h"
#include "../../Renderer/MeshVertex.h"
#include "../Instantiation/MeshInstantiationSystem.h"
#include "../Instantiation/MeshVertexPacking.h"
#include "../RenderSystem.h"

#include <cmath>
#include <cstdint>

namespace gte {

namespace {

// VMD's own fixed frame grid - see Assets/MotionData.h's own file comment.
constexpr float kVmdFramesPerSecond = 30.0f;

// Job System Phase 6 (First Production Consumer - Animation / Vertex
// Skinning - see AGENTS.md, "Job System", and
// task_manager/job_system/JOB_SYSTEM_PHASE6_COMPLETION_REPORT.md): below
// this vertex count, CPU vertex skinning runs inline, serially, on the
// calling (main) thread - scheduling a gte::Jobs::Dispatch() for a
// genuinely tiny model would spend more time on the Job System's own
// per-Dispatch() scheduling overhead than the actual skinning work itself
// (see JOBSYSTEM_PHASE6_FIRST_PRODUCTION_CONSUMER_ANIMATION_SKINNING_v2.md,
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

void AnimationSystem::Update(Registry& registry, double deltaSeconds)
{
    GTE_PROFILE_SCOPE("AnimationSystem::Update");

    ComponentStorage<SkeletalAnimator>& animators = registry.Storage<SkeletalAnimator>();

    // Job System Phase 6 (First Production Consumer - see
    // task_manager/job_system/JOBSYSTEM_PHASE6_FIRST_PRODUCTION_CONSUMER_ANIMATION_SKINNING_v2.md,
    // Section 3.6, and JOB_SYSTEM_PHASE6_COMPLETION_REPORT.md):
    //
    // *** THIS OUTER LOOP MUST REMAIN STRICTLY SEQUENTIAL, ONE ANIMATOR AT
    // A TIME - NEVER "HELPFULLY" RESTRUCTURED TO FIRE OFF EVERY ANIMATOR'S
    // OWN Dispatch() CALL UP FRONT AND WAIT ON ALL OF THEM TOGETHER. ***
    //
    // Two entities spawned from the SAME *.gta file share one underlying
    // Mesh (see README.md's own documented limitation, "A spawned MMD
    // model can now actually be ANIMATED..."), including the very GPU
    // vertex buffer(s) this loop's own skinned positions/normals are about
    // to be uploaded into. Today that sharing is safe ONLY because this
    // loop processes one animator's ENTIRE per-model sequence (every part's
    // skinning + GPU upload) to full completion before the next animator's
    // own sequence begins - at any given instant, at most one animator is
    // ever touching that shared memory. Overlapping two animators' own
    // Dispatch()/WaitForJobs() work on the worker pool at the same time
    // would turn this into a genuine, unsynchronized DATA RACE on that
    // shared buffer, not merely today's harmless "last write wins" visual
    // bug. This rule may only be lifted once every spawned model instance
    // owns its own private GPU mesh buffers - a separate, unstarted piece
    // of engine work (see README.md/TODO.md).
    for (std::size_t i = 0; i < animators.Size(); ++i) {
        SkeletalAnimator& animator = animators.ComponentAt(i);
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

        // Sample -> IK-solve -> append/grant-inherit -> forward-kinematics,
        // in that exact, correctness-critical fixed order - see
        // Animation/AnimationPoseEvaluator.h's own file comment. This pure
        // math module is NOT touched by this refactor at all.
        const std::vector<Mat4> skinningMatrices =
            EvaluateAnimatedSkinningPose(skinData->skeleton, binding, animator.frame);

        // Job System Phase 6: CPU vertex skinning itself - dispatched across
        // the worker pool for a model with enough vertices to be worth it,
        // otherwise run inline. Either way, `skinnedPositions`/
        // `skinnedNormals` hold the exact same values SkinVertices() alone
        // would have produced (see tests/Animation/VertexSkinningParityTests.cpp) -
        // this is purely a "where/how" change, never a "what" change.
        const std::size_t vertexCount = skinData->bindPositions.size();
        std::vector<Vec3> skinnedPositions(vertexCount);
        std::vector<Vec3> skinnedNormals(vertexCount);

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
            // dispatch, before this loop iteration's GPU upload below runs -
            // see this function's own header comment on why the NEXT
            // animator's own Dispatch() must never begin before this
            // WaitForJobs() call returns.
            Jobs::JobSystem::Instance().WaitForJobs(skinningHandle);
        }

        const std::vector<MeshAssetPart>* parts = m_meshInstantiationSystem.TryGetMeshAssetParts(animator.meshGtaPath);
        if (parts == nullptr) {
            continue;
        }

        // Re-upload EVERY one of this model's mesh parts - each part's own
        // GPU vertex buffer holds a full copy of the whole model's vertex
        // data, so all of them need the same freshly-skinned positions/
        // normals, just reformatted per part's own vertex layout via the
        // SHARED MeshVertexPacking helpers (the exact same functions
        // MeshAssetGpuCatalog used at initial load time) instead of the two
        // duplicated inline loops this used to be. This GPU upload step
        // stays main-thread-only, unconditionally - see AGENTS.md's Job
        // System Phase 4 audit table's `Renderer`/`Mesh` row (NEVER for a
        // job body to touch).
        for (const MeshAssetPart& part : *parts) {
            Mesh* gpuMesh = m_renderSystem.TryGetMesh(part.mesh);
            if (gpuMesh == nullptr) {
                continue;
            }

            if (part.texture.IsValid()) {
                const std::vector<MeshVertexUv> vertices =
                    PackMeshVertexUvs(skinnedPositions, skinnedNormals, skinData->uvs);
                gpuMesh->UpdateVertexData(vertices.data(), vertices.size() * sizeof(MeshVertexUv));
            } else {
                const std::vector<MeshVertex> vertices = PackMeshVertices(skinnedPositions, skinnedNormals);
                gpuMesh->UpdateVertexData(vertices.data(), vertices.size() * sizeof(MeshVertex));
            }
        }
    }
}

} // namespace gte
