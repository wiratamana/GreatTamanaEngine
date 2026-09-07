#pragma once

#include "../Assets/PhysicsData.h" // RigidBodyShape
#include "../Math/Vec3.h"

#include <vector>

namespace gte {

// One straight-line segment of a rigid body's wireframe outline, already
// fully positioned/oriented in the SAME model-local space as
// RigidBody::translate/MeshData::positions (see PhysicsData.h) - ready to
// be projected through a viewProj matrix and drawn directly (e.g. via
// ImDrawList::AddLine on each endpoint's projected screen position), with
// no further transform needed by the caller.
struct WireframeSegment {
    Vec3 a;
    Vec3 b;
};

// Builds a wireframe outline for one rigid body - a small set of straight
// LINE SEGMENTS that, drawn together, trace the ACTUAL silhouette of
// `shape` at `shapeSize`, positioned/oriented by `translate`/
// `rotateRadians` (Euler angles, radians, PMX convention - see
// PhysicsData.h's own RigidBody::rotateRadians doc comment). This is
// honest, per-shape geometry - a Sphere/Box/Capsule each produce a visually
// distinct, correctly-proportioned outline - NOT the flat, shape-blind
// screen-space circle approximation this replaces (see
// task_manager/verlet-integration-3/PHASE0_MASTER_STRATEGY.md's Culprit).
//
// `rotateRadians` is applied via Quat::FromEulerDegrees(RadToDeg(x),
// RadToDeg(y), RadToDeg(z)) - Quat::FromEulerDegrees's own documented
// Yaw(Y)*Pitch(X)*Roll(Z) composition order is ALREADY the exact same
// order PMX's own rigid-body rotation uses (verified directly against
// third_party/saba/src/Saba/Model/MMD/MMDPhysics.cpp's own
// `rotMat = ry * rx * rz` - see PHASE0_MASTER_STRATEGY.md, Culprit #3) -
// do not change this to a different Euler order.
//
// Degenerate/non-positive size components mean "nothing meaningful to
// draw for this shape" and return an EMPTY vector, never NaN/zero-length/
// garbage segments:
//   - Sphere: shapeSize.x (radius) <= 0.
//   - Box: ALL of shapeSize.x/y/z (half-extents) <= 0 (a box degenerate on
//     only one or two axes still draws its remaining, genuinely flat
//     outline - a flattened box is still a meaningful shape to see).
//   - Capsule: shapeSize.x (radius) <= 0. A non-positive shapeSize.y
//     (cylinder length) is treated as 0 (a "pure sphere" capsule, i.e. two
//     coincident hemispheres) rather than emptying the whole result.
std::vector<WireframeSegment> BuildRigidBodyWireframe(
    RigidBodyShape shape, const Vec3& shapeSize, const Vec3& translate, const Vec3& rotateRadians);

} // namespace gte
