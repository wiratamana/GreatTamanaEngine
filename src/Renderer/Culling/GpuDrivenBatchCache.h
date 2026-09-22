#pragma once

// ============================================================================
// GPU-Driven Frustum Culling + Indirect Draw (render-pass-5) - PHASE4:
// Per-Batch Resource Management and Batching
// ============================================================================
// See task_manager/render-pass-5/PHASE4_PER_BATCH_RESOURCE_MANAGEMENT_AND_BATCHING.md
// for the full design reasoning, and
// task_manager/gpu_skinning/GPU_SKINNING_PHASE4_PER_MODEL_RESOURCE_MANAGEMENT_STRATEGY_v1.md
// (src/Game/Animation/GpuSkinningRigCache.h) for the precedent this class
// mirrors: a small, explicitly-owned cache of PERSISTENT (created once,
// reused/resized across frames, never recreated every frame) GPU buffers,
// keyed by identity, "reallocate only when the thing's own size grows"
// lifecycle.
//
// Deliberately kept Renderer-layer-clean (AGENTS.md, "Clean Architecture" -
// lower-level layers must not depend on higher-level ones): this class only
// ever touches MeshHandle/PipelineHandle (both plain Renderer-layer
// identifiers, see MeshHandle.h/PipelineHandle.h) and already-packed
// GpuCullingInstanceInput values (CullingTypes.h) - never a
// RenderBatchGroup/DrawCommand/Registry/Entity (all Game/ECS-layer concepts,
// see src/Game/RenderBatching.h). Resolving a batch's live Transform/Mesh
// bounds into a std::vector<GpuCullingInstanceInput> is therefore
// RenderSystem's own job (RenderSystem is explicitly allowed to depend on
// both ECS and Renderer - see AGENTS.md, "Entity-Component-System") - see
// RenderSystem::CollectGpuDrivenBatches() (src/Game/RenderSystem.h/.cpp).
//
// This class builds NO render-graph pass declarations and issues NO
// vkCmdDispatch/indirect draw of any kind - PHASE5's job. It only owns the
// raw Buffer objects themselves; PHASE5 imports them into a fresh
// RenderGraphBuilder every frame via ImportBuffer(), mirroring
// AddGpuSkinningPasses()'s own builder.ImportBuffer(request.name,
// request.outputBuffer, request.outputBufferSize) call exactly.

#include "CullingPipelines.h"
#include "CullingTypes.h"
#include "../Buffer.h"
#include "../MeshHandle.h"
#include "../PipelineHandle.h"

#include <cstddef>
#include <map>
#include <vector>

namespace gte {

class Renderer;

// Identifies one GPU-driven batch - a single (MeshHandle, PipelineHandle)
// pair. Deliberately no view/camera dimension (PHASE0's Locked Design
// Decision 11 - this whole campaign's GPU-driven cutover is GAME VIEW ONLY,
// so this cache is never asked to serve two different cameras' worth of
// culling results for the same batch at once - see this campaign's
// PHASE0_MASTER_STRATEGY.md for the full reasoning. If a future campaign
// ever extends batching to Scene View too, this key must grow a view
// dimension first - do not silently assume this cache generalizes to
// multiple concurrently-active views).
struct GpuDrivenBatchKey {
    MeshHandle mesh;
    PipelineHandle pipeline;

    friend bool operator==(const GpuDrivenBatchKey& a, const GpuDrivenBatchKey& b) noexcept
    {
        return a.mesh == b.mesh && a.pipeline == b.pipeline;
    }
    friend bool operator!=(const GpuDrivenBatchKey& a, const GpuDrivenBatchKey& b) noexcept { return !(a == b); }

    // Arbitrary but total/deterministic ordering - lets this key be used
    // directly as a std::map key with no separate hash functor needed (a
    // std::unordered_map would work too, but would need a hand-written
    // std::hash specialization or a local hash functor for no real benefit
    // at this cache's expected scale - a handful of distinct batches per
    // frame, not thousands).
    friend bool operator<(const GpuDrivenBatchKey& a, const GpuDrivenBatchKey& b) noexcept
    {
        if (a.mesh.index != b.mesh.index) {
            return a.mesh.index < b.mesh.index;
        }
        if (a.mesh.generation != b.mesh.generation) {
            return a.mesh.generation < b.mesh.generation;
        }
        if (a.pipeline.index != b.pipeline.index) {
            return a.pipeline.index < b.pipeline.index;
        }
        return a.pipeline.generation < b.pipeline.generation;
    }
};

// Owns, per distinct GpuDrivenBatchKey:
//   - inputBuffer: BufferMemoryUsage::CpuToGpu, sized for `capacity`
//     GpuCullingInstanceInput entries - re-written from the CPU every frame
//     via PackThisFrame()'s own Buffer::Upload() call (host-visible/
//     persistently-mapped, NOT a device-local buffer needing a staging
//     upload each frame - mirrors CreateSkinnedMesh()'s own host-visible
//     vertex buffer precedent for the exact same "written by the CPU every
//     frame" reason).
//   - indirectCommandBuffer: BufferMemoryUsage::GpuOnly, sized for
//     `capacity` IndirectDrawCommand entries (the WORST CASE - every
//     instance survives culling - regardless of which useCompaction mode a
//     future dispatch selects, since a compacted array can never need MORE
//     slots than the input has instances), extraUsage =
//     VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT (the real Vulkan enumerator name -
//     see PHASE3_COMPLETION_REPORT.md's own flagged documentation-only typo
//     correction: PHASE3/PHASE4's own strategy docs wrote
//     "VK_BUFFER_USAGE_INDIRECT_COMMAND_BIT", which does not exist).
//   - countBuffer: BufferMemoryUsage::GpuOnly, one std::uint32_t, BOTH
//     VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT (required by
//     vkCmdDrawIndexedIndirectCount's own countBuffer argument) AND
//     VK_BUFFER_USAGE_STORAGE_BUFFER_BIT (needed for Shaders/FrustumCull.comp's
//     own atomicAdd/write access to it as a plain SSBO) - both flags
//     together, on the SAME buffer, simultaneously, is legal Vulkan (see
//     PHASE3_STRATEGY_DOUBLE_CHECK_REPORT.md's own resolution of this exact
//     question) - CreateStructuredBuffer() already ORs in
//     VK_BUFFER_USAGE_STORAGE_BUFFER_BIT unconditionally, so only
//     extraUsage needs to be passed explicitly. This class does NOT reset
//     this buffer to zero itself anywhere - PHASE5 declares the real
//     vkCmdFillBuffer reset pass every frame, immediately before dispatching
//     Shaders/FrustumCull.comp against it (see
//     PHASE3_FRUSTUM_CULL_COMPUTE_SHADER_AND_PIPELINE.md, Section 3.2, for
//     why the shader can never safely do this on its own).
//
// EnsureCapacity()/PackThisFrame() are the only two ways this cache's state
// changes - both are safe to call every frame; EnsureCapacity() only ever
// reallocates when `instanceCount` GREW past what's currently allocated for
// that key (mirrors RenderGraphResourcePool's own "reuse if it already
// matches, don't reallocate needlessly" philosophy, applied to a persistent,
// NOT render-graph-pooled, resource this time).
class GpuDrivenBatchCache {
public:
    GpuDrivenBatchCache() = default;

