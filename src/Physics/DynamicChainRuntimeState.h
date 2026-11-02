#pragma once
#include "VerletParticle.h"
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

    // Scratch for the effective bend limits (reused every step).
    std::vector<float> scratchLimits;

    // Particle positions in the anchor frame after the previous and the last fixed step.
    std::vector<Vec3> prevStepLocal;
    std::vector<Vec3> currStepLocal;
};

} // namespace gte
