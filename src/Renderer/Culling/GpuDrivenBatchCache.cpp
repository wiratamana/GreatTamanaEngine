#include "GpuDrivenBatchCache.h"

#include "../IndirectDrawTypes.h"
#include "../Renderer.h"

#include <cstring>

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
            BufferMemoryUsage::CpuToGpu, /*extraUsage=*/BufferUsage::None, "GpuDrivenBatch.Input"),
        renderer.CreateStructuredBuffer(sizeof(IndirectDrawCommand), static_cast<std::uint32_t>(instanceCount),
            BufferMemoryUsage::GpuOnly, /*extraUsage=*/BufferUsage::IndirectBuffer,
            "GpuDrivenBatch.IndirectCommands"),
        // The count is filled (transfer dst), read by the draw (indirect) and copied to the readback (transfer src).
        renderer.CreateStructuredBuffer(sizeof(std::uint32_t), /*elementCount=*/1, BufferMemoryUsage::GpuOnly,
            BufferUsage::IndirectBuffer | BufferUsage::TransferSrc, "GpuDrivenBatch.VisibleCount"),
        instanceCount,
        // Replaced by the existing readback buffer below when an entry grows.
        renderer.CreateStructuredBuffer(sizeof(std::uint32_t), /*elementCount=*/1, BufferMemoryUsage::GpuToCpu,
            BufferUsage::TransferDst, "GpuDrivenBatch.VisibleCountReadback"),
    };

    if (found != m_entries.end()) {
        // The count readback is one uint32 regardless of capacity, so the existing buffer is kept on a grow.
        entry.countReadbackBuffer = std::move(found->second.countReadbackBuffer);
        found->second = std::move(entry);
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

std::optional<std::uint32_t> GpuDrivenBatchCache::ReadLastKnownVisibleCount(const GpuDrivenBatchKey& key) const
{
    const auto found = m_entries.find(key);
    if (found == m_entries.end()) {
        return std::nullopt;
    }
    std::uint32_t value = 0;
    if (!found->second.countReadbackBuffer.Download(&value, sizeof(value))) {
        return std::nullopt;
    }
    return value;
}

void GpuDrivenBatchCache::EnsureDescriptorSetsWritten(Renderer& renderer, const GpuDrivenBatchKey& key)
{
    const auto found = m_entries.find(key);
    if (found == m_entries.end()) {
        return; // No capacity was ensured for this key this frame.
    }
    Entry& entry = found->second;

    m_pipelines.EnsureInitialized(renderer); // Safe to call every frame.

    const VkDevice device = renderer.GetVulkanContextInfo().device;

    if (entry.cullingDescriptorSet == DescriptorSetHandle::Invalid) {
        entry.cullingDescriptorSet =
            RegisterDescriptorSet(renderer.AllocateComputeDescriptorSet(m_pipelines.DescriptorSetLayout()).Native());
    }
    ComputeDescriptorSet(Native(entry.cullingDescriptorSet), device)
        .Rewrite(
            {
                ComputeDescriptorWrite::StorageBuffer(0, entry.inputBuffer.Native()),
                ComputeDescriptorWrite::StorageBuffer(1, entry.indirectCommandBuffer.Native()),
                ComputeDescriptorWrite::StorageBuffer(2, entry.countBuffer.Native()),
            });

    if (entry.instanceBufferDescriptorSet == DescriptorSetHandle::Invalid) {
        entry.instanceBufferDescriptorSet = RegisterDescriptorSet(
            renderer.AllocateComputeDescriptorSet(renderer.InstanceBufferDescriptorSetLayout()).Native());
    }
    ComputeDescriptorSet(Native(entry.instanceBufferDescriptorSet), device)
        .Rewrite(
            {
                ComputeDescriptorWrite::StorageBuffer(0, entry.inputBuffer.Native()),
            });
}

// GPU-Driven Frustum Culling + Indirect Draw campaign (render-pass-5), PHASE5 -
// see this method's own doc comment in GpuDrivenBatchCache.h.
const Pipeline& GpuDrivenBatchCache::ResolveInstancedPipeline(
    Renderer& renderer, PipelineHandle originalHandle, VkDescriptorSetLayout sceneServicesSetLayout)
{
    const auto found = m_instancedPipelines.find(originalHandle);
    if (found != m_instancedPipelines.end()) {
        return found->second;
    }

    Pipeline instanced = renderer.CreatePipeline("shaders/MeshInstanced.vert.spv", "shaders/Mesh.frag.spv",
        VertexLayout::PositionNormalInstanced, /*useMaterialTexture=*/false,
        "MeshInstanced.vert/Mesh.frag (PositionNormalInstanced)", /*useInstanceBuffer=*/true, sceneServicesSetLayout);
    const auto [it, inserted] = m_instancedPipelines.emplace(originalHandle, std::move(instanced));
    (void)inserted;
    return it->second;
}

} // namespace gte
