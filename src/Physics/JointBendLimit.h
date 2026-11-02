#pragma once
#include "DynamicChainDefinition.h"
#include "../Assets/PhysicsData.h"
#include "../Assets/SkeletonData.h"

#include <cstdint>
#include <unordered_map>
#include <vector>

namespace gte {

// Bend limit (radians) of one PMX joint from its X/Z rotate limits; pi (free) if not a spring/Dof6 joint.
float BendLimitFromPmxJoint(const Joint& joint) noexcept;

// Sets every joint's bendLimitRadians from the smallest PMX joint to its parent body.
// Returns the chosen PMX joint index per joint (-1 = none).
std::vector<std::int32_t> AssignBendLimits(DynamicChainDefinition& chain, const SkeletonData& skeleton,
    const PhysicsData& physics, const std::unordered_map<std::int32_t, std::int32_t>& boneToRigidBody);

} // namespace gte
