#pragma once
#include "WindField.h"
#include "../Math/Vec3.h"

namespace gte {

// World-level physics tuning shared by every DynamicChainRig. Owned by PhysicsSystem.
struct GlobalPhysicsSettings {
    // World gravity in model units/s^2 (PMX: 1 unit = 8 cm, MMD uses 98).
    Vec3 gravity = Vec3::Down() * 98.0f;

    WindSettings wind;

    // Fixed step length and the cap of steps per render frame.
    float fixedTimestepSeconds = 1.0f / 120.0f;
    int maxStepsPerFrame = 10;

    // Scale the per-step damping and goal stiffness of every joint.
    float dampingMultiplier = 1.0f;
    float stiffnessMultiplier = 1.0f;
    // Use the raw PMX damping as a per-step fraction.
    bool useRawPmxDamping = false;

    // Global collision switch, ANDed with each chain's own collisionEnabled.
    bool collisionEnabled = true;
    // Use the bind-pose collision radius fit and ignore lists.
    bool useCollisionFit = true;

    // Bend cone around the animated direction: effective = cumulative * scale + minDegrees.
    // The cumulative sum uses a minimum own limit (floor) for every non-root-child joint.
    bool bendLimitsEnabled = true;
    float bendLimitScale = 1.0f;
    float bendLimitMinDegrees = 0.0f;
    // Minimum own bend limit of every non-root-child joint, in degrees.
    float bendJointFloorDegrees = 3.0f;

    // Interpolate the written pose between the last two fixed steps.
    bool interpolateOutput = true;
};

} // namespace gte
