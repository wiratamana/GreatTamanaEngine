#pragma once
#include "../../Animation/BoneLocalOffset.h"
#include "../../ECS/Registry.h"
#include "../../Math/Mat4.h"
#include "../../Physics/Collider.h"
#include "../../Physics/DynamicChainSolver.h"
#include "../../Physics/GlobalPhysicsSettings.h"
#include "DynamicChainRigCache.h"
#include "PhysicsDebugRecorder.h"

#include <string>
#include <vector>

namespace gte {

struct SkinnedMeshData; // Game/Animation/SkeletalRigCache.h

// Solver options from the global settings (diagnostics left null).
StepOptions BuildStepOptions(const GlobalPhysicsSettings& settings) noexcept;

// World matrix (rotation + translation, no scale) of the entity, as the solver sees it.
Mat4 ComputeEntityPhysicsMatrix(Registry& registry, Entity entity);

// Resolves the model colliders to world space. An empty `pose` gives the bind pose.
std::vector<Collider> ResolveEntityColliders(
    const DynamicChainRigCache::ModelEntry& model, const std::vector<BoneLocalOffset>& pose, const Mat4& entityWorldMatrix);

// Secondary-motion physics: Verlet dynamic bone chains driven by PMX rigid bodies and joints.
// Runs after pose evaluation and rewrites the physics bones of ResolvedAnimationPose in place.
class PhysicsSystem {
public:
    // Detects the chains of a skinned model and caches them (plus a skeleton copy) under `absoluteGtaPath`.
    void RegisterDynamicChains(const std::string& absoluteGtaPath, const SkinnedMeshData& data);

    // Attaches a DynamicChainRig to `rootEntity` when the model has at least one detected chain.
    void AttachDynamicChainRigIfNeeded(Registry& registry, Entity rootEntity, const std::string& absoluteGtaPath);

    // Steps every enabled rig that has a ResolvedAnimationPose this frame.
    void Update(Registry& registry, double deltaSeconds);

    const GlobalPhysicsSettings& GetGlobalPhysicsSettings() const noexcept { return m_globalSettings; }
    GlobalPhysicsSettings& GetGlobalPhysicsSettings() noexcept { return m_globalSettings; }

    DynamicChainRigCache& GetDynamicChainRigCache() noexcept { return m_rigCache; }
    const DynamicChainRigCache& GetDynamicChainRigCache() const noexcept { return m_rigCache; }

    // Live-capture recorder. Off by default; when off, Update() pays one bool test.
    PhysicsDebugRecorder& GetDebugRecorder() noexcept { return m_ownRecorder; }
    const PhysicsDebugRecorder& GetDebugRecorder() const noexcept { return m_ownRecorder; }

    // Records into `recorder` instead of the own one for as long as it is set. Null restores the own recorder.
    void SetDebugRecorderOverride(PhysicsDebugRecorder* recorder) noexcept { m_recorderOverride = recorder; }

    // Wall time of the last Update() call, in milliseconds.
    double LastUpdateMilliseconds() const noexcept { return m_lastUpdateMilliseconds; }

    // Drops all simulation state of the entity's rig (next step re-seeds from the animated pose).
    static void ResetRig(Registry& registry, Entity entity);

private:
    DynamicChainRigCache m_rigCache;
    GlobalPhysicsSettings m_globalSettings;
    PhysicsDebugRecorder m_ownRecorder;
    PhysicsDebugRecorder* m_recorderOverride = nullptr;
    double m_lastUpdateMilliseconds = 0.0;
};

} // namespace gte
