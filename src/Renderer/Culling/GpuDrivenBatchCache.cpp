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

} // namespace gte
