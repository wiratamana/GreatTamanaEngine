#pragma once
#include "Collider.h"
#include "DynamicChainDefinition.h"
#include "../Math/Vec3.h"

#include <span>

namespace gte {

// Distance a fresh particle at `position` with `radius` is moved by `collider` (0 when below 1e-5).
float ProbeCollisionPush(const Vec3& position, float radius, const Collider& collider) noexcept;

// Fits the collision of every joint of `chain` to the bind pose (bindJointPositions is index-aligned with joints).
// Per collider: no push keeps the radius, a push at radius 0 ignores the collider, else the radius shrinks to fit.
// collisionFitRadius stays -1 when no collider touches the joint. No-op when the span size does not match.
void FitJointCollisionToBindPose(
    DynamicChainDefinition& chain, std::span<const Vec3> bindJointPositions, std::span<const Collider> bindColliders);

} // namespace gte
