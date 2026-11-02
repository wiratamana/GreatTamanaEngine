#pragma once
#include "VerletParticle.h"
#include "../Math/Mat4.h"
#include "../Math/Vec3.h"

#include <cstdint>
#include <vector>

namespace gte {

// Per-instance simulation memory of one DynamicChainDefinition (never shared between entities).
struct DynamicChainRuntimeState {
    // Index-aligned with DynamicChainDefinition::jointBoneIndices; seeded on the first step.
    std::vector<VerletParticle> particles;
    bool initialized = false;

    // Simulation clock in seconds; drives the wind phase.
    float simulationTimeSeconds = 0.0f;

    // Root position of the last call, for the teleport guard.
    Vec3 lastRootWorldPosition = Vec3::Zero();

    // Re-seeds after the first seed since the last reset.
    std::uint32_t reseedCount = 0;

    // Display knots in the anchor frame: each is the mean of two consecutive fixed steps, which removes step-rate jitter.
    // The two latest knots are kept; lastStepLocal is the latest raw step.
    std::vector<Vec3> prevStepLocal;
    std::vector<Vec3> currStepLocal;
    std::vector<Vec3> lastStepLocal;

    // Animated joint positions in the anchor frame, and the anchor matrix, of the previous render frame (empty = none yet).
    std::vector<Vec3> prevAnimatedLocal;
    Mat4 prevAnchorWorld = Mat4::Identity();

    // Scratch reused every step so the solver never allocates after the first call.
    std::vector<float> scratchLimits;
    std::vector<float> scratchPathLength;
    std::vector<float> scratchPushDistance;
    std::vector<Vec3> scratchPositions;
    std::vector<Vec3> scratchTargets;
    std::vector<Vec3> scratchAnimatedLocal;
};

} // namespace gte
