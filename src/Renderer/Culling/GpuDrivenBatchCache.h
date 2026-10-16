#pragma once

// Persistent GPU buffers and descriptor sets of GPU-driven batches, one entry per (mesh, pipeline).
// Renderer-layer only: it never sees ECS or game types, only handles and already packed instances.

#include "CullingPipelines.h"
#include "CullingTypes.h"
#include "../../Core/GraphicsHandles.h" // DescriptorSetHandle
#include "../Compute/ComputeDescriptorSet.h"
#include "../MeshHandle.h"
#include "../Pipeline.h"
#include "../PipelineHandle.h"
#include "../RenderGraph/GpuBuffer.h"

#include <cstddef>
#include <cstdint>
#include <map>
#include <optional>
#include <vector>

namespace gte {

class Renderer;

// One batch: a single (mesh, pipeline) pair. There is no view dimension: batches are built for the game view only.
struct GpuDrivenBatchKey {
    MeshHandle mesh;
    PipelineHandle pipeline;

    friend bool operator==(const GpuDrivenBatchKey& a, const GpuDrivenBatchKey& b) noexcept
    {
        return a.mesh == b.mesh && a.pipeline == b.pipeline;
    }
    friend bool operator!=(const GpuDrivenBatchKey& a, const GpuDrivenBatchKey& b) noexcept { return !(a == b); }

    // Total order so the key works as a std::map key.
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

// Buffers are created once per key and only reallocated when the instance count grows past `capacity`.
class GpuDrivenBatchCache {
public:
    GpuDrivenBatchCache() = default;

    GpuDrivenBatchCache(const GpuDrivenBatchCache&) = delete;
    GpuDrivenBatchCache& operator=(const GpuDrivenBatchCache&) = delete;
    GpuDrivenBatchCache(GpuDrivenBatchCache&&) = delete;
    GpuDrivenBatchCache& operator=(GpuDrivenBatchCache&&) = delete;

    struct Entry {
        rg::GpuBuffer inputBuffer;           // Packed instances, rewritten by the CPU every frame.
        rg::GpuBuffer indirectCommandBuffer; // Worst case: one command per instance.
        rg::GpuBuffer countBuffer;           // One uint32: filled, written by culling, read by the draw, copied out.
        std::size_t capacity = 0;            // Instances the buffers above are sized for.

        // One uint32 copy of the visible count for the CPU; its size does not depend on `capacity`.
        rg::GpuBuffer countReadbackBuffer;

        // Persistent sets, allocated on first use and rewritten in place by EnsureDescriptorSetsWritten().
        DescriptorSetHandle cullingDescriptorSet = DescriptorSetHandle::Invalid;       // input, commands, count.
        DescriptorSetHandle instanceBufferDescriptorSet = DescriptorSetHandle::Invalid; // input only.
    };

    // Grows the key's buffers to hold at least `instanceCount` instances; never shrinks. No-op for 0.
    // `capacity` is not this frame's draw count: pass the frame's real instance count separately.
    void EnsureCapacity(Renderer& renderer, const GpuDrivenBatchKey& key, std::size_t instanceCount);

    // Uploads this frame's packed instances into the key's input buffer. No-op without EnsureCapacity() or with no instances.
    void PackThisFrame(const GpuDrivenBatchKey& key, const std::vector<GpuCullingInstanceInput>& instances);

    const Entry* TryGet(const GpuDrivenBatchKey& key) const;

    // Reads the readback buffer without waiting. Call only after the frame's fence wait. Nullopt for an unknown key.
    std::optional<std::uint32_t> ReadLastKnownVisibleCount(const GpuDrivenBatchKey& key) const;

    // The culling pipelines shared by every batch.
    CullingPipelines& Pipelines() noexcept { return m_pipelines; }

    // Allocates (once) and rewrites the key's two descriptor sets against its current buffers.
    // Call after EnsureCapacity(); no-op for an unknown key.
    void EnsureDescriptorSetsWritten(Renderer& renderer, const GpuDrivenBatchKey& key);

    // The instanced sibling of a batch's pipeline, built once per original pipeline and cached.
    // May throw from pipeline creation; the caller must then drop the batch from every decision.
    const Pipeline& ResolveInstancedPipeline(
        Renderer& renderer, PipelineHandle originalHandle, DescriptorSetLayoutHandle sceneServicesSetLayout);

private:
    struct PipelineHandleLess {
        bool operator()(const PipelineHandle& a, const PipelineHandle& b) const noexcept
        {
            if (a.index != b.index) {
                return a.index < b.index;
            }
            return a.generation < b.generation;
        }
    };

    std::map<GpuDrivenBatchKey, Entry> m_entries;
    CullingPipelines m_pipelines;
    std::map<PipelineHandle, Pipeline, PipelineHandleLess> m_instancedPipelines;
};

} // namespace gte
