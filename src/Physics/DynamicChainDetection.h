#pragma once
#include "DynamicChainDefinition.h"
#include "../Assets/PhysicsData.h"
#include "../Assets/SkeletonData.h"

#include <cstdint>
#include <vector>

namespace gte {

// LOCAL default parameters used to seed a detected chain's joints when no
// more specific per-bone RigidBody data is found (see DetectDynamicChains()
// below) - itself still "local" in the global/local split (these are
// per-MODEL defaults, tunable independently of GlobalPhysicsSettings),
// exposed to the Editor as the starting point a user then fine-tunes per
// joint (see the Inspector's "Dynamic Chain Physics" section,
// Panels/InspectorPanel.cpp).
struct DynamicChainDetectionDefaults {
    DynamicJointSettings defaultJointSettings; // damping/stiffness/mass fallback.
    float defaultGravityScale = 1.0f;
    float defaultWindScale = 1.0f;
    std::uint8_t defaultConstraintIterations = 4;
    // Shorter runs (e.g. a single physics-driven bone) are not worth
    // simulating as a chain - skip them.
    std::size_t minimumChainLength = 2;
};

// Detects every maximal, LINEAR run of bones in `skeleton` where:
//   - every bone in the run has Bone::deformAfterPhysics == true, AND
//   - each bone's parentBoneIndex is the PREVIOUS bone in the same run
//     (a genuinely linear chain - a bone with more than one
//     deformAfterPhysics CHILD starts a new, separate chain per child rather
//     than being folded into one branching definition - this campaign does
//     not simulate branching/cloth-mesh chains, see
//     task_manager/verlet-integration-1/PHASE0_MASTER_STRATEGY.md's own
//     "What We Will NOT Do").
// The run's OWN PARENT bone (the first non-deformAfterPhysics ancestor, or -
// at a branch point - the deformAfterPhysics bone that branches into more
// than one child chain) becomes that chain's rootBoneIndex (the pinned
// anchor - see DynamicChainDefinition.h). A run that starts at the
// skeleton's own literal ROOT bone (no parent at all, i.e. the would-be
// rootBoneIndex has no ancestor to anchor to) is DISCARDED outright, never
// emitted with `rootBoneIndex == -1` - Animation/BoneChainResolver.h's own
// documented contract makes ComputeBoneWorldMatrix(..., -1) silently return
// Mat4::Identity(), which would anchor that chain to the WORLD origin
// instead of the character, a visibly wrong "bone floating at (0,0,0)"
// result for what is already a malformed/degenerate rig. Chains shorter
// than `defaults.minimumChainLength` joints are ALSO discarded - both
// rejection rules apply independently.
//
// For each detected joint bone, if `physics` (may be nullptr - a model with
// no PMX rigidbody data at all is still fully supported, using pure
// defaults) contains a RigidBody whose own `boneIndex` matches AND whose
// `motionType != RigidBodyMotionType::Static`, that RigidBody's `mass`/
// `linearDamping` seed this joint's DynamicJointSettings::mass/damping
// instead of `defaults.defaultJointSettings` - reusing already-authored PMX
// physics data exactly the way RigidBodyMotionType::Dynamic's own doc
// comment always intended, without building a general rigid-body solver.
// `stiffness` has no PMX equivalent - always comes from
// `defaults.defaultJointSettings.stiffness` (or a later per-joint Inspector
// override), never derived from RigidBody data.
//
// restLengths (DynamicChainDefinition's own field) are computed here too,
// directly from skeleton.bones[...].position (bind-pose positions) via
// Length(childBindPos - parentBindPos) - precomputed once, never recomputed
// per frame (see DynamicChainDefinition.h's own doc comment).
std::vector<DynamicChainDefinition> DetectDynamicChains(
    const SkeletonData& skeleton, const PhysicsData* physics, const DynamicChainDetectionDefaults& defaults);

} // namespace gte
