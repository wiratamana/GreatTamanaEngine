#include "PhysicsSystem.h"

#include "../../Animation/BoneWorldMatrixQuery.h"
#include "../../ECS/Components/DynamicChainRig.h"
#include "../../ECS/Components/ResolvedAnimationPose.h"
#include "../../ECS/Components/Transform.h"
#include "../../ECS/TransformHierarchy.h"
#include "../../Jobs/JobDispatch.h"
#include "../../Jobs/JobSystem.h"
#include "../../Math/MathTypes.h"
#include "../../Physics/BoneChainPhysicsResolver.h"
#include "../../Physics/DynamicChainDetection.h"
#include "../../Physics/FixedTimestepAccumulator.h"
#include "../../Physics/JointCollisionFit.h"
#include "../../Physics/JointPhysicsOverrideApplication.h"
#include "../../Physics/ModelColliderDetection.h"
#include "../../Physics/StepInterpolation.h"
#include "../../Profiling/JobScopeTimer.h"
#include "../../Profiling/ScopeTimer.h"
#include "../Animation/SkeletalRigCache.h"

#include <algorithm>
#include <cassert>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <deque>
#include <cstdio>
#include <cstring>
#include <unordered_set>

namespace gte {

namespace {

// Below this total joint count of one entity, chains step inline instead of on the job system.
constexpr std::size_t kMinDynamicJointsToParallelize = 24;

ColliderShape ToColliderShape(RigidBodyShape shape) noexcept
{
    switch (shape) {
    case RigidBodyShape::Sphere: return ColliderShape::Sphere;
    case RigidBodyShape::Box: return ColliderShape::Box;
    case RigidBodyShape::Capsule: return ColliderShape::Capsule;
    }
    return ColliderShape::Sphere;
}

// Shared by every chain job of one entity. All pointers outlive the dispatch and its wait.
// Chains write disjoint bones of `pose`, whose size must not change during the dispatch.
struct DynamicChainBatchContext {
    const SkeletonData* skeleton;
    const std::vector<DynamicChainDefinition>* chains;
    std::vector<DynamicChainRuntimeState>* chainStates;
    std::vector<BoneLocalOffset>* pose;
    int stepCount;
    float fixedTimestepSeconds;
    Vec3 gravity;
    WindSettings wind;

    // Entity world matrix (rotation + translation, no scale) and its inverse.
    Mat4 entityWorldMatrix;
    Mat4 entityWorldMatrixInverse;

    // Frozen rigs take no new steps but still re-apply their last particles.
    bool frozen;

    // World-space colliders of the entity (empty = no collision).
    const std::vector<Collider>* resolvedColliders;

    StepOptions options;
    bool interpolate;
    float alpha;

    // Frame length and the accumulator before this frame's steps; place each step inside the frame.
    float frameDeltaSeconds;
    float accumulatedBeforeSeconds;

