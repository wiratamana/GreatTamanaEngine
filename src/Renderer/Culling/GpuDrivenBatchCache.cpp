#include "GpuDrivenBatchCache.h"

#include "../IndirectDrawTypes.h"
#include "../Renderer.h"
#include "../RenderGraph/GpuBufferInternal.h"

#include <cstring>

namespace gte {

using rg::GpuBuffer;
using rg::GpuBufferAccess;
using rg::GpuBufferUse;

void GpuDrivenBatchCache::EnsureCapacity(Renderer& renderer, const GpuDrivenBatchKey& key, std::size_t instanceCount)
{
    if (instanceCount == 0) {
        return;
    }

    const auto found = m_entries.find(key);
    if (found != m_entries.end() && found->second.capacity >= instanceCount) {
        return;
    }

    const rg::DeviceRef device = renderer.Device();
    Entry entry{
        GpuBuffer(device, sizeof(GpuCullingInstanceInput) * instanceCount, "GpuDrivenBatch.Input",
            GpuBufferUse::Storage | GpuBufferUse::HostWrite),
        GpuBuffer(device, sizeof(IndirectDrawCommand) * instanceCount, "GpuDrivenBatch.IndirectCommands",
            GpuBufferUse::Storage | GpuBufferUse::Indirect),
        // Filled (transfer dst), read by the draw (indirect) and copied to the readback (transfer src).
        GpuBuffer(device, sizeof(std::uint32_t), "GpuDrivenBatch.VisibleCount",
            GpuBufferUse::Storage | GpuBufferUse::Indirect | GpuBufferUse::TransferSrc),
        instanceCount,
        GpuBuffer(device, sizeof(std::uint32_t), "GpuDrivenBatch.VisibleCountReadback",
            GpuBufferUse::HostRead | GpuBufferUse::TransferDst),
    };

    if (found != m_entries.end()) {
        // The readback is one uint32 whatever the capacity, so the existing buffer survives a grow.
        entry.countReadbackBuffer = std::move(found->second.countReadbackBuffer);
        entry.cullingDescriptorSet = found->second.cullingDescriptorSet;
        entry.instanceBufferDescriptorSet = found->second.instanceBufferDescriptorSet;
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
        return;
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
        return;
    }
    Entry& entry = found->second;

    m_pipelines.EnsureInitialized(renderer);

    const VkDevice device = renderer.GetVulkanContextInfo().device;
    const VkBuffer input = GpuBufferAccess::Underlying(entry.inputBuffer).Native();

    if (entry.cullingDescriptorSet == DescriptorSetHandle::Invalid) {
        entry.cullingDescriptorSet =
            RegisterDescriptorSet(renderer.AllocateComputeDescriptorSet(m_pipelines.DescriptorSetLayout()).Native());
    }
    ComputeDescriptorSet(Native(entry.cullingDescriptorSet), device)
        .Rewrite(
            {
                ComputeDescriptorWrite::StorageBuffer(0, input),
                ComputeDescriptorWrite::StorageBuffer(1, GpuBufferAccess::Underlying(entry.indirectCommandBuffer).Native()),
                ComputeDescriptorWrite::StorageBuffer(2, GpuBufferAccess::Underlying(entry.countBuffer).Native()),
            });

    if (entry.instanceBufferDescriptorSet == DescriptorSetHandle::Invalid) {
        entry.instanceBufferDescriptorSet = RegisterDescriptorSet(
            renderer.AllocateComputeDescriptorSet(renderer.InstanceBufferDescriptorSetLayout()).Native());
    }
    ComputeDescriptorSet(Native(entry.instanceBufferDescriptorSet), device)
        .Rewrite({ ComputeDescriptorWrite::StorageBuffer(0, input) });
}

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
