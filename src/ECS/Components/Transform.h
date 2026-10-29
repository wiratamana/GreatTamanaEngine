#pragma once

#include "../Entity.h"
#include "Math/Mat4.h"
#include "Math/Quat.h"
#include "Math/Vec3.h"

#include <cstdint>

namespace gte {

// Local position/rotation/scale, relative to `parent` (world space when there is none).
// Plain data; hierarchy operations live in ECS/TransformHierarchy.h.
struct Transform {
    Vec3 position = Vec3::Zero();
    Quat rotation = Quat::Identity();
    Vec3 scale = Vec3::One();

    // kInvalidEntity = no parent. Change it only through SetParent()/AssignHierarchy().
    Entity parent = kInvalidEntity;

    // Display order among siblings (same parent); lower first, ties ordered by entity index.
    // Not guaranteed unique or dense until SetSiblingIndex()/AssignHierarchy() normalizes the group.
    std::uint32_t siblingIndex = 0;

    // Local Translate * Rotate * Scale matrix. The world matrix is ComputeWorldMatrix().
    Mat4 LocalToWorldMatrix() const noexcept
    {
        return Mat4::TRS(position, rotation, scale);
    }
};

} // namespace gte
