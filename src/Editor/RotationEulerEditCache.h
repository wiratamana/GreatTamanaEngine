#pragma once

#include "../ECS/Entity.h"
#include "../Math/Quat.h"
#include "../Math/Vec3.h"

#include <algorithm>
#include <cmath>

namespace gte {

// Keeps the Euler angles a user is editing so they are not rebuilt from the quaternion
// every frame (which folds pitch into [-90, 90] and flips yaw/roll). No ImGui dependency.
struct RotationEulerEditCache {
    // Largest accepted Euler component in degrees; keeps sin/cos finite.
    static constexpr float kMaxDegrees = 100000.0f;

    Entity entity = kInvalidEntity;
    Vec3 euler{};
    Quat written{};
    bool valid = false;

    // Euler to display. Re-derived when the entity changed or the quaternion was
    // modified by anything other than Commit() (bit-exact compare on purpose).
    Vec3 Resolve(Entity e, const Quat& current) noexcept
    {
        const bool sameQuat = current.x == written.x && current.y == written.y &&
                              current.z == written.z && current.w == written.w;
        if (!valid || e != entity || !sameQuat) {
            euler = current.ToEulerDegrees();
            entity = e;
            valid = true;
            written = current;
        }
        return euler;
    }

    // Stores the edited Euler and returns the quaternion to write to the Transform.
    Quat Commit(Vec3 edited) noexcept
    {
        edited.x = Sanitize(edited.x);
        edited.y = Sanitize(edited.y);
        edited.z = Sanitize(edited.z);
        euler = edited;
        written = Quat::FromEulerDegrees(edited.x, edited.y, edited.z);
        valid = true;
        return written;
    }

    void Invalidate() noexcept { valid = false; }

private:
    static float Sanitize(float v) noexcept
    {
        return std::isfinite(v) ? std::clamp(v, -kMaxDegrees, kMaxDegrees) : 0.0f;
    }
};

} // namespace gte
