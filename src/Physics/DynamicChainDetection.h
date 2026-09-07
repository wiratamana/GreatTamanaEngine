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

// task_manager/verlet-integration-6, Phase 3 - purely diagnostic output,
// alongside the real chains - never affects simulation, but must never be
// silently lost (see DynamicChainDetection.cpp's own Step F, case 4, and
// Step C/D).
struct DynamicChainDetectionDiagnostics {
    // Dynamic/DynamicAndBoneMerge rigid-body bones that could not reach ANY
    // Static anchor - populated from TWO sources, unioned and deduplicated:
    // (a) bones whose own rigid body is not graph-reachable from any Static
    // body at all via ANY path (RigidBodyJointGraph's own
    // orphanedDynamicRigidBodyIndices), and (b) bones that ARE
    // graph-reachable but whose own real skeleton ancestry never crosses a
    // Static anchor bone (or hits an ancestry cycle). NOT simulated; the
    // Editor may render these distinctly (see task_manager/
    // verlet-integration-6/PHASE5_EDITOR_VISUALIZATION_TREE_WEB_AND_ORPHAN_GIZMO.md).
    std::vector<std::int32_t> orphanedDynamicBoneIndices;
    // Original PhysicsData::joints indices whose two endpoints landed in two
    // DIFFERENT final chains - dropped, never applied, never crashes.
    std::vector<std::int32_t> crossChainJointsDropped;
    // RigidBody indices dropped by the "same boneIndex, keep lowest index"
    // tie-break - a Static-vs-Dynamic collision on one bone silently decides
    // whether that bone becomes an ANCHOR or a MEMBER, a materially
    // significant, otherwise-invisible outcome.
    std::vector<std::int32_t> duplicateBoneRigidBodyAssignmentsDropped;
};

struct DynamicChainDetectionResult {
    std::vector<DynamicChainDefinition> chains;
    DynamicChainDetectionDiagnostics diagnostics;
};

// task_manager/verlet-integration-6 - detects dynamic bone chains by
// traversing PhysicsData's own RigidBody/Joint graph (REPLACES the previous
// Bone::deformAfterPhysics-based algorithm entirely - see this campaign's
// PHASE0_MASTER_STRATEGY.md for the full rationale; Bone::deformAfterPhysics
// itself is untouched as a data field/Inspector checkbox, it simply is no
// longer read for chain detection).
//
// Algorithm summary (see DynamicChainDetection.cpp's own top-of-file comment
// for the complete Step A-H write-up):
//   A. physics == nullptr -> empty result (a model with no PMX rigid
//      body/joint data at all has nothing to detect chains FROM under this
//      algorithm - an intentional behavior change from the old
//      bone-flag-only algorithm; see the .cpp's own "Known Behavior Change"
//      note).
//   B. Build the RigidBody/Joint graph (RigidBodyJointGraph.h) and compute
//      which Dynamic/DynamicAndBoneMerge rigid bodies are graph-reachable
//      from a Static anchor.
//   C. Map bones to their own eligible RigidBody (Static or reachable
//      Dynamic), seeding the orphan diagnostic from any body Phase B already
//      proved has no reachable anchor at all, and diagnosing any duplicate
//      same-bone assignment.
//   D. For every reachable Dynamic bone, walk its REAL skeleton ancestor
//      chain (never the raw joint graph) to find which anchor it ultimately
//      belongs to and its own tree-parent - guaranteeing every recorded tree
//      edge is a real skeleton parent/child pair (the FK "a bone's rotation
//      only ever moves its descendants" constraint
//      BoneChainPhysicsResolver.cpp depends on). A bone whose own ancestry
//      never reaches an anchor (or hits a cycle) is flagged as orphaned.
//   E. Assemble each chain's jointBoneIndices/parentJointIndex/restLengths
//      in a valid, parent-before-child topological order (one chain per
//      anchor, however many branches/cross-braces it internally has - a
//      spider-web skirt becomes ONE chain, never one per branch).
//   F. Fold every remaining original PMX Joint whose two endpoints land in
//      the SAME final chain, but isn't already a tree edge, into an
//      ExtraStructuralConstraint (a "web brace" - stabilizes the simulation
//      without ever driving bone rotation); a Joint whose two endpoints land
//      in two DIFFERENT chains is dropped, but recorded in
//      diagnostics.crossChainJointsDropped, never silently lost.
//   G. Discard any assembled chain shorter than defaults.minimumChainLength;
//      seed DynamicJointSettings/head-collider defaults, overriding
//      mass/damping from each joint's own matched RigidBody.
//   H. Sort the final chain list and every diagnostic list for full
//      determinism - order-independent with respect to PhysicsData's own
//      rigidBodies/joints storage order.
//
// Known Behavior Change: a model with RigidBody/Joint PMX data entirely
// absent, or present but with zero Joints at all, now detects ZERO chains,
// regardless of how many bones have Bone::deformAfterPhysics == true. This
// is an intentional, accepted consequence of fully replacing the old
// bone-flag-only algorithm (see PHASE0_MASTER_STRATEGY.md, Step 1).
//
// Known Limitation: when two or more joints in the same chain share the
// exact same parentJointIndex-resolved parent bone (a genuine "hub", e.g. a
// spider-web skirt's own strand roots), BoneChainPhysicsResolver.cpp's
// ApplyDynamicChainPhysicsToPose() processes joints strictly in ascending
// order and, for a shared parent, each subsequent child's own corrective
// rotation OVERWRITES the previous child's - only the LAST child processed
// at a given hub each frame "wins" the parent bone's final FK rotation
// (every joint still gets a physically-simulated Verlet position - this only
// affects how faithfully the FK pose reproduces it for extra siblings
// sharing one rotating parent). Accepted, documented, not fixed by this
// campaign (see PHASE0_MASTER_STRATEGY.md's own "What We Will NOT Do").
DynamicChainDetectionResult DetectDynamicChains(
    const SkeletonData& skeleton, const PhysicsData* physics, const DynamicChainDetectionDefaults& defaults);

} // namespace gte
