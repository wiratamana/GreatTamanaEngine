#include "PhysicsSystem.h"

#include "../../Animation/BoneWorldMatrixQuery.h"
#include "../../ECS/Components/DynamicChainRig.h"
#include "../../ECS/Components/ResolvedAnimationPose.h"
#include "../../Physics/BoneChainPhysicsResolver.h"
#include "../../Physics/DynamicChainDetection.h"
#include "../../Physics/DynamicChainSolver.h"
#include "../../Physics/FixedTimestepAccumulator.h"
#include "../../Profiling/ScopeTimer.h"
#include "../Animation/SkeletalRigCache.h"

namespace gte {

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

        std::vector<BoneLocalOffset>& pose = resolvedPose->pose;

        const int stepCount = ComputeFixedStepCount(rig.accumulatedSeconds, static_cast<float>(deltaSeconds),
            m_globalSettings.fixedTimestepSeconds, m_globalSettings.maxStepsPerFrame);
        if (stepCount <= 0) {
            continue;
        }

        for (std::size_t chainIndex = 0; chainIndex < model->chains.size(); ++chainIndex) {
            const DynamicChainDefinition& chain = model->chains[chainIndex];
            DynamicChainRuntimeState& state = rig.chainStates[chainIndex];

            // Captured ONCE, from `pose` EXACTLY as EvaluatePoses() left it
            // this frame, BEFORE any substep below mutates `pose` in place -
            // see PHASE0's Revision Notes finding #4. Never re-read inside
            // the substep loop.
            const Mat4 rootWorld = ComputeBoneWorldMatrix(model->skeleton, pose, chain.rootBoneIndex);
            const Vec3 rootWorldPos = rootWorld.TransformPoint(Vec3::Zero());

            std::vector<Vec3> animatedJointWorldPositions;
            animatedJointWorldPositions.reserve(chain.jointBoneIndices.size());
            for (std::int32_t boneIndex : chain.jointBoneIndices) {
                animatedJointWorldPositions.push_back(
                    ComputeBoneWorldMatrix(model->skeleton, pose, boneIndex).TransformPoint(Vec3::Zero()));
            }

            for (int step = 0; step < stepCount; ++step) {
                StepDynamicChain(chain, rootWorldPos, animatedJointWorldPositions, state,
                    m_globalSettings.fixedTimestepSeconds, m_globalSettings.gravity, m_globalSettings.wind);

                std::vector<Vec3> simulatedPositions;
                simulatedPositions.reserve(state.particles.size());
                for (const VerletParticle& particle : state.particles) {
                    simulatedPositions.push_back(particle.position);
                }
                ApplyDynamicChainPhysicsToPose(model->skeleton, chain, simulatedPositions, pose);
            }
        }
    }
}

} // namespace gte
