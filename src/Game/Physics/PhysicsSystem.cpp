#include "PhysicsSystem.h"

#include "../../Animation/BoneWorldMatrixQuery.h"
#include "../../ECS/Components/DynamicChainRig.h"
#include "../../ECS/Components/ResolvedAnimationPose.h"
#include "../../Physics/BoneChainPhysicsResolver.h"
#include "../../Physics/DynamicChainSolver.h"
#include "../../Physics/FixedTimestepAccumulator.h"
#include "../../Profiling/ScopeTimer.h"
#include "../Animation/SkeletalRigCache.h"

namespace gte {

void PhysicsSystem::RegisterDynamicChains(const std::string& absoluteGtaPath, const SkinnedMeshData& data)
{
    // PHASE3 STUB - real chain detection (Physics/DynamicChainDetection.h)
    // is PHASE4's job (see
    // task_manager/verlet-integration-1/PHASE4_PARAMETER_AUTHORING_AND_DATA_DRIVEN_CONFIG.md).
    // Deliberately never registers anything yet, so m_rigCache.TryGet()
    // keeps returning nullptr for every model and Update() below stays a
    // PROVABLE no-op this phase - see DynamicChainRigCache.h's own file
    // comment.
    (void)absoluteGtaPath;
    (void)data;
}

void PhysicsSystem::AttachDynamicChainRigIfNeeded(Registry& registry, Entity rootEntity, const std::string& absoluteGtaPath)
{
    // PHASE3 STUB - mirrors RegisterDynamicChains() above: since that
    // function never registers a non-empty chain list yet, this is
    // unconditionally a no-op for every model until PHASE4 lands.
    (void)registry;
    (void)rootEntity;
    (void)absoluteGtaPath;
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
