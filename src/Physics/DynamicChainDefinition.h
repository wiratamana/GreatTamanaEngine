#pragma once
#include <cstdint>
#include <vector>

namespace gte {

// LOCAL (per-joint) tuning - see PHASE4's "global vs. local" split.
struct DynamicJointSettings {
    float damping = 0.08f;   // "Damping" - see VerletIntegration.h.
    float stiffness = 0.35f; // "Stiffness" (goal constraint) - see ChainConstraints.h's SolveGoalConstraint().
    float mass = 1.0f;       // "Weight" - inverse-mass fed to VerletParticle::inverseMass (must be > 0).
};

// One dynamic bone chain (a linear run of physics-simulated bones on one
// model - any number of independent chains may coexist on the same
// skeleton, sharing no state with each other) - `rootBoneIndex` is NOT
// simulated (it is the pinned anchor, always taken directly from the
// animated FK pose every step); `jointBoneIndices` is the ordered list of
// bones that ARE simulated, root-to-tip, each one's parent in the chain
// being the previous entry (or rootBoneIndex for the first).
struct DynamicChainDefinition {
    std::int32_t rootBoneIndex = -1;
    std::vector<std::int32_t> jointBoneIndices;
    std::vector<DynamicJointSettings> jointSettings; // index-aligned 1:1 with jointBoneIndices.

    // Bind-pose segment lengths, index-aligned with jointBoneIndices:
    // restLengths[0] is the distance from rootBoneIndex to jointBoneIndices[0]
    // in the BIND pose; restLengths[i] (i>0) is the distance from
    // jointBoneIndices[i-1] to jointBoneIndices[i]. Precomputed once (see
    // PHASE4's chain-building step) directly from SkeletonData::Bone::position
    // - never recomputed per frame.
    std::vector<float> restLengths;

    float gravityScale = 1.0f;             // LOCAL multiplier applied to the GLOBAL gravity vector (see PHASE4).
    float windScale = 1.0f;                // LOCAL multiplier applied to the GLOBAL WindSettings (see PHASE4).
    std::uint8_t constraintIterations = 4; // structural relaxation passes per fixed step - see ChainConstraints.h.

    // PHASE5 (task_manager/verlet-integration-1/
    // PHASE5_COLLISION_STABILITY_AND_PERFORMANCE_HARDENING.md, 3.2) - simple
    // head/body collision. LOCAL, per-chain authoring: which collider bone a
    // chain checks against (typically the head bone) is a per-model
    // decision. `headColliderBoneIndex` is the bone this chain's collision
    // sphere should track every step (see Physics/SphereCollider.h);
    // `headColliderRadius` is a world-space radius authored once (never
    // derived automatically from mesh geometry). Left DISABLED
    // (`hasHeadCollider = false`) by default even when
    // DynamicChainDetection.h pre-fills a reasonable starting
    // bone/radius - a human must opt in via the Editor Inspector.
    bool hasHeadCollider = false;
    std::int32_t headColliderBoneIndex = -1;
    float headColliderRadius = 0.0f;

    // PHASE5, 3.3 - numerical safety: if the chain's own root bone moves
    // farther than this in a single StepDynamicChain() call (a teleporting
    // character, an Editor gizmo drag, ...), every particle is re-seeded
    // onto the animated pose instead of being integrated across a spurious,
    // implausibly large displacement. A generous default sufficient for
    // hand-built definitions/tests; DynamicChainDetection.h overrides this
    // per-chain based on the chain's own actual combined rest length.
    float maxPlausibleRootDelta = 10.0f;
};

} // namespace gte
