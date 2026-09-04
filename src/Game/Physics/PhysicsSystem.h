#pragma once
#include "../../ECS/Registry.h"
#include "../../Physics/GlobalPhysicsSettings.h"
#include "DynamicChainRigCache.h"

#include <string>

namespace gte {

struct SkinnedMeshData; // Game/Animation/SkeletalRigCache.h - forward-declared only, see below.

// The secondary-motion, Verlet-based dynamic-bone-chain physics orchestrator
// - the direct analog of AnimationSystem, but for physics instead of
// animation, and with NO dependency on AnimationSystem/
// Animation/MotionSampler.h/IkSolver.h/AppendBoneSolver.h/
// AnimationPoseEvaluator.h/VertexSkinning.h/ECS/Components/SkeletalAnimator.h/
// Renderer/Mesh/RenderSystem/MeshInstantiationSystem anywhere in this class
// (see task_manager/verlet-integration-1/PHASE3_PIPELINE_INTEGRATION_AND_FIXED_TIMESTEP.md's
// own v3 Revision Notice). Owns its own DynamicChainRigCache (chain
// definitions + a private copy of each registered model's SkeletonData) and
// its own GlobalPhysicsSettings (gravity/wind/fixed-timestep/max-steps) -
// NEITHER of these lives on AnimationSystem.
//
// Deliberately NOT part of AGENTS.md's "systems allowed to depend on both
// ECS and Renderer" list (RenderSystem/MeshInstantiationSystem/AnimationSystem)
// - PhysicsSystem never touches Renderer/Mesh/Pipeline at all; it only
// reads/writes ECS components and calls pure Physics/Animation/
// BoneWorldMatrixQuery.h functions. See AGENTS.md's "Entity-Component-System"
// section for the exact clarifying sentence this phase added.
class PhysicsSystem {
public:
    // Mirrors AnimationSystem::RegisterSkinnedMesh()'s own shape and calling
    // convention (called from the SAME Game::CreateMeshEntityFromGtaFile()
    // hand-off site, ALONGSIDE - never through - AnimationSystem::
    // RegisterSkinnedMesh()). Detects dynamic bone chains
    // (Physics/DynamicChainDetection.h, PHASE4) and caches them (plus a copy
    // of `data.skeleton`) keyed by `absoluteGtaPath`.
    void RegisterDynamicChains(const std::string& absoluteGtaPath, const SkinnedMeshData& data);

    // If this model has at least one detected chain, attaches a
    // DynamicChainRig component (sized to match) to `rootEntity` - called
    // right after RegisterDynamicChains() at the same Game.cpp call site.
    void AttachDynamicChainRigIfNeeded(Registry& registry, Entity rootEntity, const std::string& absoluteGtaPath);

    // For every entity carrying an ENABLED DynamicChainRig AND a
    // ResolvedAnimationPose (added earlier THIS SAME FRAME by
    // AnimationSystem::EvaluatePoses() - see Game::Update()'s fixed call
    // order, PHASE3's own v3 Revision Notice): fixed-timestep-accumulates
    // `deltaSeconds`, then for each of that entity's detected chains, steps
    // Phase 2's DynamicChainSolver the resulting number of times and
    // rewrites the physics-controlled bone entries of
    // ResolvedAnimationPose::pose in place via BoneChainPhysicsResolver's
    // ApplyDynamicChainPhysicsToPose(). An entity with no
    // ResolvedAnimationPose yet this frame (AnimationSystem skipped a
    // non-playing animator) is simply skipped - degrade gracefully, never
    // assume the component exists.
    void Update(Registry& registry, double deltaSeconds);

    const GlobalPhysicsSettings& GetGlobalPhysicsSettings() const noexcept { return m_globalSettings; }
    GlobalPhysicsSettings& GetGlobalPhysicsSettings() noexcept { return m_globalSettings; }

    // Editor-facing accessor (PHASE4, 3.5 - Inspector "Dynamic Chain
    // Physics" section) - lets InspectorPanel look up a model's detected
    // chains (for the read-only chain/joint count summary) and live-edit
    // their DynamicJointSettings (damping/stiffness/mass sliders) via
    // DynamicChainRigCache::TryGetMutable(). Never used by PhysicsSystem's
    // own Update()/RegisterDynamicChains()/AttachDynamicChainRigIfNeeded()
    // methods above, which already hold m_rigCache directly.
    DynamicChainRigCache& GetDynamicChainRigCache() noexcept { return m_rigCache; }
    const DynamicChainRigCache& GetDynamicChainRigCache() const noexcept { return m_rigCache; }

private:
    DynamicChainRigCache m_rigCache;
    GlobalPhysicsSettings m_globalSettings;
};

} // namespace gte
