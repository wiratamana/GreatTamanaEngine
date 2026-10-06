#pragma once

#include "../../ECS/Registry.h"
#include "../../Math/Vec3.h"

namespace gte {

// Resolves the one active sun: first-active-wins, Registry-only.
struct ResolvedDirectionalLight {
    // Normalized direction TOWARD the sun.
    Vec3 directionTowardSun = Vec3::Up();

    // Shader-space sun intensity (color * lux-to-shader-scale).
    Vec3 sunIlluminance = Vec3::One();
};

// First active DirectionalLight wins, full world transform resolved
// (parent included). No active light -> fixed placeholder sun, never throws.
ResolvedDirectionalLight ResolveActiveDirectionalLight(Registry& registry);

} // namespace gte
