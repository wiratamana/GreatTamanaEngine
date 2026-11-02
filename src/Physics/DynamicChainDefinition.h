#pragma once
#include "../Math/MathTypes.h"

#include <cstddef>
#include <cstdint>
#include <vector>

namespace gte {

// An effective bend limit at or below this means the joint is welded to the animated pose.
inline constexpr float kWeldLimitRadians = DegToRad(1.0f);
constexpr bool IsWeldedLimit(float limitRadians) noexcept { return limitRadians <= kWeldLimitRadians; }

// Per-joint tuning of one simulated bone.
struct DynamicJointSettings {
    // Fraction of velocity removed per 1/60 s step (0..1).
    float damping = 0.4f;
    // Fraction of the pull toward the animated pose per 1/60 s step (0..1).
    float stiffness = 0.02f;
    // Fed to VerletParticle::inverseMass (must be > 0).
    float mass = 1.0f;

    // PMX collision group and mask of the matched rigid body. Always mask `group` with 0x0F before shifting.
    std::uint8_t group = 0;
    std::uint16_t collisionMask = 0xFFFF;

    // Collision radius derived from the matched rigid body shape (0 = point).
    float collisionRadius = 0.0f;

    // Raw PMX linear damping of the matched rigid body (-1 = not from PMX).
    float pmxLinearDamping = -1.0f;

    // Largest collision radius with no push in the bind pose (-1 = not fitted, use collisionRadius).
    float collisionFitRadius = -1.0f;
    // Sorted indices into the model collider list that this joint never collides with.
    std::vector<std::uint16_t> ignoredColliderIndices;

    // Own bend limit in radians, from the PMX joint to the tree parent (pi = free).
    float bendLimitRadians = kPi;
};

// Distance constraint between two joints of one chain that is not a tree edge. Indices are positions in jointBoneIndices.
struct ExtraStructuralConstraint {
    std::int32_t jointIndexA = -1;
    std::int32_t jointIndexB = -1;
    float restLength = 0.0f; // bind-pose distance.
};

// One tree of simulated bones hanging from a pinned anchor bone.
// The anchor (rootBoneIndex) is never written by physics.
struct DynamicChainDefinition {
    std::int32_t rootBoneIndex = -1;
    std::vector<std::int32_t> jointBoneIndices;
    std::vector<DynamicJointSettings> jointSettings; // index-aligned with jointBoneIndices.

    // Tree parent per joint as a position in jointBoneIndices, -1 = the anchor.
    // INVARIANT: parentJointIndex[i] < i, and it is a real skeleton parent/child pair.
    std::vector<std::int32_t> parentJointIndex;

    // Brace constraints that are not tree edges (may be empty).
    std::vector<ExtraStructuralConstraint> extraConstraints;

    // Bind-pose distance to the tree parent (the anchor for root children), index-aligned.
    std::vector<float> restLengths;

    float gravityScale = 1.0f;
    float windScale = 1.0f;
    std::uint8_t constraintIterations = 4; // rod relaxation passes per fixed step.

    // Collide every joint against the model collider list.
    bool collisionEnabled = true;

    // A root move larger than this in one call re-seeds the chain (teleport guard).
    float maxPlausibleRootDelta = 10.0f;

    // Returns { -1, 0, 1, ..., jointCount - 2 }: a single strand.
    static std::vector<std::int32_t> MakeLinearParentIndices(std::size_t jointCount);
};

// Result of FindDynamicChainJointByBoneIndex(); { -1, -1 } means no match.
struct DynamicChainJointLocation {
    std::int32_t chainIndex = -1;
    std::int32_t jointIndexInChain = -1;

    bool IsValid() const noexcept { return chainIndex >= 0 && jointIndexInChain >= 0; }
};

// Finds the chain and position whose jointBoneIndices contain `boneIndex`. Anchors never match.
DynamicChainJointLocation FindDynamicChainJointByBoneIndex(
    const std::vector<DynamicChainDefinition>& chains, std::int32_t boneIndex);

} // namespace gte
