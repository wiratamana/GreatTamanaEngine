#pragma once
#include "Collider.h"
#include "DynamicChainDefinition.h"
#include "../Math/Vec3.h"

#include <span>

namespace gte {

// Fits the collision of every joint of `chain` to the bind pose.
// bindJointPositions[i] is the bind position of joint i; bindColliders is the model collider list in bind pose.
// Per collidable collider: no push at the full radius changes nothing; a push even at radius 0 adds the
// collider index to ignoredColliderIndices; otherwise the radius shrinks to the largest one with no push.
// collisionFitRadius becomes the smallest such radius, and stays -1 when no collider touches the joint.
// Does nothing when the span sizes do not match the chain.
void FitJointCollisionToBindPose(
    DynamicChainDefinition& chain, std::span<const Vec3> bindJointPositions, std::span<const Collider> bindColliders);

} // namespace gte