    GpuDrivenBatchCache(const GpuDrivenBatchCache&) = delete;
    GpuDrivenBatchCache& operator=(const GpuDrivenBatchCache&) = delete;
    // Not move-enabled - same "constructed once, held by reference/pointer"
    // convention as CullingPipelines (this class owns one internally - see
    // Pipelines() below) and GpuResourceFactory/Renderer themselves.
    GpuDrivenBatchCache(GpuDrivenBatchCache&&) = delete;
    GpuDrivenBatchCache& operator=(GpuDrivenBatchCache&&) = delete;

    struct Entry {
        Buffer inputBuffer;
        Buffer indirectCommandBuffer;
        Buffer countBuffer;
        std::size_t capacity = 0; // Instances currently allocated for - see EnsureCapacity().
    };

    // Reallocates (grows only - never shrinks, and never reallocates at all
    // if `instanceCount <= ` the currently-allocated capacity for this key)
    // this key's three persistent buffers so they can hold at least
    // `instanceCount` instances. A no-op for `instanceCount == 0` (should
    // never be reached in practice - PHASE0's Locked Design Decision 7
    // requires at least kMinInstancesForGpuDrivenBatch instances for a group
    // to be eligible at all - but guarded defensively rather than assumed).
    //
    // IMPORTANT for whichever future phase (PHASE5) threads a draw count
    // into Renderer::SubmitIndirect()'s degenerate-padding branch: this
    // buffer's own CAPACITY only ever grows across frames, it is NEVER an
    // accurate stand-in for "how many instances are in this batch THIS
    // frame" - a batch that shrinks (e.g. 10 instances last frame, 6 this
    // frame) still has 10-instance-sized buffers here. The caller must
    // always thread THIS FRAME'S real instanceCount through separately (see
    // GpuDrivenBatchFrameEntry::instanceCount, RenderSystem.h) - passing
    // this buffer's own capacity as a draw count would silently redraw
    // stale "ghost" commands left over from a larger previous frame (see
    // PHASE3_COMPLETION_REPORT.md's own RUN D, which empirically
    // reproduced exactly this hazard at the shader/buffer level).
    void EnsureCapacity(Renderer& renderer, const GpuDrivenBatchKey& key, std::size_t instanceCount);

    // Uploads `instances` (this frame's WHOLE packed instance array for this
    // batch, in the same order RenderBatchGroup::commands was in - see
    // RenderSystem::CollectGpuDrivenBatches()) into `key`'s persistent input
    // buffer via ONE Buffer::Upload() call. A no-op (degrades gracefully,
    // never crashes - mirrors RenderSystem::Draw()'s own "skip a
    // never-resolved handle" convention) if EnsureCapacity() was never
    // called for this exact key, or if `instances` is empty.
    void PackThisFrame(const GpuDrivenBatchKey& key, const std::vector<GpuCullingInstanceInput>& instances);

    const Entry* TryGet(const GpuDrivenBatchKey& key) const;

    // The ONE shared CullingPipelines instance every batch's own future
    // culling dispatch (PHASE5) binds against - see CullingPipelines.h's
    // own class comment ("PHASE4's per-batch resource cache is expected to
    // own the single instance of this class"). Deliberately never
    // EnsureInitialized()'d by this class itself - this phase issues no
    // dispatch of any kind (see this file's own header comment); PHASE5 is
    // what actually calls Pipelines().EnsureInitialized(renderer) the first
    // time it declares a real culling compute pass.
    CullingPipelines& Pipelines() noexcept { return m_pipelines; }

private:
    std::map<GpuDrivenBatchKey, Entry> m_entries;
    CullingPipelines m_pipelines;
};

} // namespace gte
