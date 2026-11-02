#include "JointCollisionFit.h"

#include "CollisionFilter.h"
#include "VerletParticle.h"
#include "../Math/Vec3.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <limits>

namespace gte {

namespace {

constexpr float kPushEpsilon = 1e-5f;
constexpr int kSearchIterations = 12;

} // namespace

float ProbeCollisionPush(const Vec3& position, float radius, const Collider& collider) noexcept
{
    VerletParticle probe;
    probe.position = position;
    probe.previousPosition = position;
    probe.inverseMass = 1.0f;
    probe.collisionRadius = radius;
    SolveCollision(probe, collider);
    const float moved = Length(probe.position - position);
    return moved > kPushEpsilon ? moved : 0.0f;
}

void FitJointCollisionToBindPose(
    DynamicChainDefinition& chain, std::span<const Vec3> bindJointPositions, std::span<const Collider> bindColliders)
{
    const std::size_t jointCount = chain.jointSettings.size();
    if (bindJointPositions.size() != jointCount) {
        return;
    }
    const std::size_t colliderCount = std::min<std::size_t>(bindColliders.size(), std::numeric_limits<std::uint16_t>::max());

    for (std::size_t i = 0; i < jointCount; ++i) {
        DynamicJointSettings& joint = chain.jointSettings[i];
        joint.collisionFitRadius = -1.0f;
        joint.ignoredColliderIndices.clear();

        const Vec3& position = bindJointPositions[i];
        const float fullRadius = std::max(joint.collisionRadius, 0.0f);
        float fitted = fullRadius;
        bool anyFit = false;

        for (std::size_t k = 0; k < colliderCount; ++k) {
            const Collider& collider = bindColliders[k];
            if (!GroupsMayCollide(joint.group, joint.collisionMask, collider.group, collider.collisionMask)) {
                continue;
            }
            if (ProbeCollisionPush(position, fullRadius, collider) == 0.0f) {
                continue;
            }
            if (ProbeCollisionPush(position, 0.0f, collider) > 0.0f) {
                joint.ignoredColliderIndices.push_back(static_cast<std::uint16_t>(k));
                continue;
            }
            float low = 0.0f; // never pushed.
            float high = fullRadius; // pushed.
            for (int iteration = 0; iteration < kSearchIterations; ++iteration) {
                const float mid = 0.5f * (low + high);
                if (ProbeCollisionPush(position, mid, collider) > 0.0f) {
                    high = mid;
                } else {
                    low = mid;
                }
            }
            fitted = std::min(fitted, low);
            anyFit = true;
        }

        if (anyFit) {
            joint.collisionFitRadius = fitted;
        }
    }
}

} // namespace gte
