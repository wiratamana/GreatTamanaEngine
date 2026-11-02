#pragma once
#include "Collider.h"
#include "DynamicChainDefinition.h"
#include "../Math/Vec3.h"

#include <span>

namespace gte {

// Fraction of the largest bind-pose free radius that a fitted joint keeps.
// The slack stops a joint resting on a collider face from being pushed again on every step.
inline constexpr float kCollisionFitSafety = 0.5f;

// Distance a fresh particle at `position` with `radius` is moved by `collider` (0 when below 1e-5).
float ProbeCollisionPush(const Vec3& position, float radius, const Collider& collider) noexcept;

// Fits each joint's collision radius to the bind pose: kCollisionFitSafety times the largest radius no collider pushes
// (-1 = untouched). No-op when the span size does not match the joints.
void FitJointCollisionToBindPose(
    DynamicChainDefinition& chain, std::span<const Vec3> bindJointPositions, std::span<const Collider> bindColliders);

} // namespace gte
