#include "PhysicsSystem.h"

#include "../../Animation/BoneWorldMatrixQuery.h"
#include "../../ECS/Components/DynamicChainRig.h"
#include "../../ECS/Components/ResolvedAnimationPose.h"
#include "../../ECS/Components/Transform.h"
#include "../../ECS/TransformHierarchy.h"
#include "../../Jobs/JobDispatch.h"
#include "../../Jobs/JobSystem.h"
#include "../../Physics/BoneChainPhysicsResolver.h"
#include "../../Physics/DynamicChainDetection.h"
#include "../../Physics/DynamicChainSolver.h"
#include "../../Physics/FixedTimestepAccumulator.h"
#include "../../Physics/Collider.h"
#include "../../Physics/ModelColliderDetection.h"
#include "../../Profiling/JobScopeTimer.h"
#include "../../Profiling/ScopeTimer.h"
#include "../Animation/SkeletalRigCache.h"

#include <algorithm>
#include <cassert>
#include <cstdint>
#include <unordered_set>

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

// task_manager/verlet-integration-9, PHASE4 - Assets::RigidBodyShape and
// Physics::ColliderShape intentionally share the same three enumerators in
// the same order (both written by this same campaign) - this explicit
// mapping is preferred over a raw static_cast so a future reordering/
// extension of either enum can never silently miscompute here.
ColliderShape ToColliderShape(RigidBodyShape shape) noexcept
{
    switch (shape) {
    case RigidBodyShape::Sphere: return ColliderShape::Sphere;
    case RigidBodyShape::Box: return ColliderShape::Box;
    case RigidBodyShape::Capsule: return ColliderShape::Capsule;
    }
    return ColliderShape::Sphere;
}

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

    // task_manager/verlet-integration-7, Phase 3 (v2) - the owning entity's
    // REAL, fully-resolved world POSITION and ROTATION ONLY (scale
    // deliberately excluded - see PHASE0_MASTER_STRATEGY.md's Revision Notes
    // (v2), Finding #1, and PhysicsSystem::Update()'s own header comment
    // where this is resolved). Read-only, resolved ONCE per entity per
    // frame, on the main thread, BEFORE any per-chain parallel dispatch -
    // shared by const reference by every chain in this one entity's own
    // batch, exactly like `skeleton`/`pose` already are.
    Mat4 entityWorldMatrix;
    // Inverse of the above - converts a simulated WORLD-space particle
    // position back into bone-local/model space before it's written into
    // `pose` via ApplyDynamicChainPhysicsToPose(), which only ever operates
    // in bone-local space.
    Mat4 entityWorldMatrixInverse;

    // task_manager/verlet-integration-7, Phase 4 (v2) - true while this
    // entity's own DynamicChainRig::frozen is set. Consulted only by
    // StepDynamicChainRange() below to skip the integration loop while
    // still unconditionally reapplying state.particles into `pose` -
    // see that function's own comment for why this must remain a separate
    // guard rather than relying solely on `stepCount == 0`.
    bool frozen;

    // task_manager/verlet-integration-9, PHASE4 - every collider this
    // entity's model has, already resolved to WORLD space THIS frame (see
    // PhysicsSystem::Update()'s own construction of this list, right
    // before this context is built) - shared, read-only, by every chain in
    // this entity's own batch, exactly like `skeleton`/`pose` already are.
    // Empty whenever no chain in this entity wants collision at all (see
    // the `anyChainWantsCollision` guard where this is built) - passing an
    // empty list is behaviorally identical to every chain treating
    // collision as disabled, matching StepDynamicChain()'s own documented
    // "empty colliders list is a no-op" contract regardless of
    // collisionEnabled.
    const std::vector<Collider>* resolvedColliders;
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
        // substep loop. task_manager/verlet-integration-7, Phase 3 - each
        // bone-local matrix is now composed with `context.entityWorldMatrix`
        // BEFORE extracting a position, so the Verlet solver genuinely
        // integrates in true (scale-free) world space instead of blind
        // bone-local "model space" - this is what makes dragging/rotating
        // the owning entity's Transform produce real inertial lag.
        const Mat4 rootWorld
            = context.entityWorldMatrix * ComputeBoneWorldMatrix(*context.skeleton, *context.pose, chain.rootBoneIndex);
        const Vec3 rootWorldPos = rootWorld.TransformPoint(Vec3::Zero());

        std::vector<Vec3> animatedJointWorldPositions;
        animatedJointWorldPositions.reserve(chain.jointBoneIndices.size());
        for (std::int32_t boneIndex : chain.jointBoneIndices) {
            const Mat4 jointWorld
                = context.entityWorldMatrix * ComputeBoneWorldMatrix(*context.skeleton, *context.pose, boneIndex);
            animatedJointWorldPositions.push_back(jointWorld.TransformPoint(Vec3::Zero()));
        }

        // task_manager/verlet-integration-9, PHASE4 - the shared, already
        // WORLD-space-resolved collider list for this entire entity (resolved
        // ONCE per entity per frame by PhysicsSystem::Update(), never here) -
        // passed uniformly to every chain regardless of that chain's own
        // collisionEnabled; StepDynamicChain()'s own internal
        // `if (definition.collisionEnabled)` guard decides per-chain whether
        // it is actually used.

        // task_manager/verlet-integration-7, Phase 4 (v2) - take zero NEW
        // integration steps whenever context.frozen is true, regardless of
        // whatever context.stepCount happens to hold (PhysicsSystem::Update()
        // itself already forces stepCount to 0 while frozen - see this
        // file's own Update() comment - but this explicit guard means a
        // future change to that invariant can never silently make a frozen
        // rig integrate again without also revisiting this guard).
        if (!context.frozen) {
            for (int step = 0; step < context.stepCount; ++step) {
                StepDynamicChain(chain, rootWorldPos, animatedJointWorldPositions, state, context.fixedTimestepSeconds,
                    context.gravity, context.wind, *context.resolvedColliders);
            }
        }

        // task_manager/verlet-integration-7, Phase 4 (v2, Culprit F fix) -
        // ALWAYS re-apply whatever `state.particles` currently holds into
        // `pose`, EVERY call, regardless of whether any new integration step
        // ran just above - this is what stops EvaluatePoses()'s own
        // unconditional every-frame pose overwrite (Phase 1's bind pose, or
        // a fresh FK sample) from ever being visible, even for one single
        // rendered frame, once a chain has simulated at least once.
        // `state.initialized` guards the ONLY case with nothing meaningful
        // to reapply yet: a chain that has never once called
        // StepDynamicChain() (e.g. spawned already-frozen, before its very
        // first step) - in that case `pose` is correctly left exactly as
        // EvaluatePoses() wrote it (its own bind/FK value), which is the
        // right "nothing to show yet" behavior.
        if (state.initialized) {
            std::vector<Vec3> simulatedPositions;
            simulatedPositions.reserve(state.particles.size());
            for (const VerletParticle& particle : state.particles) {
                simulatedPositions.push_back(context.entityWorldMatrixInverse.TransformPoint(particle.position));
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
    // task_manager/verlet-integration-6 (PHASE0_MASTER_STRATEGY.md) - real
    // chain auto-detection by traversing the model's own RigidBody/Joint
    // graph (RigidBodyMotionType::Static bodies anchor a chain, Dynamic/
    // DynamicAndBoneMerge bodies reachable from one become its simulated
    // joints), combined with the skeleton's own real bone ancestry to build
    // each chain's tree - REPLACES the previous Bone::deformAfterPhysics-only
    // algorithm entirely. No new asset format, no authoring UI required to
    // get a first working result. An Editor override of
    // DynamicChainDetectionDefaults may land later; pure defaults are used
    // for every model today.
    const DynamicChainDetectionDefaults defaults{};
    DynamicChainDetectionResult detection
        = DetectDynamicChains(data.skeleton, data.physics.has_value() ? &*data.physics : nullptr, defaults);

    // task_manager/verlet-integration-9, PHASE3 - independent of chain
    // detection above (see DetectModelColliders()'s own doc comment for
    // why): every Static rigid body of any shape becomes a collision
    // obstacle candidate for every chain belonging to this same model, once
    // opted in per-chain (DynamicChainDefinition::collisionEnabled, PHASE2).
    std::vector<ModelColliderDefinition> colliders
        = DetectModelColliders(data.skeleton, data.physics.has_value() ? &*data.physics : nullptr);

#ifndef NDEBUG
    // task_manager/verlet-integration-6, Phase 4 - defensive re-verification
    // of the disjoint-jointBoneIndices invariant PhysicsSystem::Update()'s
    // own parallel dispatch path relies on (see this file's own comment
    // above DynamicChainBatchContext) - Phase 3's construction is SUPPOSED
    // to guarantee this by construction (every participating bone is
    // assigned to exactly one chain), but this is cheap, debug-only
    // insurance against a future regression silently corrupting a shared
    // pose buffer under the parallel path instead of failing loudly here.
    {
        std::unordered_set<std::int32_t> seenBoneIndices;
        for (const DynamicChainDefinition& chain : detection.chains) {
            for (std::int32_t boneIndex : chain.jointBoneIndices) {
                assert(seenBoneIndices.insert(boneIndex).second
                    && "DetectDynamicChains() produced two chains sharing a bone index - parallel dispatch is unsafe.");
            }
        }
    }
#endif

    DynamicChainRigCache::ModelEntry entry;
    entry.chains = std::move(detection.chains);
    entry.skeleton = data.skeleton; // A private COPY - see DynamicChainRigCache.h's own file comment.
    entry.diagnostics = std::move(detection.diagnostics);
    entry.colliders = std::move(colliders);
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

        // task_manager/verlet-integration-7, Phase 4 (v2) - `stepCount`
        // legitimately stays 0 in TWO distinct situations: this rig is
        // explicitly `frozen` (Culprit D), or it is simply not yet time for
        // a new fixed step this frame (the ordinary, expected accumulator-
        // pattern outcome on most frames at any render rate above ~60 Hz).
        // NEITHER case may `continue` out of this loop - doing so would
        // leave whatever physics-blind pose EvaluatePoses() just wrote this
        // frame (Phase 1's bind pose, or a fresh FK sample) completely
        // unconverted, visibly "popping" every chain-controlled bone back to
        // its un-simulated value for this one render frame (Culprit F - see
        // PHASE0_MASTER_STRATEGY.md's Revision Notes, Finding #2). Every rig
        // that reaches this point (enabled, has a resolved pose, has a
        // registered model) ALWAYS falls through into
        // StepDynamicChainRange()/the dispatch path below, every single
        // frame - that function itself decides internally whether to take
        // any NEW integration steps, but ALWAYS reapplies whatever
        // state.particles already holds once the chain has been initialized
        // at least once. Only genuinely unrecoverable conditions (disabled,
        // no pose yet, model not registered/stale) may still `continue`
        // above this point - never a merely-zero stepCount.
        int stepCount = 0;
        if (rig.frozen) {
            rig.accumulatedSeconds = 0.0f; // Never bank time while frozen - un-freezing later must not trigger a multi-step catch-up burst.
        } else {
            stepCount = ComputeFixedStepCount(rig.accumulatedSeconds, static_cast<float>(deltaSeconds),
                m_globalSettings.fixedTimestepSeconds, m_globalSettings.maxStepsPerFrame);
        }

        // task_manager/verlet-integration-7, Phase 3 (v2) - the owning
        // entity's REAL, fully-resolved world POSITION and ROTATION (walking
        // its whole ECS parent chain via ECS/TransformHierarchy.h - the SAME
        // underlying data RenderSystem::CollectRenderables() uses to place
        // the rendered mesh), deliberately EXCLUDING scale - see
        // PHASE0_MASTER_STRATEGY.md's Revision Notes (v2), Finding #1, for
        // the full rationale: every chain's own restLengths/
        // headColliderRadius/maxPlausibleRootDelta is precomputed once in
        // UNSCALED bind-pose units and shared by every entity spawned from
        // the same model path, so baking a per-instance Transform::scale
        // into the matrix the solver simulates in would desync the solver's
        // own authored rest data from the world distances it actually sees.
        // `scale` is still applied, correctly, entirely downstream and
        // untouched by this phase - see RenderSystem::CollectRenderables()'s
        // own unmodified full-TRS model matrix.
        //
        // Resolved ONCE per entity, per frame, here on the main thread
        // (never inside the per-chain parallel Dispatch() below) - every
        // chain this entity owns reads the SAME already-resolved matrix by
        // const reference, so this is exactly as safe under the existing
        // parallel-dispatch path as `pose`/`skeleton` already are.
        const Transform entityWorldTransform = ComputeWorldTransform(registry, entity);
        const Mat4 entityWorldMatrix = Mat4::TRS(entityWorldTransform.position, entityWorldTransform.rotation, Vec3::One());

        // A pure rotation+translation matrix (unit scale, and Mat4::FromQuat()
        // of a normalized quaternion is always orthonormal) is ALGEBRAICALLY
        // NEVER singular - TryInverse() here is expected to ALWAYS succeed
        // for every normal input. It is kept (rather than the asserting
        // Inverse()) purely as cheap, unconditional, debug-only-asserting
        // insurance against a theoretically-malformed (e.g. non-normalized)
        // input quaternion reaching this far - a case this codebase has no
        // evidence can actually happen today, so the Identity() fallback
        // below is genuinely last-resort/should-never-trigger territory.
        Mat4 entityWorldMatrixInverse;
        if (!entityWorldMatrix.TryInverse(entityWorldMatrixInverse)) {
            assert(false && "PhysicsSystem: entity world (rotation+translation) matrix was singular - "
                             "should be algebraically impossible; check for a non-normalized Transform::rotation.");
            entityWorldMatrixInverse = Mat4::Identity();
        }

        // task_manager/verlet-integration-9, PHASE4 - resolve every one of this
        // model's Static-rigid-body colliders (PHASE3) to its CURRENT
        // world-space center/rotation, ONCE per entity per frame, shared
        // read-only by every chain this entity owns (mirrors entityWorldMatrix/
        // skeleton/pose's own existing "resolved once on the main thread,
        // shared by every chain" pattern) - only bothered with at all when at
        // least one of this entity's chains actually opted in
        // (collisionEnabled), and the model has at least one collider, so an
        // entity with collision disabled everywhere (today's default for every
        // chain) pays zero extra ComputeBoneWorldMatrix() calls per frame.
        std::vector<Collider> resolvedColliders;
        const bool anyChainWantsCollision = std::any_of(model->chains.begin(), model->chains.end(),
            [](const DynamicChainDefinition& c) { return c.collisionEnabled; });
        if (anyChainWantsCollision && !model->colliders.empty()) {
            resolvedColliders.reserve(model->colliders.size());
            for (const ModelColliderDefinition& colliderDef : model->colliders) {
                const Mat4 boneWorld
                    = entityWorldMatrix * ComputeBoneWorldMatrix(model->skeleton, resolvedPose->pose, colliderDef.boneIndex);
                Collider collider;
                collider.shape = ToColliderShape(colliderDef.shape);
                collider.center = boneWorld.TransformPoint(colliderDef.localOffsetPosition);
                collider.rotation = Quat::FromMat4(boneWorld) * colliderDef.localOffsetRotation;
                collider.size = colliderDef.shapeSize;
                collider.group = colliderDef.group;
                collider.collisionMask = colliderDef.collisionMask;
                resolvedColliders.push_back(collider);
            }
        }

        DynamicChainBatchContext context{ &model->skeleton, &model->chains, &rig.chainStates, &resolvedPose->pose,
            stepCount, m_globalSettings.fixedTimestepSeconds, m_globalSettings.gravity, m_globalSettings.wind,
            entityWorldMatrix, entityWorldMatrixInverse, rig.frozen, &resolvedColliders };

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
