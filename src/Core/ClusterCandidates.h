#pragma once

#include "FeatureKit/ClusterDrawTypes.h"
#include "../Renderer/VertexLayout.h"

#include <optional>
#include <span>
#include <vector>

namespace gte {

// One draw command plus what Core knows about its mesh and pipeline.
struct ClusterCandidateInput {
    Entity entity;
    fx::MeshToken mesh;
    fx::MaterialToken material; // The draw command's material; invalid when it has none.
    Mat4 world = Mat4::Identity();
    bool meshHasClusters = false;
    bool meshSynthesizable = false; // No table, but indices and bounds allow a one-cluster table.
    bool meshHasIndexBuffer = false;
    VertexLayout layout = VertexLayout::None; // Vertex layout of the draw command's pipeline.
    bool gpuSkinned = false;
};

// The material kind a vertex layout draws with, or nullopt when the layout is outside the cluster path.
constexpr std::optional<ClusterMaterialKind> ClusterMaterialKindOf(VertexLayout layout) noexcept
{
    switch (layout) {
    case VertexLayout::PositionNormal: return ClusterMaterialKind::None;
    case VertexLayout::PositionNormalUv: return ClusterMaterialKind::Textured;
    case VertexLayout::PositionNormalUvTangent: return ClusterMaterialKind::Pbr;
    case VertexLayout::PositionColor:
    case VertexLayout::None: break;
    }
    return std::nullopt;
}

// True when every entry of `m` is finite.
bool IsFiniteMatrix(const Mat4& m) noexcept;

// Eligible inputs as cluster instances, in input order. Needs a supported vertex layout (a textured one also needs a
// material and its own cluster table; only an untextured mesh may get a synthesized table, and then only with enough
// instances in the list), an index buffer, no GPU skinning, a finite world matrix and `capsPresent`.
// Every rejected input is counted once in `rejected` when it is given.
std::vector<fx::ClusterInstance> SelectClusterCandidates(
    std::span<const ClusterCandidateInput> inputs, bool capsPresent, fx::ClusterRejectCounts* rejected = nullptr);

} // namespace gte
