#pragma once
#include "DynamicChainDefinition.h"
#include "../Assets/PhysicsData.h"
#include "../Assets/SkeletonData.h"

#include <cstdint>
#include <vector>

namespace gte {

// Defaults for the joints of a detected chain; per-joint rigid body data overrides them.
struct DynamicChainDetectionDefaults {
    DynamicJointSettings defaultJointSettings; // damping/stiffness/mass fallback.
    float defaultGravityScale = 1.0f;
    float defaultWindScale = 1.0f;
    std::uint8_t defaultConstraintIterations = 4;
    // Shorter runs are not worth simulating.
    std::size_t minimumChainLength = 2;
    // PMX linear damping d becomes a step fraction of 1 - exp(-d * rate / 60).
    float pmxDampingRatePerSecond = 1.0f;
};

// Diagnostic output next to the chains. Never affects the simulation.
struct DynamicChainDetectionDiagnostics {
    // Dynamic bones that no Static anchor reaches (by joint graph or by skeleton ancestry). Not simulated.
    std::vector<std::int32_t> orphanedDynamicBoneIndices;
    // PMX joint indices whose two ends landed in different chains. Dropped.
    std::vector<std::int32_t> crossChainJointsDropped;
    // Rigid body indices dropped by the "lowest index per bone" tie-break.
    std::vector<std::int32_t> duplicateBoneRigidBodyAssignmentsDropped;
    // Bones of joints that have no PMX joint to their tree parent (sorted). Their bend limit is free.
    std::vector<std::int32_t> bendLimitMissingBoneIndices;
};

struct DynamicChainDetectionResult {
    std::vector<DynamicChainDefinition> chains;
    DynamicChainDetectionDiagnostics diagnostics;
    // Per chain, per joint: index of the PMX joint to the tree parent used for the bend limit (-1 = none).
    std::vector<std::vector<std::int32_t>> pmxJointIndexPerJoint;
};

// Detects dynamic bone chains from the PMX rigid body and joint graph.
// Static bodies anchor a chain; Dynamic and DynamicAndBoneMerge bodies reachable from one become its joints.
// Tree edges follow the real skeleton parents. PMX joints between two joints of a chain that are not tree edges
// become extra constraints. A model without physics data gives an empty result.
// Hub limit: several children of one non-anchor joint share one bone rotation, the last child wins in the resolver.
DynamicChainDetectionResult DetectDynamicChains(
    const SkeletonData& skeleton, const PhysicsData* physics, const DynamicChainDetectionDefaults& defaults);

} // namespace gte
