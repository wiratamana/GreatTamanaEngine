#include "PhysicsSystem.h"

#include "../../Animation/BoneWorldMatrixQuery.h"
#include "../../ECS/Components/DynamicChainRig.h"
#include "../../ECS/Components/ResolvedAnimationPose.h"
#include "../../Jobs/JobDispatch.h"
#include "../../Jobs/JobSystem.h"
#include "../../Physics/BoneChainPhysicsResolver.h"
#include "../../Physics/DynamicChainDetection.h"
#include "../../Physics/DynamicChainSolver.h"
#include "../../Physics/FixedTimestepAccumulator.h"
#include "../../Physics/SphereCollider.h"
#include "../../Profiling/JobScopeTimer.h"
#include "../../Profiling/ScopeTimer.h"
#include "../Animation/SkeletalRigCache.h"

#include <cstdint>

namespace gte {

namespace {

// PHASE5 (task_manager/verlet-integration-1/
// PHASE5_COLLISION_STABILITY_AND_PERFORMANCE_HARDENING.md, 3.4) - below this
// TOTAL joint count (summed across every chain a single entity carries),
// stepping every chain runs inline, serially, on the calling (main) thread -
// scheduling a gte::Jobs::Dispatch() for a handful of joints would spend
// more time on the Job System's own per-Dispatch() scheduling overhead than
// the actual work itself, mirroring AnimationSystem.cpp's own
// kMinVerticesToParallelize precedent exactly (see AGENTS.md, "Job System").
constexpr std::size_t kMinDynamicJointsToParallelize = 24;

// The per-batch job context handed through gte::Jobs::Dispatch()'s opaque
// payload pointer. One "item" here is one WHOLE CHAIN (not one joint/vertex,
// unlike AnimationSystem.cpp's own vertex-skinning batches) - a batch job
// body (RunDynamicChainBatch, below) steps every chain in its own
// [beginIndex, endIndex) range to full completion (every fixed substep) via
// the exact same StepDynamicChainRange() helper the serial (non-Dispatch)
// path below also calls directly - there is exactly ONE copy of the actual
// per-chain stepping logic, never two independently-maintained copies, which
// is also what guarantees the serial and parallel paths produce
// byte-identical results (see tests/Game/Physics/PhysicsSystemParallelTests.cpp).
//
// `pose` is the SHARED ResolvedAnimationPose::pose vector, mutated in place
// by every chain's own ApplyDynamicChainPhysicsToPose() call - safe for
// several batches to write CONCURRENTLY only because Phase 4's
// DetectDynamicChains() guarantees every two chains' own jointBoneIndices
// sets are DISJOINT (see AGENTS.md, "Job System", this phase's own added
// rows) - `pose`'s SIZE must never change for the duration of this
// Dispatch()/WaitForJobs() bracket (no push_back/resize inside a job body).
struct DynamicChainBatchContext {
    const SkeletonData* skeleton;
    const std::vector<DynamicChainDefinition>* chains;
    std::vector<DynamicChainRuntimeState>* chainStates;
    std::vector<BoneLocalOffset>* pose;
    int stepCount;
    float fixedTimestepSeconds;
    Vec3 gravity;
    WindSettings wind;
};

// Steps every chain in `[beginIndex, endIndex)` of `context` to full
// completion (all of that entity's `stepCount` fixed substeps) - used
// DIRECTLY (never through gte::Jobs::Dispatch()) for the serial path, and as
// the batch-job trampoline body for the parallel path (see
// RunDynamicChainBatchJob() below) - one function, two call sites, by
// design.
void StepDynamicChainRange(std::uint32_t beginIndex, std::uint32_t endIndex, DynamicChainBatchContext& context)
{
    for (std::uint32_t chainIndex = beginIndex; chainIndex < endIndex; ++chainIndex) {
        const DynamicChainDefinition& chain = (*context.chains)[chainIndex];
        DynamicChainRuntimeState& state = (*context.chainStates)[chainIndex];

        // Captured ONCE, from `pose` EXACTLY as EvaluatePoses() (plus
        // whatever earlier chains in THIS SAME batch/frame already wrote)
        // left it, BEFORE any substep below mutates `pose` in place - see
        // PHASE0's Revision Notes finding #4. Never re-read inside the
        // substep loop.
        const Mat4 rootWorld = ComputeBoneWorldMatrix(*context.skeleton, *context.pose, chain.rootBoneIndex);
        const Vec3 rootWorldPos = rootWorld.TransformPoint(Vec3::Zero());

        std::vector<Vec3> animatedJointWorldPositions;
        animatedJointWorldPositions.reserve(chain.jointBoneIndices.size());
        for (std::int32_t boneIndex : chain.jointBoneIndices) {
            animatedJointWorldPositions.push_back(
                ComputeBoneWorldMatrix(*context.skeleton, *context.pose, boneIndex).TransformPoint(Vec3::Zero()));
        }

        // PHASE5, 3.2 - resolve this chain's own collision sphere (if any),
        // once, from the SAME pure-FK snapshot as the root/joint targets
        // above - never recomputed per substep.
        SphereCollider collider;
        bool hasCollider = false;
        if (chain.hasHeadCollider) {
            collider.center = ComputeBoneWorldMatrix(*context.skeleton, *context.pose, chain.headColliderBoneIndex)
                                   .TransformPoint(Vec3::Zero());
            collider.radius = chain.headColliderRadius;
            hasCollider = true;
        }

        for (int step = 0; step < context.stepCount; ++step) {
            StepDynamicChain(chain, rootWorldPos, animatedJointWorldPositions, state, context.fixedTimestepSeconds,
                context.gravity, context.wind, hasCollider ? &collider : nullptr);

            std::vector<Vec3> simulatedPositions;
            simulatedPositions.reserve(state.particles.size());
            for (const VerletParticle& particle : state.particles) {
                simulatedPositions.push_back(particle.position);
            }
            ApplyDynamicChainPhysicsToPose(*context.skeleton, chain, simulatedPositions, *context.pose);
        }
    }
}

// The job-body trampoline gte::Jobs::Dispatch() actually schedules - mirrors
// AnimationSystem.cpp's own SkinningBatchContext/RunSkinningBatch() pattern
// exactly (see AGENTS.md, "Job System", Phase 6), just for whole chains
// instead of vertex ranges.
void RunDynamicChainBatchJob(std::uint32_t beginIndex, std::uint32_t endIndex, void* payload)
{
    // The ONE sanctioned way to profile code running inside a job body - see
    // AGENTS.md, "Job System" (Phase 5) - never GTE_PROFILE_SCOPE here.
    GTE_PROFILE_JOB_SCOPE("StepDynamicChain");
    DynamicChainBatchContext* context = static_cast<DynamicChainBatchContext*>(payload);
    StepDynamicChainRange(beginIndex, endIndex, *context);
}

} // namespace

void PhysicsSystem::RegisterDynamicChains(const std::string& absoluteGtaPath, const SkinnedMeshData& data)
{
    // PHASE4 (task_manager/verlet-integration-1/
    // PHASE4_PARAMETER_AUTHORING_AND_DATA_DRIVEN_CONFIG.md) - real chain
    // auto-detection, derived purely from already-imported PMX data
    // (Bone::deformAfterPhysics / RigidBody::motionType) - no new asset
    // format, no authoring UI required to get a first working result. An
    // Editor override of DynamicChainDetectionDefaults may land later; pure
    // defaults are used for every model today.
    const DynamicChainDetectionDefaults defaults{};
    std::vector<DynamicChainDefinition> chains
        = DetectDynamicChains(data.skeleton, data.physics.has_value() ? &*data.physics : nullptr, defaults);

    DynamicChainRigCache::ModelEntry entry;
    entry.chains = std::move(chains);
    entry.skeleton = data.skeleton; // A private COPY - see DynamicChainRigCache.h's own file comment.
    m_rigCache.Register(absoluteGtaPath, std::move(entry));
}

void PhysicsSystem::AttachDynamicChainRigIfNeeded(Registry& registry, Entity rootEntity, const std::string& absoluteGtaPath)
{
    const DynamicChainRigCache::ModelEntry* model = m_rigCache.TryGet(absoluteGtaPath);
    if (model == nullptr || model->chains.empty()) {
        return; // Nothing detected for this model - no DynamicChainRig needed.
    }

    DynamicChainRig& rig = registry.AddComponent<DynamicChainRig>(rootEntity);
    rig.meshGtaPath = absoluteGtaPath;
    rig.chainStates.resize(model->chains.size()); // one default-constructed DynamicChainRuntimeState per detected chain.
}

void PhysicsSystem::Update(Registry& registry, double deltaSeconds)
{
    GTE_PROFILE_SCOPE("PhysicsSystem::Update");

    // PHASE5, 3.4: *** THIS OUTER PER-ENTITY LOOP MUST REMAIN STRICTLY
    // SEQUENTIAL, ONE ENTITY AT A TIME. *** Unlike AnimationSystem::
    // SkinAndUpload()'s own identically-worded rule, this is NOT about a
    // shared GPU mesh buffer (PhysicsSystem never touches Renderer/Mesh/GPU
    // state at all) - it is because no two entities' own ResolvedAnimationPose
    // components ever alias the same memory, so cross-entity parallelism
    // is a genuinely open, unstarted follow-up (see this phase's own "What
    // We Will NOT Do"), not a correctness requirement of THIS loop today.
    ComponentStorage<DynamicChainRig>& rigs = registry.Storage<DynamicChainRig>();
    for (std::size_t i = 0; i < rigs.Size(); ++i) {
        DynamicChainRig& rig = rigs.ComponentAt(i);
        if (!rig.enabled) {
            continue;
        }
        const Entity entity = rigs.EntityAt(i);

        ResolvedAnimationPose* resolvedPose = registry.TryGetComponent<ResolvedAnimationPose>(entity);
        if (resolvedPose == nullptr) {
            continue; // Nothing to overwrite - AnimationSystem::EvaluatePoses() hasn't produced a pose for this entity this frame.
        }

        const DynamicChainRigCache::ModelEntry* model = m_rigCache.TryGet(rig.meshGtaPath);
        if (model == nullptr || model->chains.size() != rig.chainStates.size()) {
            continue; // Not (yet) registered, or stale - degrade gracefully.
        }

        const int stepCount = ComputeFixedStepCount(rig.accumulatedSeconds, static_cast<float>(deltaSeconds),
            m_globalSettings.fixedTimestepSeconds, m_globalSettings.maxStepsPerFrame);
        if (stepCount <= 0) {
            continue;
        }

        DynamicChainBatchContext context{ &model->skeleton, &model->chains, &rig.chainStates, &resolvedPose->pose,
            stepCount, m_globalSettings.fixedTimestepSeconds, m_globalSettings.gravity, m_globalSettings.wind };

        // PHASE5, 3.4 - parallelize INDEPENDENT chains WITHIN this one
        // entity's own physics step, across the Job System's worker pool,
        // once there are enough TOTAL joints across all of this entity's
        // chains to be worth the Dispatch() overhead. One "item" is one
        // WHOLE CHAIN - gte::Jobs::Dispatch() itself derives the batch count/
        // split automatically from JobSystem::Instance().WorkerCount(), the
        // same "never hand-compute a batch split" convention
        // AnimationSystem.cpp's own vertex-skinning dispatch already uses.
        std::size_t totalJoints = 0;
        for (const DynamicChainDefinition& chain : model->chains) {
            totalJoints += chain.jointBoneIndices.size();
        }

        if (totalJoints >= kMinDynamicJointsToParallelize && model->chains.size() > 1) {
            Jobs::JobHandle chainHandle;
            Jobs::Dispatch(&RunDynamicChainBatchJob, static_cast<std::uint32_t>(model->chains.size()), &context,
                chainHandle, /*minItemsPerBatch=*/1);
            // Exactly ONE wait, for THIS ONE entity's entire chain dispatch,
            // before the next entity's own processing begins - see this
            // method's own header comment on why the outer loop must stay
            // sequential.
            Jobs::JobSystem::Instance().WaitForJobs(chainHandle);
        } else {
            StepDynamicChainRange(0, static_cast<std::uint32_t>(model->chains.size()), context);
        }
    }
}

} // namespace gte