    // One recorder slot per chain (entries may be null); null when the recorder is off.
    const std::vector<ChainFrameSample*>* recorderSlots;
};


void AddDiagnostics(StepDiagnostics& sum, const StepDiagnostics& add) noexcept
{
    sum.collisionRawPushedParticles += add.collisionRawPushedParticles;
    sum.collisionPushedParticles += add.collisionPushedParticles;
    sum.collisionPushDistanceSum += add.collisionPushDistanceSum;
    sum.bendClampedJoints += add.bendClampedJoints;
    sum.reseeds += add.reseeds;
    sum.nanGuardHits += add.nanGuardHits;
}

// Stores this step's particles in the anchor frame as a display knot: the mean of the last two raw steps.
// A welded joint has the same anchor-local position every step, so the mean is exact for it.
void StoreStepLocal(DynamicChainRuntimeState& state, const Mat4& anchorInverse, bool resetHistory)
{
    const std::size_t count = state.particles.size();
    const bool restart = resetHistory || state.currStepLocal.size() != count || state.prevStepLocal.size() != count
        || state.lastStepLocal.size() != count;
    if (!restart) {
        state.prevStepLocal.swap(state.currStepLocal);
    }
    state.currStepLocal.resize(count);
    state.lastStepLocal.resize(count);
    for (std::size_t i = 0; i < count; ++i) {
        const Vec3 local = anchorInverse.TransformPoint(state.particles[i].position);
        state.currStepLocal[i] = restart ? local : (state.lastStepLocal[i] + local) * 0.5f;
        state.lastStepLocal[i] = local;
    }
    if (restart) {
        state.prevStepLocal = state.currStepLocal;
    }
}

// Steps every chain in [beginIndex, endIndex) and writes the result into the pose.
// One copy serves both the serial and the job path.
void StepDynamicChainRange(std::uint32_t beginIndex, std::uint32_t endIndex, DynamicChainBatchContext& context)
{
    for (std::uint32_t chainIndex = beginIndex; chainIndex < endIndex; ++chainIndex) {
        const DynamicChainDefinition& chain = (*context.chains)[chainIndex];
        DynamicChainRuntimeState& state = (*context.chainStates)[chainIndex];
        ChainFrameSample* slot = context.recorderSlots != nullptr ? (*context.recorderSlots)[chainIndex] : nullptr;

        // Animated (pre-physics) pose in world space; captured once before any step.
        state.boneCache.Reset(context.skeleton->bones.size());
        const Mat4 rootWorld
            = context.entityWorldMatrix * state.boneCache.Get(*context.skeleton, *context.pose, chain.rootBoneIndex);
        const Vec3 rootWorldPos = rootWorld.TransformPoint(Vec3::Zero());

        std::vector<Vec3> animatedJointWorldPositions;
        animatedJointWorldPositions.reserve(chain.jointBoneIndices.size());
        for (std::int32_t boneIndex : chain.jointBoneIndices) {
            const Mat4 jointWorld
                = context.entityWorldMatrix * state.boneCache.Get(*context.skeleton, *context.pose, boneIndex);
            animatedJointWorldPositions.push_back(jointWorld.TransformPoint(Vec3::Zero()));
        }

        Mat4 anchorInverse;
        const bool interpolate = context.interpolate && rootWorld.TryInverse(anchorInverse);
        if (!interpolate) {
            state.prevStepLocal.clear();
            state.currStepLocal.clear();
            state.lastStepLocal.clear();
            state.prevAnimatedLocal.clear();
        }

        // Animated joints in the anchor frame; blended per step so a rigid anchor motion never distorts the targets.
        const std::size_t jointCount = chain.jointBoneIndices.size();
        if (interpolate) {
            state.scratchAnimatedLocal.resize(jointCount);
            for (std::size_t i = 0; i < jointCount; ++i) {
                state.scratchAnimatedLocal[i] = anchorInverse.TransformPoint(animatedJointWorldPositions[i]);
            }
        }

        // Each step sees the animated pose of its own time inside the frame, not the frame-end pose.
        const bool blendTargets = interpolate && !context.frozen && context.stepCount > 0 && context.frameDeltaSeconds > 0.0f
            && state.prevAnimatedLocal.size() == jointCount
            && Length(rootWorldPos - state.prevAnchorWorld.TransformPoint(Vec3::Zero())) <= chain.maxPlausibleRootDelta;

        StepDiagnostics frameDiag;
        if (!context.frozen) {
            for (int step = 0; step < context.stepCount; ++step) {
                Vec3 stepRootPos = rootWorldPos;
                Mat4 stepAnchorInverse = anchorInverse;
                const std::vector<Vec3>* stepTargets = &animatedJointWorldPositions;
                if (blendTargets) {
                    const float stepTime = static_cast<float>(step + 1) * context.fixedTimestepSeconds - context.accumulatedBeforeSeconds;
                    const float fraction = std::clamp(stepTime / context.frameDeltaSeconds, 0.0f, 1.0f);
                    const Mat4 stepAnchor = BlendAnchorFrames(state.prevAnchorWorld, rootWorld, fraction);
                    if (stepAnchor.TryInverse(stepAnchorInverse)) {
                        state.scratchTargets.resize(jointCount);
                        for (std::size_t i = 0; i < jointCount; ++i) {
                            state.scratchTargets[i] = stepAnchor.TransformPoint(
                                Lerp(state.prevAnimatedLocal[i], state.scratchAnimatedLocal[i], fraction));
                        }
                        stepRootPos = stepAnchor.TransformPoint(Vec3::Zero());
                        stepTargets = &state.scratchTargets;
                    } else {
                        stepAnchorInverse = anchorInverse;
                    }
                }

                StepDiagnostics stepDiag;
                StepOptions options = context.options;
                options.diagnostics = &stepDiag;
                const bool wasInitialized = state.initialized;
                StepDynamicChain(chain, stepRootPos, *stepTargets, state, context.fixedTimestepSeconds,
                    context.gravity, context.wind, *context.resolvedColliders, options);
                AddDiagnostics(frameDiag, stepDiag);
                if (interpolate) {
                    StoreStepLocal(state, stepAnchorInverse, !wasInitialized || stepDiag.reseeds > 0);
                }
            }
        }
        if (interpolate) {
            state.prevAnimatedLocal.swap(state.scratchAnimatedLocal);
            state.prevAnchorWorld = rootWorld;
        }

        // Always re-apply the last particles, so a frame without a step never shows the raw animated pose.
        std::vector<Vec3> displayWorld;
        if (state.initialized) {
            const std::size_t count = state.particles.size();
            if (interpolate && state.currStepLocal.size() != count) {
                StoreStepLocal(state, anchorInverse, true);
            }
            const bool useLocal = interpolate && state.prevStepLocal.size() == count && state.currStepLocal.size() == count;
            const float alpha = context.frozen ? 1.0f : context.alpha;
            displayWorld.reserve(count);
            for (std::size_t i = 0; i < count; ++i) {
                displayWorld.push_back(useLocal
                        ? InterpolateToAnchor(rootWorld, state.prevStepLocal[i], state.currStepLocal[i], alpha)
                        : state.particles[i].position);
            }

            std::vector<Vec3> simulatedPositions;
            simulatedPositions.reserve(count);
            for (const Vec3& world : displayWorld) {
                simulatedPositions.push_back(context.entityWorldMatrixInverse.TransformPoint(world));
            }
            ApplyDynamicChainPhysicsToPose(*context.skeleton, chain, simulatedPositions, *context.pose, state.boneCache);
        }

        if (slot != nullptr) {
            slot->rootAnimWorld = rootWorldPos;
            slot->anchorWorld = rootWorld;
            slot->animated = animatedJointWorldPositions;
            slot->display = displayWorld.empty() ? animatedJointWorldPositions : displayWorld;
            slot->fk.resize(chain.jointBoneIndices.size());
            for (std::size_t i = 0; i < chain.jointBoneIndices.size(); ++i) {
                const Mat4 jointWorld = context.entityWorldMatrix
                    * state.boneCache.Get(*context.skeleton, *context.pose, chain.jointBoneIndices[i]);
                slot->fk[i] = jointWorld.TransformPoint(Vec3::Zero());
            }
            slot->collisionRawPushedParticles = frameDiag.collisionRawPushedParticles;
            slot->collisionPushedParticles = frameDiag.collisionPushedParticles;
            slot->collisionPushDistanceSum = frameDiag.collisionPushDistanceSum;
            slot->bendClampedJoints = frameDiag.bendClampedJoints;
            slot->reseedCount = frameDiag.reseeds;
            slot->nanGuardHits = frameDiag.nanGuardHits;
        }
    }
}

// One entity's frame of work. Lives in a deque, so the pointers inside `context` stay valid while jobs run.
struct EntityStepWork {
    DynamicChainBatchContext context;
    std::vector<Collider> colliders;
    std::vector<ChainFrameSample*> recorderSlots;
    std::uint32_t chainCount = 0;
    bool parallel = false;
};

// One chain of one entity, with a cost estimate used to start the largest chains first.
struct ChainStepItem {
    EntityStepWork* work;
    std::uint32_t chainIndex;
    std::uint32_t cost;
};

// Every claimer takes the next unclaimed chain until none are left.
struct ChainClaimContext {
    const std::vector<ChainStepItem>* items;
    std::atomic<std::size_t> next{ 0 };
};

void ClaimAndStepChains(ChainClaimContext& claim)
{
    const std::vector<ChainStepItem>& items = *claim.items;
    for (std::size_t i = claim.next.fetch_add(1, std::memory_order_relaxed); i < items.size();
         i = claim.next.fetch_add(1, std::memory_order_relaxed)) {
        StepDynamicChainRange(items[i].chainIndex, items[i].chainIndex + 1, items[i].work->context);
    }
}

void RunChainClaimJob(std::uint32_t /*beginIndex*/, std::uint32_t /*endIndex*/, void* payload)
{
    GTE_PROFILE_JOB_SCOPE("StepDynamicChain");
    ClaimAndStepChains(*static_cast<ChainClaimContext*>(payload));
}

} // namespace

StepOptions BuildStepOptions(const GlobalPhysicsSettings& settings) noexcept
{
    StepOptions options;
    options.dampingMultiplier = settings.dampingMultiplier;
    options.stiffnessMultiplier = settings.stiffnessMultiplier;
    options.useRawPmxDamping = settings.useRawPmxDamping;
    options.useCollisionFit = settings.useCollisionFit;
    options.bendLimitsEnabled = settings.bendLimitsEnabled;
    options.bendLimitScale = settings.bendLimitScale;
    options.bendLimitMinRadians = DegToRad(settings.bendLimitMinDegrees);
    options.bendJointFloorRadians = DegToRad(settings.bendJointFloorDegrees);
    return options;
}

Mat4 ComputeEntityPhysicsMatrix(Registry& registry, Entity entity)
{
    const Transform world = ComputeWorldTransform(registry, entity);
    return Mat4::TRS(world.position, world.rotation, Vec3::One());
}

std::vector<Collider> ResolveEntityColliders(
    const DynamicChainRigCache::ModelEntry& model, const std::vector<BoneLocalOffset>& pose, const Mat4& entityWorldMatrix)
{
    std::vector<Collider> colliders;
    colliders.reserve(model.colliders.size());
    for (const ModelColliderDefinition& def : model.colliders) {
        const Mat4 boneWorld = entityWorldMatrix * ComputeBoneWorldMatrix(model.skeleton, pose, def.boneIndex);
        Collider collider;
        collider.shape = ToColliderShape(def.shape);
        collider.center = boneWorld.TransformPoint(def.localOffsetPosition);
        collider.rotation = Quat::FromMat4(boneWorld) * def.localOffsetRotation;
        collider.size = def.shapeSize;
        collider.group = def.group;
        collider.collisionMask = def.collisionMask;
        colliders.push_back(collider);
    }
    return colliders;
}

void PhysicsSystem::ResetRig(Registry& registry, Entity entity)
{
    if (DynamicChainRig* rig = registry.TryGetComponent<DynamicChainRig>(entity)) {
        for (DynamicChainRuntimeState& state : rig->chainStates) {
            state = DynamicChainRuntimeState{};
        }
        rig->accumulatedSeconds = 0.0f;
    }
}

void PhysicsSystem::RegisterDynamicChains(const std::string& absoluteGtaPath, const SkinnedMeshData& data)
{
    const PhysicsData* physics = data.physics.has_value() ? &*data.physics : nullptr;
    const DynamicChainDetectionDefaults defaults{};
    DynamicChainDetectionResult detection = DetectDynamicChains(data.skeleton, physics, defaults);

    // Saved joint tuning goes on top of the detected defaults, before anything is cached.
    ApplyJointPhysicsOverrides(detection.chains, data.jointPhysicsOverrides);

    DynamicChainRigCache::ModelEntry entry;
    entry.colliders = DetectModelColliders(data.skeleton, physics);
    entry.colliderRigidBodyNames.reserve(entry.colliders.size());
    for (const ModelColliderDefinition& collider : entry.colliders) {
        const bool valid = physics != nullptr && collider.rigidBodyIndex >= 0
            && static_cast<std::size_t>(collider.rigidBodyIndex) < physics->rigidBodies.size();
        entry.colliderRigidBodyNames.push_back(valid ? physics->rigidBodies[static_cast<std::size_t>(collider.rigidBodyIndex)].name : std::string());
    }
    entry.skeleton = data.skeleton;

    // Debug names and PMX limits per joint.
    entry.jointDebugInfo.resize(detection.chains.size());
    for (std::size_t c = 0; c < detection.chains.size(); ++c) {
        const DynamicChainDefinition& chain = detection.chains[c];
        std::vector<JointDebugInfo>& infos = entry.jointDebugInfo[c];
        infos.resize(chain.jointBoneIndices.size());
        for (std::size_t j = 0; j < infos.size(); ++j) {
            JointDebugInfo& info = infos[j];
            const std::int32_t bodyIndex = (c < detection.rigidBodyIndexPerJoint.size()
                                               && j < detection.rigidBodyIndexPerJoint[c].size())
                ? detection.rigidBodyIndexPerJoint[c][j]
                : -1;
            if (physics != nullptr && bodyIndex >= 0 && static_cast<std::size_t>(bodyIndex) < physics->rigidBodies.size()) {
                info.rigidBodyName = physics->rigidBodies[static_cast<std::size_t>(bodyIndex)].name;
            }
            const std::int32_t jointIndex = (c < detection.pmxJointIndexPerJoint.size()
                                                && j < detection.pmxJointIndexPerJoint[c].size())
                ? detection.pmxJointIndexPerJoint[c][j]
                : -1;
            if (physics != nullptr && jointIndex >= 0 && static_cast<std::size_t>(jointIndex) < physics->joints.size()) {
                const Joint& joint = physics->joints[static_cast<std::size_t>(jointIndex)];
                info.pmxJointFound = true;
                info.pmxJointName = joint.name;
                info.pmxRotLowerRad[0] = joint.rotateLowerLimit.x;
                info.pmxRotLowerRad[1] = joint.rotateLowerLimit.y;
                info.pmxRotLowerRad[2] = joint.rotateLowerLimit.z;
                info.pmxRotUpperRad[0] = joint.rotateUpperLimit.x;
                info.pmxRotUpperRad[1] = joint.rotateUpperLimit.y;
                info.pmxRotUpperRad[2] = joint.rotateUpperLimit.z;
            }
        }
    }

    // Fit each joint's collision to the bind pose so overlapping joints are never pushed.
    if (!entry.colliders.empty()) {
        const std::vector<Collider> bindColliders = ResolveEntityColliders(entry, {}, Mat4::Identity());
        for (DynamicChainDefinition& chain : detection.chains) {
            std::vector<Vec3> bindPositions;
            bindPositions.reserve(chain.jointBoneIndices.size());
            for (std::int32_t boneIndex : chain.jointBoneIndices) {
                bindPositions.push_back(ComputeBoneWorldMatrix(entry.skeleton, {}, boneIndex).TransformPoint(Vec3::Zero()));
            }
            FitJointCollisionToBindPose(chain, bindPositions, bindColliders);
        }
    }

#ifndef NDEBUG
    {
        std::unordered_set<std::int32_t> seenBoneIndices;
        for (const DynamicChainDefinition& chain : detection.chains) {
            for (std::int32_t boneIndex : chain.jointBoneIndices) {
                assert(seenBoneIndices.insert(boneIndex).second && "two chains share a bone; parallel stepping is unsafe");
            }
        }
    }
#endif

    entry.chains = std::move(detection.chains);
    entry.diagnostics = std::move(detection.diagnostics);
    m_rigCache.Register(absoluteGtaPath, std::move(entry));
}

void PhysicsSystem::AttachDynamicChainRigIfNeeded(Registry& registry, Entity rootEntity, const std::string& absoluteGtaPath)
{
    const DynamicChainRigCache::ModelEntry* model = m_rigCache.TryGet(absoluteGtaPath);
    if (model == nullptr || model->chains.empty()) {
        return;
    }

    DynamicChainRig& rig = registry.AddComponent<DynamicChainRig>(rootEntity);
    rig.meshGtaPath = absoluteGtaPath;
    rig.chainStates.resize(model->chains.size());
}

void PhysicsSystem::Update(Registry& registry, double deltaSeconds)
{
    GTE_PROFILE_SCOPE("PhysicsSystem::Update");
    const auto startTime = std::chrono::steady_clock::now();

    PhysicsDebugRecorder& recorder = m_recorderOverride != nullptr ? *m_recorderOverride : m_ownRecorder;
    const bool recording = recorder.Enabled();
    const StepOptions stepOptions = BuildStepOptions(m_globalSettings);

    // Pass 1 (main thread): prepare every entity. Pass 2: step all chains of all entities in one parallel call.
    std::deque<EntityStepWork> entityWork;
    ComponentStorage<DynamicChainRig>& rigs = registry.Storage<DynamicChainRig>();
    for (std::size_t i = 0; i < rigs.Size(); ++i) {
        DynamicChainRig& rig = rigs.ComponentAt(i);
        if (!rig.enabled) {
            continue;
        }
        const Entity entity = rigs.EntityAt(i);

        ResolvedAnimationPose* resolvedPose = registry.TryGetComponent<ResolvedAnimationPose>(entity);
        if (resolvedPose == nullptr) {
            continue;
        }

        const DynamicChainRigCache::ModelEntry* model = m_rigCache.TryGet(rig.meshGtaPath);
        if (model == nullptr || model->chains.size() != rig.chainStates.size()) {
            continue;
        }

        // A rig with no new step this frame must still re-apply its last particles (no pop to the animated pose).
        const float accumulatedBefore = rig.frozen ? 0.0f : rig.accumulatedSeconds;
        int stepCount = 0;
        if (rig.frozen) {
            rig.accumulatedSeconds = 0.0f; // Never bank time while frozen.
        } else {
            stepCount = ComputeFixedStepCount(rig.accumulatedSeconds, static_cast<float>(deltaSeconds),
                m_globalSettings.fixedTimestepSeconds, m_globalSettings.maxStepsPerFrame);
        }
        const float alpha = m_globalSettings.fixedTimestepSeconds > 0.0f
            ? std::clamp(rig.accumulatedSeconds / m_globalSettings.fixedTimestepSeconds, 0.0f, 1.0f)
            : 0.0f;

        const Mat4 entityWorldMatrix = ComputeEntityPhysicsMatrix(registry, entity);
        Mat4 entityWorldMatrixInverse;
        if (!entityWorldMatrix.TryInverse(entityWorldMatrixInverse)) {
            assert(false && "entity world matrix is singular; check for a non-normalized rotation");
            entityWorldMatrixInverse = Mat4::Identity();
        }

        EntityStepWork& work = entityWork.emplace_back();

        // Colliders are resolved once per entity and shared by its chains.
        const bool anyChainWantsCollision = m_globalSettings.collisionEnabled
            && std::any_of(model->chains.begin(), model->chains.end(),
                [](const DynamicChainDefinition& c) { return c.collisionEnabled; });
        if (anyChainWantsCollision && !model->colliders.empty()) {
            work.colliders = ResolveEntityColliders(*model, resolvedPose->pose, entityWorldMatrix);
        }

        std::size_t totalJoints = 0;
        for (const DynamicChainDefinition& chain : model->chains) {
            totalJoints += chain.jointBoneIndices.size();
        }

        // Recorder slots are looked up here on the main thread, so jobs never touch the recorder map.
        const bool recordThis = recording && (m_recorderOverride == nullptr || entity == m_recorderOverrideEntity);
        if (recordThis) {
            std::vector<std::size_t> jointCounts;
            jointCounts.reserve(model->chains.size());
            for (const DynamicChainDefinition& chain : model->chains) {
                jointCounts.push_back(chain.jointBoneIndices.size());
            }
            recorder.BeginFrame(entity, model->chains.size(), jointCounts, static_cast<float>(deltaSeconds), stepCount);
            work.recorderSlots.reserve(model->chains.size());
            for (std::size_t c = 0; c < model->chains.size(); ++c) {
                work.recorderSlots.push_back(recorder.SlotFor(entity, c));
            }
        }

        work.context = DynamicChainBatchContext{ &model->skeleton, &model->chains, &rig.chainStates, &resolvedPose->pose,
            stepCount, m_globalSettings.fixedTimestepSeconds, m_globalSettings.gravity, m_globalSettings.wind,
            entityWorldMatrix, entityWorldMatrixInverse, rig.frozen, &work.colliders, stepOptions,
            m_globalSettings.interpolateOutput, alpha, static_cast<float>(deltaSeconds), accumulatedBefore,
            recordThis ? &work.recorderSlots : nullptr };
        work.chainCount = static_cast<std::uint32_t>(model->chains.size());
        work.parallel = totalJoints >= kMinDynamicJointsToParallelize && model->chains.size() > 1;
    }

    // Chains of different entities are independent, so one claim list covers all of them, largest first.
    std::vector<ChainStepItem> items;
    for (EntityStepWork& work : entityWork) {
        if (!work.parallel) {
            continue;
        }
        for (std::uint32_t c = 0; c < work.chainCount; ++c) {
            const std::size_t joints = (*work.context.chains)[c].jointBoneIndices.size();
            items.push_back(ChainStepItem{ &work, c, static_cast<std::uint32_t>(joints * (work.context.stepCount + 1)) });
        }
    }
    std::stable_sort(items.begin(), items.end(),
        [](const ChainStepItem& a, const ChainStepItem& b) { return a.cost > b.cost; });

    ChainClaimContext claim;
    claim.items = &items;
    Jobs::JobHandle chainHandle;
    const std::size_t workerCount = Jobs::JobSystem::Instance().WorkerCount();
    const std::size_t helperCount = items.size() > 1 ? std::min(workerCount, items.size() - 1) : 0;
    if (helperCount > 0) {
        Jobs::Dispatch(&RunChainClaimJob, static_cast<std::uint32_t>(helperCount), &claim, chainHandle,
            /*minItemsPerBatch=*/1);
    }
    // The main thread works too: serial entities first, then it claims chains like a worker.
    for (EntityStepWork& work : entityWork) {
        if (!work.parallel) {
            StepDynamicChainRange(0, work.chainCount, work.context);
        }
    }
    ClaimAndStepChains(claim);
    if (helperCount > 0) {
        GTE_PROFILE_BLOCKED("Wait.Workers");
        Jobs::JobSystem::Instance().WaitForJobs(chainHandle);
    }

    m_lastUpdateMilliseconds
        = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - startTime).count();
}

} // namespace gte
