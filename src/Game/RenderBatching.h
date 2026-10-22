#pragma once

// Pure batching logic over CollectRenderables() output: no Renderer or GPU dependency.

#include "DrawCommand.h"
#include "../Renderer/Pipeline.h" // VertexLayout

#include <cstddef>
#include <vector>

namespace gte {

// One (MeshHandle, PipelineHandle) group's draw commands, in CollectRenderables() order.
struct RenderBatchGroup {
    MeshHandle mesh;
    PipelineHandle pipeline;
    std::vector<DrawCommand> commands; // Always >= 1.
};

// Groups `commands` by (mesh, pipeline). Stable: first-seen group order, input order inside a group.
std::vector<RenderBatchGroup> GroupDrawCommandsByMeshAndPipeline(const std::vector<DrawCommand>& commands);

// A group needs at least this many instances to be GPU-driven.
inline constexpr std::size_t kMinInstancesForGpuDrivenBatch = 4;

// True when the group is a GPU-driven batch: enough instances, an index buffer, the untextured
// PositionNormal layout, not GPU-skinned and not clustered (clustered meshes use the cluster path).
bool IsGpuDrivenEligible(const RenderBatchGroup& group, bool hasIndexBuffer, VertexLayout vertexLayout,
    bool isGpuSkinned, std::size_t minInstancesForGpuDrivenBatch, bool hasClusters = false);

} // namespace gte
