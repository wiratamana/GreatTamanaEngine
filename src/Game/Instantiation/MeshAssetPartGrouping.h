#pragma once

#include "MeshAssetGpuCatalog.h"
#include "../RenderSystem.h"

#include <cstddef>
#include <vector>

namespace gte {

// One distinct, shared underlying GPU vertex buffer among a model's own
// MeshAssetParts (see MeshAssetGpuCatalog.h's own header comment on why
// several parts - e.g. every textured-material submesh - can legitimately
// point at the exact SAME physical vertex buffer, differing only in their
// own index buffer/range: Stage 1 of
// task_manager/optimizing_multi_thread_cpu_skinning/
// MULTITHREAD_CPU_SKINNING_OPTIMIZATION_STRATEGY_v1.md).
struct MeshAssetPartGroup {
    // Mesh::VertexBufferIdentity() - the cheap, opaque identity token every
    // part sharing this group's underlying vertex buffer returns the exact
    // same value for.
    const void* vertexBufferIdentity = nullptr;

    // Any one of this group's own parts' live Mesh* - used to read the
    // shared vertex buffer's own VertexCount()/etc. Never null for a group
    // actually present in the returned list.
    Mesh* representativeMesh = nullptr;

    // Whether every part in this group was built with a valid
    // MeshAssetPart::texture (the textured PositionNormalUv layout) as
    // opposed to the untextured PositionNormal layout - matches
    // MeshAssetGpuCatalog.cpp's own "one shared buffer per {untextured,
    // textured}" split, so this is uniform across every part folded into
    // one group by construction (see GroupMeshAssetPartsBySharedVertexBuffer()
    // below).
    bool textured = false;

    // Indices into the ORIGINAL `parts` vector passed to
    // GroupMeshAssetPartsBySharedVertexBuffer(), in declaration order, of
    // every MeshAssetPart that belongs to this group - what the GPU Vertex
    // Skinning campaign's Phase 4 (GpuSkinningRigCache, see
    // src/Game/Animation/GpuSkinningRigCache.h) needs to build one
    // GPU-skinned Mesh PER PART (each with its own index buffer/range)
    // while still sharing exactly one GPU skinning output buffer per
    // group - the CPU path (AnimationSystem::Update()) only needs
    // `representativeMesh`/`textured` above and can ignore this field.
    std::vector<std::size_t> partIndices;
};

// Groups `parts` by their own live Mesh's VertexBufferIdentity() - the
// single, shared home for logic that used to be hand-duplicated inline
// inside AnimationSystem::Update() (see
// task_manager/gpu_skinning/GPU_SKINNING_PHASE4_PER_MODEL_RESOURCE_MANAGEMENT_STRATEGY_v1.md,
// Step 3.3: "extract it into a small shared free function... so the two
// never drift into two independently-maintained copies of the same
// grouping logic"). A part whose MeshHandle no longer resolves against
// `renderSystem` (e.g. a stale/unloaded mesh) is silently skipped, mirroring
// every other "best-effort against whatever is currently loaded" handle
// resolution in this engine (see RenderSystem::Draw()'s own doc comment).
//
// Genuinely needs a live RenderSystem/Mesh - Tier 2, no automated coverage
// yet (see AGENTS.md, "Testability & Regression Safety") - same bucket as
// RenderSystem's own non-pure methods.
std::vector<MeshAssetPartGroup> GroupMeshAssetPartsBySharedVertexBuffer(
    RenderSystem& renderSystem, const std::vector<MeshAssetPart>& parts);

} // namespace gte
