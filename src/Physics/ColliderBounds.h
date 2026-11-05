#pragma once
#include "Collider.h"
#include "../Math/MathTypes.h"
#include "../Math/Vec3.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <limits>
#include <vector>

namespace gte {

// Conservative reach of one collider: a particle of radius r that is farther than bound + perRadius * r from `center`
// can never be moved by it, so the exact solver is skipped. NaN or infinite values never reject.
struct ColliderReach {
    Vec3 center = Vec3::Zero();
    float bound = std::numeric_limits<float>::infinity();
    float perRadius = 0.0f;
};

// Padding for float rounding; the reject test only has to be safe, never tight.
inline float PadReach(float value) noexcept
{
    return value * 1.001f + 1.0e-3f;
}

inline ColliderReach ComputeColliderReach(const Collider& collider) noexcept
{
    ColliderReach reach;
    reach.center = collider.center;
    switch (collider.shape) {
    case ColliderShape::Sphere:
        reach.bound = PadReach(std::max(0.0f, collider.size.x));
        reach.perRadius = 1.001f;
        break;
    case ColliderShape::Capsule: {
        // Same segment end points as SolveCapsuleCollision, so the bound is exact for any rotation.
        const float halfHeight = std::max(0.0f, collider.size.y) * 0.5f;
        const Vec3 axis = collider.rotation.RotateVector(Vec3::Up());
        const Vec3 segStart = collider.center - axis * halfHeight;
        const Vec3 segEnd = collider.center + axis * halfHeight;
        const float reachAlongAxis = std::max(Length(segStart - collider.center), Length(segEnd - collider.center));
        reach.bound = PadReach(reachAlongAxis + std::max(0.0f, collider.size.x));
        reach.perRadius = 1.001f;
        break;
    }
    case ColliderShape::Box: {
        // The box test runs in rotated space; only a near-unit rotation keeps lengths, otherwise never reject.
        const Quat& q = collider.rotation;
        const float lengthSq = q.x * q.x + q.y * q.y + q.z * q.z + q.w * q.w;
        if (std::fabs(lengthSq - 1.0f) <= 1.0e-2f) {
            const Vec3 half(std::max(0.0f, collider.size.x), std::max(0.0f, collider.size.y), std::max(0.0f, collider.size.z));
            reach.bound = 1.12f * Length(half) + 1.0e-2f;
            reach.perRadius = 1.12f * 1.7320509f;
        }
        break;
    }
    }
    return reach;
}

// The reaches of a collider list as five float lanes (x, y, z, bound, perRadius), padded to a multiple of 4 so four colliders
// can be tested with one SSE2 compare. Padding lanes sit far away and never pass.
struct ColliderReachSet {
    std::size_t count = 0;
    std::size_t padded = 0;
    std::vector<float> lanes;

    const float* X() const noexcept { return lanes.data(); }
    const float* Y() const noexcept { return lanes.data() + padded; }
    const float* Z() const noexcept { return lanes.data() + 2 * padded; }
    const float* Bound() const noexcept { return lanes.data() + 3 * padded; }
    const float* PerRadius() const noexcept { return lanes.data() + 4 * padded; }
};

inline void ComputeColliderReach(const std::vector<Collider>& colliders, ColliderReachSet& out)
{
    out.count = colliders.size();
    out.padded = (colliders.size() + 3) & ~static_cast<std::size_t>(3);
    constexpr float kFar = 3.0e18f;
    out.lanes.assign(5 * out.padded, 0.0f);
    for (std::size_t i = 0; i < out.padded; ++i) {
        out.lanes[i] = kFar;
        out.lanes[out.padded + i] = kFar;
        out.lanes[2 * out.padded + i] = kFar;
    }
    for (std::size_t i = 0; i < colliders.size(); ++i) {
        const ColliderReach reach = ComputeColliderReach(colliders[i]);
        out.lanes[i] = reach.center.x;
        out.lanes[out.padded + i] = reach.center.y;
        out.lanes[2 * out.padded + i] = reach.center.z;
        out.lanes[3 * out.padded + i] = reach.bound;
        out.lanes[4 * out.padded + i] = reach.perRadius;
    }
}

} // namespace gte
