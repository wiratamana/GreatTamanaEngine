#pragma once
#include <cstddef>
#include <cstdint>
#include <vector>

namespace gte {

// LOCAL (per-joint) tuning - see PHASE4's "global vs. local" split.
//
// task_manager/verlet-integration-7, Phase 5 (PHASE5_IDLE_PHYSICS_TUNING_AND_SETTLING_REGRESSION.md,
// Culprit E) - `damping`/`stiffness` defaults below were RE-TUNED this phase.
// Every prior value (damping = 0.08f, stiffness = 0.35f) was only ever
// validated/judged with physics riding on TOP of an actively-playing MMD
// dance animation - large, constantly-changing bone motion that visually
// masked how mediocre these defaults actually were in true isolation. Once
// verlet-integration-7's Phases 1-4 made a perfectly still, NEVER-animated
// T-pose model simulate continuously for the first time, the old defaults
// were measured (via a throwaway tuning harness driving the real
// StepDynamicChain() directly) to sag less than 0.15% of the chain's own
// total rest length under default gravity - i.e. visually indistinguishable
// from "dead"/rigid, exactly the complaint this campaign exists to fix. The
// new defaults were chosen so a perfectly still T-pose chain (see
// tests/Physics/DynamicChainSolverIdleSettlingTests.cpp) visibly sags
// (comfortably above 1% of its own total rest length), settles into a
// stable shape within a small fraction of the test's own 10-second budget,
// and never diverges/explodes/NaNs - `stiffness` in particular had to drop
// far more than `damping` rose, since SolveGoalConstraint() is applied as a
// fixed-fraction-per-frame Lerp back toward the animated target EVERY single
// step (see ChainConstraints.h), which otherwise suppresses almost all
// gravity-driven motion even at a seemingly "low" value like 0.35.
struct DynamicJointSettings {
    float damping = 0.4f;    // "Damping" - see VerletIntegration.h.
    float stiffness = 0.02f; // "Stiffness" (goal constraint) - see ChainConstraints.h's SolveGoalConstraint().
    float mass = 1.0f;       // "Weight" - inverse-mass fed to VerletParticle::inverseMass (must be > 0).
};

// task_manager/verlet-integration-6, Phase 1/3 - a single non-hierarchy
// stabilizing constraint between two joints that are BOTH already members
// of this same chain's jointBoneIndices, but are NOT a parentJointIndex
// tree edge (see DynamicChainDefinition::parentJointIndex's own doc comment
// below). Indices are POSITIONS within jointBoneIndices
// (0..jointBoneIndices.size()-1), never raw skeleton bone indices - mirrors
// jointBoneIndices' own "index into itself" convention used by
// parentJointIndex. Purely a Verlet SolveDistanceConstraint() pair
// (ChainConstraints.h) - never drives bone rotation (see
// BoneChainPhysicsResolver.h's own "IMPORTANT DESIGN NOTE" for exactly why
// only a REAL skeleton parent/child pair can ever do that). This is how an
// MMD skirt's authored horizontal "ring brace" Joints (connecting two
// SIBLING bones, or two bones unrelated in the skeleton) still meaningfully
// stabilize the simulation even though they can never move a bone by
// themselves.
struct ExtraStructuralConstraint {
    std::int32_t jointIndexA = -1; // position within jointBoneIndices.
    std::int32_t jointIndexB = -1; // position within jointBoneIndices.
    float restLength = 0.0f;       // bind-pose distance between the two bones.
};

// One dynamic bone chain (a TREE of physics-simulated bones on one model -
// any number of independent chains may coexist on the same skeleton,
// sharing no state with each other) - `rootBoneIndex` is NOT simulated (it
// is the pinned anchor, always taken directly from the animated FK pose
// every step); `jointBoneIndices` is the ordered list of bones that ARE
// simulated. task_manager/verlet-integration-6, Phase 1 - a chain used to be
// an implicit flat linked list (jointBoneIndices[i]'s parent was ALWAYS
// jointBoneIndices[i-1]); it is now an EXPLICIT tree via parentJointIndex
// below, so a single chain can represent genuine branching (an MMD skirt's
// spider-web hub with 4+ children) instead of being forced into one object
// per branch - see PHASE0_MASTER_STRATEGY.md (verlet-integration-6) for the
// full rationale.
struct DynamicChainDefinition {
    std::int32_t rootBoneIndex = -1;
    std::vector<std::int32_t> jointBoneIndices;
    std::vector<DynamicJointSettings> jointSettings; // index-aligned 1:1 with jointBoneIndices.

    // task_manager/verlet-integration-6, Phase 1 - EXPLICIT tree-parent per
    // joint, index-aligned 1:1 with jointBoneIndices. parentJointIndex[i] is
    // a POSITION within jointBoneIndices (never a raw bone index) of joint
    // i's own parent joint; -1 means "my parent is rootBoneIndex directly."
    // INVARIANT (relied on by BoneChainPhysicsResolver.cpp/DynamicChainSolver.cpp,
    // and re-verified by both files' own tests): parentJointIndex[i], if not
    // -1, MUST be < i (every joint's parent already has an earlier position
    // in this same array) - this guarantees a single top-to-bottom pass over
    // jointBoneIndices always processes a parent strictly before any of its
    // children, exactly like the OLD implicit "i-1" order already guaranteed
    // by construction. A chain builder (Phase 3) that violates this ordering
    // produces a definition that will silently fail to pose/simulate
    // correctly for the affected joint and every one of its descendants -
    // this is the single most important invariant in this whole campaign.
    // ALSO INVARIANT: skeleton.bones[jointBoneIndices[i]].parentBoneIndex
    // must equal (parentJointIndex[i] < 0 ? rootBoneIndex :
    // jointBoneIndices[parentJointIndex[i]]) - i.e. parentJointIndex must
    // always describe a REAL skeleton parent/child pair, never an arbitrary
    // graph edge (see this file's own header comment above
    // ExtraStructuralConstraint, and PHASE0's Culprit A). Chain builders
    // (Phase 3) are the ONLY code that may construct this array; hand-built
    // test fixtures must respect it too.
    //
    // A pre-Phase1 "flat list" chain is just the special case
    // parentJointIndex[i] == static_cast<std::int32_t>(i) - 1 for every i -
    // use DynamicChainDefinition::MakeLinearParentIndices() below to build
    // exactly that shape without repeating this logic at every call site.
    std::vector<std::int32_t> parentJointIndex;

    // task_manager/verlet-integration-6, Phase 1/3 - see
    // ExtraStructuralConstraint's own doc comment above. May be empty (the
    // overwhelmingly common case for a plain single-strand hair/tail chain
    // with no cross-bracing Joints at all).
    std::vector<ExtraStructuralConstraint> extraConstraints;

    // Bind-pose segment lengths, index-aligned with jointBoneIndices:
    // restLengths[i] is the bind-pose distance between jointBoneIndices[i]
    // and ITS OWN parentJointIndex-resolved parent (rootBoneIndex if -1) -
    // see parentJointIndex's own doc comment above for exactly which bone
    // that is. Precomputed once, never recomputed per frame.
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

    // task_manager/verlet-integration-6, Phase 1 - convenience helper for
    // both hand-built test fixtures AND any future single-strand-only
    // caller: returns { -1, 0, 1, ..., jointCount - 2 }, the exact "flat
    // list" shape every chain implicitly had before this phase. Pass the
    // RESULT to a freshly-built DynamicChainDefinition's own
    // parentJointIndex field directly.
    static std::vector<std::int32_t> MakeLinearParentIndices(std::size_t jointCount);
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
