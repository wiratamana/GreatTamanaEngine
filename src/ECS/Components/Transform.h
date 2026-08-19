#pragma once

#include "Math/Mat4.h"
#include "Math/Quat.h"
#include "Math/Vec3.h"

namespace gte {

// The engine's first real component (see AGENTS.md, "Entity-Component-
// System") - plain position/rotation/scale data, no parent-hierarchy field
// yet (that's a natural follow-up once something actually needs nested
// transforms - a Scene/Hierarchy panel most likely; until then every
// Transform is implicitly in world space). Deliberately a plain struct with
// no behavior beyond the one pure-math helper below, same spirit as every
// gte::Math type (Vec3/Quat/Mat4) it's built from.
struct Transform {
    Vec3 position = Vec3::Zero();
    Quat rotation = Quat::Identity();
    Vec3 scale = Vec3::One();

    // Translate * Rotate * Scale, via Mat4::TRS() - see Mat4.h for the
    // exact composition order/convention this engine uses.
    Mat4 LocalToWorldMatrix() const noexcept
    {
        return Mat4::TRS(position, rotation, scale);
    }
};

} // namespace gte
