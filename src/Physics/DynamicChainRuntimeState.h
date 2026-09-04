#pragma once
#include "VerletParticle.h"

#include <vector>

namespace gte {

// Persistent, per-INSTANCE simulation memory for one DynamicChainDefinition -
// owned by whichever entity/component is simulating it (see PHASE3's new
// DynamicChainRig component), one DynamicChainRuntimeState per
// DynamicChainDefinition, never shared across two different entities animating
// the same underlying model (this mirrors AnimationSystem's own existing,
// documented "two entities sharing one *.gta currently fight over shared
// state" limitation - see AGENTS.md's Job System table - a per-entity
// runtime state is exactly what avoids that same trap here).
struct DynamicChainRuntimeState {
    // Index-aligned with DynamicChainDefinition::jointBoneIndices - resized
    // and (re-)seeded to the animated bind pose the first time
    // StepDynamicChain() is ever called for this instance (see its own
    // "lazy init" step) so the chain doesn't visibly "fall" from the origin
    // on its very first frame.
    std::vector<VerletParticle> particles;
    bool initialized = false;

    // Running simulation clock, in seconds, advanced by exactly
    // fixedDeltaTime every StepDynamicChain() call - fed to
    // ComputeWindAcceleration() (Phase 1) so wind phase is continuous
    // across frames rather than resetting.
    float simulationTimeSeconds = 0.0f;

    // PHASE5 (task_manager/verlet-integration-1/
    // PHASE5_COLLISION_STABILITY_AND_PERFORMANCE_HARDENING.md, 3.3) - the
    // root world position StepDynamicChain() was last called with, used to
    // detect an implausible root teleport between two consecutive calls.
    // Set to this call's OWN rootWorldPosition before every
    // StepDynamicChain() call returns, unconditionally - whether this call
    // re-seeded because of a teleport, a genuine lazy-init, or ran the
    // ordinary integrate+constrain path - so the guard never permanently
    // trips after firing once.
    Vec3 lastRootWorldPosition = Vec3::Zero();
};

} // namespace gte
