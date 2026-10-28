#pragma once

#include "FeatureKit/ClusterDrawTypes.h"

#include <span>
#include <vector>

namespace gte {

// One draw command plus what Core knows about its mesh and pipeline.
struct ClusterCandidateInput {
    Entity entity;
    fx::MeshToken mesh;
    Mat4 world = Mat4::Identity();
    bool meshHasClusters = false;
    bool meshSynthesizable = false; // No table, but indices and bounds allow a one-cluster table.
    bool meshHasIndexBuffer = false;
    bool pipelineUntextured = false; // Position + normal vertex layout, no material set.
    bool gpuSkinned = false;
};

// True when every entry of `m` is finite.
bool IsFiniteMatrix(const Mat4& m) noexcept;

// Eligible inputs as cluster instances, in input order. Needs a table (or a synthesizable mesh with enough
// instances in the list), an index buffer, an untextured pipeline, no GPU skinning, a finite world matrix and
// `capsPresent`.
std::vector<fx::ClusterInstance> SelectClusterCandidates(std::span<const ClusterCandidateInput> inputs, bool capsPresent);

} // namespace gte
