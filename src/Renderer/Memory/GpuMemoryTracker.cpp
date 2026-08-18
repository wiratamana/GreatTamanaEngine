#include "GpuMemoryTracker.h"

namespace gte {

GpuMemoryLocation ClassifyGpuMemoryLocation(VmaAllocator allocator, VmaAllocation allocation)
{
    VkMemoryPropertyFlags flags = 0;
    vmaGetAllocationMemoryProperties(allocator, allocation, &flags);

    const bool deviceLocal = (flags & VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT) != 0;
    const bool hostVisible = (flags & VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT) != 0;

    if (deviceLocal && hostVisible) {
        return GpuMemoryLocation::Shared;
    }
    if (deviceLocal) {
        return GpuMemoryLocation::GpuOnly;
    }
    return GpuMemoryLocation::CpuOnly;
}

GpuResourceHandle GpuMemoryTracker::Track(GpuResourceType type, GpuMemoryLocation location, VkDeviceSize sizeBytes)
{
    const GpuResourceRecord record{ type, location, sizeBytes };

    std::uint32_t index;
    if (!m_freeList.empty()) {
        index = m_freeList.back();
        m_freeList.pop_back();

        Slot& slot = m_slots[index];
        // Bump forward from whatever generation this slot last held (skip
        // the reserved 0 sentinel on wraparound - astronomically unlikely
        // to matter in practice, but cheap to guard against regardless).
        slot.generation += 1;
        if (slot.generation == 0) {
            slot.generation = 1;
        }
        slot.record = record;
        slot.occupied = true;
    } else {
        index = static_cast<std::uint32_t>(m_slots.size());
        m_slots.push_back(Slot{ record, 1, true });
    }

    AddToTotals(record);

    return GpuResourceHandle{ index, m_slots[index].generation };
}

void GpuMemoryTracker::Untrack(GpuResourceHandle handle)
{
    if (handle.index >= m_slots.size()) {
        return; // Never valid - ignore.
    }

    Slot& slot = m_slots[handle.index];
    if (!slot.occupied || slot.generation != handle.generation) {
        return; // Already freed, or a stale handle from a since-reused slot.
    }

    RemoveFromTotals(slot.record);

#if GTE_ENABLE_EDITOR
    m_debugNames.erase(PackHandle(handle));
#endif

    slot.record = GpuResourceRecord{};
    slot.occupied = false;
    // slot.generation deliberately left as-is here (not reset to 0) - it's
    // exactly the value Track() bumps forward from the next time this slot
    // is reused, so two different resources that ever occupied the same
    // slot can never share a handle.
    m_freeList.push_back(handle.index);
}

void GpuMemoryTracker::AddToTotals(const GpuResourceRecord& record)
{
    m_totals.totalBytes += record.sizeBytes;
    switch (record.type) {
    case GpuResourceType::Buffer:
        m_totals.bufferBytes += record.sizeBytes;
        m_totals.bufferCount += 1;
        break;
    case GpuResourceType::Texture:
        m_totals.textureBytes += record.sizeBytes;
        m_totals.textureCount += 1;
        break;
    }
    switch (record.location) {
    case GpuMemoryLocation::GpuOnly:
        m_totals.gpuOnlyBytes += record.sizeBytes;
        break;
    case GpuMemoryLocation::CpuOnly:
        m_totals.cpuOnlyBytes += record.sizeBytes;
        break;
    case GpuMemoryLocation::Shared:
        m_totals.sharedBytes += record.sizeBytes;
        break;
    }
}

void GpuMemoryTracker::RemoveFromTotals(const GpuResourceRecord& record)
{
    m_totals.totalBytes -= record.sizeBytes;
    switch (record.type) {
    case GpuResourceType::Buffer:
        m_totals.bufferBytes -= record.sizeBytes;
        m_totals.bufferCount -= 1;
        break;
    case GpuResourceType::Texture:
        m_totals.textureBytes -= record.sizeBytes;
        m_totals.textureCount -= 1;
        break;
    }
    switch (record.location) {
    case GpuMemoryLocation::GpuOnly:
        m_totals.gpuOnlyBytes -= record.sizeBytes;
        break;
    case GpuMemoryLocation::CpuOnly:
        m_totals.cpuOnlyBytes -= record.sizeBytes;
        break;
    case GpuMemoryLocation::Shared:
        m_totals.sharedBytes -= record.sizeBytes;
        break;
    }
}

std::vector<GpuMemoryTracker::Entry> GpuMemoryTracker::GetAllResources() const
{
    std::vector<Entry> result;
    result.reserve(m_slots.size());
    for (std::uint32_t i = 0; i < static_cast<std::uint32_t>(m_slots.size()); ++i) {
        const Slot& slot = m_slots[i];
        if (!slot.occupied) {
            continue;
        }
        result.push_back(Entry{ GpuResourceHandle{ i, slot.generation }, slot.record });
    }
    return result;
}

#if GTE_ENABLE_EDITOR
void GpuMemoryTracker::SetDebugName(GpuResourceHandle handle, const char* name)
{
    if (!handle.IsValid()) {
        return;
    }
    m_debugNames[PackHandle(handle)] = (name != nullptr) ? name : "";
}

const std::string& GpuMemoryTracker::GetDebugName(GpuResourceHandle handle) const
{
    static const std::string kEmpty;
    const auto it = m_debugNames.find(PackHandle(handle));
    return it != m_debugNames.end() ? it->second : kEmpty;
}
#endif

} // namespace gte
