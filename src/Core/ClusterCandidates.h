#pragma once

#include "FeatureKit/ClusterDrawTypes.h"
#include "../Renderer/Culling/CullingTypes.h"
#include "../Renderer/MeshHandle.h"
#include "../Renderer/PipelineHandle.h"
#include "../Renderer/VertexLayout.h"

#include <optional>
#include <span>
#include <unordered_map>
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

// What Core knows about the mesh and pipeline of a draw command; equal for every command with the same pair.
struct ClusterCandidateFacts {
    bool meshHasClusters = false;
    bool meshSynthesizable = false; // No table, but indices and bounds allow a one-cluster table.
    bool meshHasIndexBuffer = false;
    VertexLayout layout = VertexLayout::None;
    bool gpuSkinned = false;

    bool operator==(const ClusterCandidateFacts&) const noexcept = default;
};

// What a candidate list read from one (mesh, pipeline) pair of the render list.
struct ClusterCandidatePair {
    MeshHandle mesh;
    PipelineHandle pipeline;
    bool resolved = false; // Both handles resolve to a live mesh and pipeline.
    ClusterCandidateFacts facts;
    std::optional<AABB> clusterBounds; // World boxes of the candidates come from these.

    // True when `other` is the same pair with equal facts and bit-equal bounds.
    bool SameAs(const ClusterCandidatePair& other) const noexcept;
};

// What a candidate list depends on besides the render list. While both are unchanged the old list is exact.
struct ClusterCandidateReuse {
    static constexpr std::size_t kMaxPairs = 32; // More distinct pairs than this: no reuse.

    bool valid = false;
    std::uint64_t renderListEpoch = 0;
    fx::ClusterRejectCounts rejections;
    std::vector<ClusterCandidatePair> pairs;

    // Records `pair` once; false when the pair limit is exceeded (the reuse is then dropped).
    bool Note(const ClusterCandidatePair& pair);
};

// Builds one frame's candidate list command by command; the result equals SelectClusterCandidates() on the same input.
class ClusterCandidateBuilder {
public:
    // `out` is cleared and keeps its capacity; `expectedCount` is a reserve hint.
    ClusterCandidateBuilder(std::vector<fx::ClusterInstance>& out, std::size_t expectedCount);

    void Add(const ClusterCandidateFacts& facts, Entity entity, fx::MeshToken mesh, fx::MaterialToken material,
        const Mat4& world);

    // Drops synthesized-table instances of meshes with too few instances. Call once, after the last Add().
    void Finish(fx::ClusterRejectCounts* rejected);

private:
    std::vector<fx::ClusterInstance>& m_out;
    std::vector<std::uint32_t> m_synthesized; // Indices in `m_out` of instances without a cluster table.
    std::unordered_map<fx::MeshToken, std::size_t, fx::MeshTokenHash> m_synthesizedCounts;
    fx::ClusterRejectCounts m_counts;
    float m_basis[9] = {}; // Columns 0..2 (x, y, z) of the last instance's world matrix.
    bool m_haveBasis = false;
    std::uint32_t m_basisFlags = 0;
    float m_basisMaxScale = 0.0f;
};

// Eligible inputs as cluster instances, in input order: supported layout, index buffer, no GPU skinning,
// finite world matrix, `capsPresent`, and (textured) a material plus table. Rejects are counted in `rejected`.
std::vector<fx::ClusterInstance> SelectClusterCandidates(
    std::span<const ClusterCandidateInput> inputs, bool capsPresent, fx::ClusterRejectCounts* rejected = nullptr);

} // namespace gte
