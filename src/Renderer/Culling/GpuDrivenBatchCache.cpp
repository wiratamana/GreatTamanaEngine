#include "GpuDrivenBatchCache.h"

#include "../IndirectDrawTypes.h"
#include "../Renderer.h"

namespace gte {

void GpuDrivenBatchCache::EnsureCapacity(Renderer& renderer, const GpuDrivenBatchKey& key, std::size_t instanceCount)
{
    if (instanceCount == 0) {
        return; // Nothing to allocate for an empty batch - see this method's own doc comment.
    }

    const auto found = m_entries.find(key);
    if (found != m_entries.end() && found->second.capacity >= instanceCount) {
        return; // Already big enough - reallocate ONLY when instanceCount GREW past capacity.
    }

    Entry entry{
        renderer.CreateStructuredBuffer(sizeof(GpuCullingInstanceInput), static_cast<std::uint32_t>(instanceCount),
            BufferMemoryUsage::CpuToGpu, /*extraUsage=*/0, "GpuDrivenBatch.Input"),
        renderer.CreateStructuredBuffer(sizeof(IndirectDrawCommand), static_cast<std::uint32_t>(instanceCount),
            BufferMemoryUsage::GpuOnly, /*extraUsage=*/VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT,
            "GpuDrivenBatch.IndirectCommands"),
        renderer.CreateStructuredBuffer(sizeof(std::uint32_t), /*elementCount=*/1, BufferMemoryUsage::GpuOnly,
            /*extraUsage=*/VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT, "GpuDrivenBatch.VisibleCount"),
        instanceCount,
    };

    if (found != m_entries.end()) {
        found->second = std::move(entry); // Grow: old, smaller buffers are destroyed (RAII) as this assignment runs.
    } else {
        m_entries.emplace(key, std::move(entry));
    }
}

void GpuDrivenBatchCache::PackThisFrame(const GpuDrivenBatchKey& key, const std::vector<GpuCullingInstanceInput>& instances)
{
    if (instances.empty()) {
        return;
    }

    const auto found = m_entries.find(key);
    if (found == m_entries.end()) {
        return; // EnsureCapacity() was never called for this key this frame - degrade gracefully, never crash.
    }

    found->second.inputBuffer.Upload(instances.data(), instances.size() * sizeof(GpuCullingInstanceInput));
}

const GpuDrivenBatchCache::Entry* GpuDrivenBatchCache::TryGet(const GpuDrivenBatchKey& key) const
{
    const auto found = m_entries.find(key);
    return found != m_entries.end() ? &found->second : nullptr;
}

// GPU-Driven Frustum Culling + Indirect Draw campaign (render-pass-5), PHASE5 -
// see this method's own doc comment in GpuDrivenBatchCache.h.
void GpuDrivenBatchCache::EnsureDescriptorSetsWritten(Renderer& renderer, const GpuDrivenBatchKey& key)
{
    const auto found = m_entries.find(key);
    if (found == m_entries.end()) {
        return; // EnsureCapacity() was never called for this key this frame - degrade gracefully, never crash.
    }
    Entry& entry = found->second;

    // Idempotent/safe to call every frame - see CullingPipelines::
    // EnsureInitialized()'s own doc comment.
    m_pipelines.EnsureInitialized(renderer);

    const VkDevice device = renderer.GetVulkanContextInfo().device;

    if (entry.cullingDescriptorSet == VK_NULL_HANDLE) {
        entry.cullingDescriptorSet = renderer.AllocateComputeDescriptorSet(m_pipelines.DescriptorSetLayout());
    }
    ComputeDescriptorSet(entry.cullingDescriptorSet)
        .Rewrite(device,
            {
                ComputeDescriptorWrite::StorageBuffer(0, entry.inputBuffer.Native()),
                ComputeDescriptorWrite::StorageBuffer(1, entry.indirectCommandBuffer.Native()),
                ComputeDescriptorWrite::StorageBuffer(2, entry.countBuffer.Native()),
            });

    if (entry.instanceBufferDescriptorSet == VK_NULL_HANDLE) {
        entry.instanceBufferDescriptorSet =
            renderer.AllocateComputeDescriptorSet(renderer.InstanceBufferDescriptorSetLayout());
    }
    ComputeDescriptorSet(entry.instanceBufferDescriptorSet)
        .Rewrite(device,
            {
                ComputeDescriptorWrite::StorageBuffer(0, entry.inputBuffer.Native()),
            });
}

// GPU-Driven Frustum Culling + Indirect Draw campaign (render-pass-5), PHASE5 -
// see this method's own doc comment in GpuDrivenBatchCache.h.
const Pipeline& GpuDrivenBatchCache::ResolveInstancedPipeline(Renderer& renderer, PipelineHandle originalHandle)
{
    const auto found = m_instancedPipelines.find(originalHandle);
    if (found != m_instancedPipelines.end()) {
        return found->second;
    }

    Pipeline instanced = renderer.CreatePipeline("shaders/MeshInstanced.vert.spv", "shaders/Mesh.frag.spv",
        VertexLayout::PositionNormalInstanced, /*useMaterialTexture=*/false,
        "MeshInstanced.vert/Mesh.frag (PositionNormalInstanced)", /*useInstanceBuffer=*/true);
    const auto [it, inserted] = m_instancedPipelines.emplace(originalHandle, std::move(instanced));
    (void)inserted;
    return it->second;
}

} // namespace gte
