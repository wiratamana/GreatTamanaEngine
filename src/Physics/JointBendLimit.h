#pragma once
#include "DynamicChainDefinition.h"
#include "../Assets/PhysicsData.h"
#include "../Assets/SkeletonData.h"

#include <cstdint>
#include <unordered_map>
#include <vector>

namespace gte {

// Bend limit (radians) of one PMX joint. Bend axes are X and Z, Y is twist and ignored.
// Free (pi) when the type is not SpringDof6 or Dof6, or lower > upper on X or Z.
// Otherwise hypot(max(|lo.x|, |up.x|), max(|lo.z|, |up.z|)) capped at pi.
float BendLimitFromPmxJoint(const Joint& joint) noexcept;

// Sets DynamicJointSettings::bendLimitRadians of every joint of `chain` from the PMX joint between its
// rigid body and its tree parent's rigid body (pi when none). For a root child the parent bodies are all
// rigid bodies on the anchor bone. Several PMX joints for one pair: the smallest limit wins.
// `boneToRigidBody` maps bone index -> lowest rigid body index.
// Returns the chosen PMX joint index per joint (-1 = none).
std::vector<std::int32_t> AssignBendLimits(DynamicChainDefinition& chain, const SkeletonData& skeleton,
    const PhysicsData& physics, const std::unordered_map<std::int32_t, std::int32_t>& boneToRigidBody);

} // namespace gte
