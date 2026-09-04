#pragma once
#include "WindField.h"
#include "../Math/Vec3.h"

namespace gte {

// GLOBAL (world-level) physics tuning shared by every DynamicChainRig
// PhysicsSystem simulates - see PHASE4's own "global vs. local" split
// (LOCAL, per-joint tuning instead lives in DynamicJointSettings -
// Physics/DynamicChainDefinition.h). Owned entirely by
// Game/Physics/PhysicsSystem.h, never by AnimationSystem - see
// task_manager/verlet-integration-1/PHASE3_PIPELINE_INTEGRATION_AND_FIXED_TIMESTEP.md's
// own v3 Revision Notice for why this must never move there.
struct GlobalPhysicsSettings {
    // World-space gravity acceleration applied (scaled by each chain's own
    // DynamicChainDefinition::gravityScale) every fixed step - see
    // Physics/DynamicChainSolver.h's StepDynamicChain(). Points "down" by
    // default (this engine's Y-up convention - see Math/Vec3.h's own
    // coordinate-convention comment).
    Vec3 gravity = Vec3::Down() * 9.8f;

    // One shared, world-level wind description - see Physics/WindField.h.
    WindSettings wind;

    // Fixed-timestep accumulator tuning (Culprit D) - see
    // Physics/FixedTimestepAccumulator.h's ComputeFixedStepCount(). One
    // engine-wide value for every model's chains (PHASE3's own explicit,
    // deliberate scope limit - see that document's "What We Will NOT Do").
    float fixedTimestepSeconds = 1.0f / 60.0f;
    int maxStepsPerFrame = 4;
};

} // namespace gte
