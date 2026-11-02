#pragma once
#include "Collider.h"
#include "DynamicChainDefinition.h"
#include "DynamicChainRuntimeState.h"
#include "WindField.h"
#include "../Math/Vec3.h"

#include <cstdint>
#include <vector>

namespace gte {

// Per-call counters read by the physics debug toolbox. All zero when nothing happened.
struct StepDiagnostics {
    std::uint32_t collisionPushedParticles = 0;
    float collisionPushDistanceSum = 0.0f;
    std::uint32_t bendClampedJoints = 0;
    std::uint32_t reseeds = 0; // re-seeds after the first seed.
    std::uint32_t nanGuardHits = 0;
};

// Optional per-call extras. The defaults give the plain behavior.
struct StepOptions {
    StepDiagnostics* diagnostics = nullptr;
    float dampingMultiplier = 1.0f;
    float stiffnessMultiplier = 1.0f;
    bool useRawPmxDamping = false; // use pmxLinearDamping as a per-step fraction.
    bool useCollisionFit = true;   // use collisionFitRadius and ignoredColliderIndices.
    bool bendLimitsEnabled = false;
    float bendLimitScale = 1.0f;
    float bendLimitMinRadians = 0.0f;
};

// Effective bend limit per joint in radians (pi = free):
// clamp(cumulativeOwnLimits * scale + minRadians, 0, pi). All pi when limits are disabled.
void ComputeEffectiveBendLimits(const DynamicChainDefinition& definition, const StepOptions& options, std::vector<float>& out);
std::vector<float> ComputeEffectiveBendLimits(const DynamicChainDefinition& definition, const StepOptions& options);

// Advances one chain by one fixed step.
// Root and animated positions are world space (animated pose, no physics). `colliders` are world space.
// Order: seed, integrate (step-length damping), rods + extra constraints + bend cone,
// goal pull, collision, NaN guard. Does nothing when the definition arrays disagree in size.
void StepDynamicChain(const DynamicChainDefinition& definition, const Vec3& rootWorldPosition,
    const std::vector<Vec3>& animatedJointWorldPositions, DynamicChainRuntimeState& state, float fixedDeltaTime,
    const Vec3& gravity, const WindSettings& wind, const std::vector<Collider>& colliders = {},
    const StepOptions& options = {});

} // namespace gte
