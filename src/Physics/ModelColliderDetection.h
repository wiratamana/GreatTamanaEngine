#pragma once
#include "ModelColliderDefinition.h"
#include "../Assets/PhysicsData.h"
#include "../Assets/SkeletonData.h"

#include <vector>

namespace gte {

// task_manager/verlet-integration-9, PHASE3 - scans every
// RigidBodyMotionType::Static rigid body in `physics` (Assets/PhysicsData.h)
// and, for each one that is genuinely usable as a collision obstacle
// (attached to a real bone, non-degenerate shape - see
// ModelColliderDetection.cpp's own IsDegenerateColliderShape() for the
// exact per-shape thresholds, mirroring src/Editor/RigidBodyWireframe.h's
// own documented "nothing meaningful to draw" rules), precomputes its
// bind-pose-relative offset from its own tracked bone (see
// ModelColliderDefinition.h's own doc comment for exactly what that means
// and why) and returns the resulting list, SORTED by boneIndex ascending
// (tie-broken by the rigid body's own original PhysicsData::rigidBodies
// index) for full determinism, matching every other detection algorithm in
// this codebase (see Physics/DynamicChainDetection.h's own Step H).
//
// Deliberately independent of DetectDynamicChains() (Physics/
// DynamicChainDetection.h) - a Static rigid body is a collision obstacle
// regardless of whether the model has ANY dynamic chains at all, and a
// model's collider list never depends on which chains were detected. Both
// functions are called side-by-side by the SAME caller
// (Game/Physics/PhysicsSystem.cpp::RegisterDynamicChains(), PHASE3/PHASE4)
// against the SAME underlying PhysicsData.
//
// Returns an empty list (never null-derefs, never throws) if `physics` is
// nullptr or contains no eligible Static bodies - a model with no PMX
// physics data, or one whose Static bodies are all degenerate/unattached,
// simply has nothing to collide against, exactly like
// DetectDynamicChains()'s own "physics == nullptr -> empty result"
// contract.
//
// Explicitly Dynamic/DynamicAndBoneMerge bodies are NEVER included here -
// see this phase's own strategy document (task_manager/verlet-integration-9/
// PHASE3_AUTO_DETECTION_OF_MODEL_COLLIDERS_FROM_PMX_RIGID_BODIES.md, Step 2)
// for why colliding a chain against another SIMULATED body is out of scope.
std::vector<ModelColliderDefinition> DetectModelColliders(const SkeletonData& skeleton, const PhysicsData* physics);

} // namespace gte
