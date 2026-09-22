#pragma once

// ============================================================================
// GPU-Driven Frustum Culling + Indirect Draw (render-pass-5) - PHASE4:
// Per-Batch Resource Management and Batching
// ============================================================================
// See task_manager/render-pass-5/PHASE4_PER_BATCH_RESOURCE_MANAGEMENT_AND_BATCHING.md
// for the full design reasoning. RenderSystem's pure, Tier-1-testable
// batching/grouping logic: grouping CollectRenderables()'s own
// std::vector<DrawCommand> output by (MeshHandle, PipelineHandle), and
// deciding which groups are "GPU-driven-eligible batches" per PHASE0's
// Locked Design Decision 7. Pure, no Renderer/live-GPU dependency at all -
// the same "operates on plain DrawCommand data only" property
// RenderSystem::CollectRenderables() itself already has (see
// tests/Game/RenderBatchingTests.cpp).
//
// Builds NO render-graph pass declarations and issues NO dispatch/indirect
// draw of any kind - PHASE5's job. Does NOT modify RenderSystem::Draw()'s
// own existing per-entity loop behavior at all.

#include "DrawCommand.h"
#include "../Renderer/Pipeline.h" // VertexLayout

#include <cstddef>
#include <vector>

namespace gte {

// One (MeshHandle, PipelineHandle) group's worth of DrawCommands, in the
// same relative order CollectRenderables() produced them.
struct RenderBatchGroup {
    MeshHandle mesh;
    PipelineHandle pipeline;
    std::vector<DrawCommand> commands; // ALWAYS >= 1 - see GroupDrawCommandsByMeshAndPipeline() below.
};

// Groups `commands` (CollectRenderables()'s own output) by (mesh, pipeline) -
// stable, preserving first-seen group order and each group's own internal
// command order (so output is deterministic/diffable across frames for the
// SAME input, a real testability requirement). Pure, no Renderer/live-GPU
// dependency - the same "operates on plain DrawCommand data only" property
// CollectRenderables() itself already has.
std::vector<RenderBatchGroup> GroupDrawCommandsByMeshAndPipeline(const std::vector<DrawCommand>& commands);

// Locked Design Decision 7, PHASE0_MASTER_STRATEGY.md - tunable, named
// threshold: a (MeshHandle, PipelineHandle) group must have AT LEAST this
// many instances to be GPU-driven-eligible.
inline constexpr std::size_t kMinInstancesForGpuDrivenBatch = 4;

// Locked Design Decision 7 (PHASE0) - decides whether ONE group is
// GPU-driven-eligible:
//   (a) group.commands.size() >= minInstancesForGpuDrivenBatch.
//   (b) hasIndexBuffer is true (the group's shared Mesh::HasIndexBuffer()).
//   (c) vertexLayout is EXACTLY VertexLayout::PositionNormal (untextured,
//       non-instanced - the group's shared Pipeline's own vertex layout;
//       PHASE4/5 resolve such a group's Pipeline/Mesh pair onto the NEW
//       PositionNormalInstanced pipeline/shader instead, never mutating the
//       original).
//   (d) isGpuSkinned is false (the group's shared MeshHandle is NOT part of
//       this frame's GPU-skinning output-buffer set - see
//       RenderSystem::CollectGpuDrivenBatches() for the real, final
//       cross-reference mechanism this resolves to).
//
// `hasIndexBuffer`/`vertexLayout` describe the group's shared Mesh/Pipeline
// (resolved by the CALLER, which has live access - see
// RenderSystem::CollectGpuDrivenBatches()); `isGpuSkinned` is likewise
// resolved by the caller. Pure decision, no live GPU/Renderer state read
// directly by this function itself.
bool IsGpuDrivenEligible(const RenderBatchGroup& group, bool hasIndexBuffer, VertexLayout vertexLayout,
    bool isGpuSkinned, std::size_t minInstancesForGpuDrivenBatch);

} // namespace gte
