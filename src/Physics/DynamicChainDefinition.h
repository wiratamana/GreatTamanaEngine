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

// task_manager/verlet-integration-5, Phase 1 - the result of
// FindDynamicChainJointByBoneIndex() below - "no match" is represented as
// { -1, -1 }, never a thrown exception/assert (a bone that is not a
// physics-driven joint at all - the overwhelmingly common case for most
// bones in most models - is an entirely normal, expected input, not an
// error).
struct DynamicChainJointLocation {
    std::int32_t chainIndex = -1;
    std::int32_t jointIndexInChain = -1;

    bool IsValid() const noexcept { return chainIndex >= 0 && jointIndexInChain >= 0; }
};

// Finds which chain (if any) has `boneIndex` as one of its OWN
// jointBoneIndices entries, and at what position within that chain's own
// ordered list. Returns a default-constructed (invalid, both fields -1)
// DynamicChainJointLocation if `boneIndex` is not a joint of ANY chain in
// `chains` - e.g. it is an ordinary (non-physics-driven) bone, or it is
// some chain's OWN rootBoneIndex (the anchor - see
// DynamicChainDefinition::rootBoneIndex's own doc comment: the root is
// never itself a member of jointBoneIndices, by construction, so it never
// matches here either).
//
// A bone index can appear in jointBoneIndices of AT MOST ONE chain, ever -
// see DynamicChainDetection.h's own DetectDynamicChains() contract (a
// branch point starts one NEW chain per child rather than folding into a
// shared definition, and a bone has exactly one parent) - so this always
// returns at most one match; the moment one is found, this returns
// immediately without scanning the remaining chains.
//
// Deliberately pure/free (no ECS, no Editor, no GPU) so both
// src/Editor/BoneViewerWindow.cpp (task_manager/verlet-integration-5,
// Phase 2 - resolving a clicked gizmo dot/tree row back to its chain) and
// src/Editor/Panels/InspectorPanel.cpp (Phase 3 - resolving the current
// Model-Part selection back to the exact DynamicJointSettings to show/
// edit) can share ONE tested implementation, rather than each hand-rolling
// their own subtly-different linear scan.
DynamicChainJointLocation FindDynamicChainJointByBoneIndex(
    const std::vector<DynamicChainDefinition>& chains, std::int32_t boneIndex);

} // namespace gte
