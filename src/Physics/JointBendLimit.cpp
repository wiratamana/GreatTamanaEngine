#include "JointBendLimit.h"

#include "../Math/MathTypes.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <map>
#include <span>
#include <utility>

namespace gte {

namespace {

constexpr float kFreeLimit = kPi;

} // namespace

float BendLimitFromPmxJoint(const Joint& joint) noexcept
{
    if (joint.type != JointType::SpringDof6 && joint.type != JointType::Dof6) {
        return kFreeLimit;
    }
    const Vec3& lo = joint.rotateLowerLimit;
    const Vec3& up = joint.rotateUpperLimit;
    if (lo.x > up.x || lo.z > up.z) {
        return kFreeLimit;
    }
    const float x = std::max(std::fabs(lo.x), std::fabs(up.x));
    const float z = std::max(std::fabs(lo.z), std::fabs(up.z));
    const float limit = std::hypot(x, z);
    if (!std::isfinite(limit)) {
        return kFreeLimit;
    }
    return std::min(limit, kFreeLimit);
}

std::vector<std::int32_t> AssignBendLimits(DynamicChainDefinition& chain, const SkeletonData& skeleton,
    const PhysicsData& physics, const std::unordered_map<std::int32_t, std::int32_t>& boneToRigidBody)
{
    const std::size_t jointCount = chain.jointBoneIndices.size();
    std::vector<std::int32_t> pmxJointIndexPerJoint(jointCount, -1);
    if (chain.jointSettings.size() != jointCount || chain.parentJointIndex.size() != jointCount) {
        return pmxJointIndexPerJoint;
    }

    // Joint indices per unordered rigid body pair.
    using BodyPair = std::pair<std::int32_t, std::int32_t>;
    std::map<BodyPair, std::vector<std::int32_t>> jointsByPair;
    const std::int32_t bodyCount = static_cast<std::int32_t>(physics.rigidBodies.size());
    for (std::size_t j = 0; j < physics.joints.size(); ++j) {
        const std::int32_t a = physics.joints[j].rigidBodyAIndex;
        const std::int32_t b = physics.joints[j].rigidBodyBIndex;
        if (a < 0 || b < 0 || a == b || a >= bodyCount || b >= bodyCount) {
            continue;
        }
        jointsByPair[{ std::min(a, b), std::max(a, b) }].push_back(static_cast<std::int32_t>(j));
    }

    const auto lowestBodyOfBone = [&](std::int32_t boneIndex) -> std::int32_t {
        const auto found = boneToRigidBody.find(boneIndex);
        return found != boneToRigidBody.end() ? found->second : -1;
    };

    // Every rigid body attached to the anchor bone.
    std::vector<std::int32_t> anchorBodies;
    if (chain.rootBoneIndex >= 0 && static_cast<std::size_t>(chain.rootBoneIndex) < skeleton.bones.size()) {
        for (std::int32_t body = 0; body < bodyCount; ++body) {
            if (physics.rigidBodies[static_cast<std::size_t>(body)].boneIndex == chain.rootBoneIndex) {
                anchorBodies.push_back(body);
            }
        }
    }

    for (std::size_t i = 0; i < jointCount; ++i) {
        float best = kFreeLimit;
        std::int32_t bestJoint = -1;

        const std::int32_t childBody = lowestBodyOfBone(chain.jointBoneIndices[i]);
        std::int32_t singleParentBody = -1;
        std::span<const std::int32_t> parentBodies;
        const std::int32_t parentJoint = chain.parentJointIndex[i];
        if (parentJoint < 0) {
            parentBodies = anchorBodies;
        } else if (static_cast<std::size_t>(parentJoint) < jointCount) {
            singleParentBody = lowestBodyOfBone(chain.jointBoneIndices[static_cast<std::size_t>(parentJoint)]);
            if (singleParentBody >= 0) {
                parentBodies = std::span<const std::int32_t>(&singleParentBody, 1);
            }
        }

        if (childBody >= 0) {
            for (const std::int32_t parentBody : parentBodies) {
                if (parentBody == childBody) {
                    continue;
                }
                const auto found = jointsByPair.find({ std::min(parentBody, childBody), std::max(parentBody, childBody) });
                if (found == jointsByPair.end()) {
                    continue;
                }
                for (const std::int32_t jointIndex : found->second) {
                    const float limit = BendLimitFromPmxJoint(physics.joints[static_cast<std::size_t>(jointIndex)]);
                    if (bestJoint < 0 || limit < best || (limit == best && jointIndex < bestJoint)) {
                        best = limit;
                        bestJoint = jointIndex;
                    }
                }
            }
        }

        chain.jointSettings[i].bendLimitRadians = bestJoint >= 0 ? best : kFreeLimit;
        pmxJointIndexPerJoint[i] = bestJoint;
    }
    return pmxJointIndexPerJoint;
}

} // namespace gte
