#include "Buffer.h"

#include <cstdint>
#include <cstring>
#include <stdexcept>
#include <string>
#include <utility>

namespace gte {

Buffer::Buffer(VmaAllocator allocator, std::shared_ptr<GpuMemoryTracker> tracker, VkDeviceSize size,
    VkBufferUsageFlags usage, BufferMemoryUsage memoryUsage, const char* debugName)
    : m_allocator(allocator)
    , m_tracker(std::move(tracker))
    , m_size(size)
{
    VkBufferCreateInfo bufferInfo{};
    bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    bufferInfo.size = size;
    bufferInfo.usage = usage;
    bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

    VmaAllocationCreateInfo allocCreateInfo{};
    allocCreateInfo.usage = VMA_MEMORY_USAGE_AUTO;
    switch (memoryUsage) {
    case BufferMemoryUsage::GpuOnly:
        break; // No extra flags - device-local, not mappable.
    case BufferMemoryUsage::CpuToGpu:
        allocCreateInfo.flags =
            VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT | VMA_ALLOCATION_CREATE_MAPPED_BIT;
        break;
    case BufferMemoryUsage::GpuToCpu:
        allocCreateInfo.flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_RANDOM_BIT | VMA_ALLOCATION_CREATE_MAPPED_BIT;
        break;
    }

    VmaAllocationInfo allocationInfo{};
    if (vmaCreateBuffer(m_allocator, &bufferInfo, &allocCreateInfo, &m_buffer, &m_allocation, &allocationInfo) !=
        VK_SUCCESS) {
        throw std::runtime_error("Buffer: vmaCreateBuffer failed.");
    }

    // Non-null exactly when VMA_ALLOCATION_CREATE_MAPPED_BIT was requested
    // above (CpuToGpu/GpuToCpu) - stays valid for this Buffer's entire
    // lifetime, unmapped automatically by vmaDestroyBuffer().
    m_mappedData = allocationInfo.pMappedData;

    // Registers this exact allocation with the tracker - see AGENTS.md
    // ("GPU resource memory tracking"): the record must reflect the actual
    // allocation VMA gave us (allocationInfo.size, and the real memory
    // location it landed in), not just the requested `size`/memoryUsage.
    const GpuMemoryLocation location = ClassifyGpuMemoryLocation(m_allocator, m_allocation);
    m_handle = m_tracker->Track(GpuResourceType::Buffer, location, allocationInfo.size);
#if GTE_ENABLE_EDITOR
    if (debugName != nullptr) {
        m_tracker->SetDebugName(m_handle, debugName);
    }
#else
    (void)debugName;
#endif
}

Buffer::~Buffer()
{
    Destroy();
}

Buffer::Buffer(Buffer&& other) noexcept
    : m_allocator(std::exchange(other.m_allocator, VK_NULL_HANDLE))
    , m_tracker(std::move(other.m_tracker))
    , m_handle(std::exchange(other.m_handle, kInvalidGpuResourceHandle))
    , m_buffer(std::exchange(other.m_buffer, VK_NULL_HANDLE))
    , m_allocation(std::exchange(other.m_allocation, VK_NULL_HANDLE))
    , m_size(std::exchange(other.m_size, VkDeviceSize{ 0 }))
    , m_mappedData(std::exchange(other.m_mappedData, nullptr))
{
}

Buffer& Buffer::operator=(Buffer&& other) noexcept
{
    if (this != &other) {
        Destroy();
        m_allocator = std::exchange(other.m_allocator, VK_NULL_HANDLE);
        m_tracker = std::move(other.m_tracker);
        m_handle = std::exchange(other.m_handle, kInvalidGpuResourceHandle);
        m_buffer = std::exchange(other.m_buffer, VK_NULL_HANDLE);
        m_allocation = std::exchange(other.m_allocation, VK_NULL_HANDLE);
        m_size = std::exchange(other.m_size, VkDeviceSize{ 0 });
        m_mappedData = std::exchange(other.m_mappedData, nullptr);
    }
    return *this;
}

void Buffer::Upload(const void* data, std::size_t size, std::size_t offset)
{
    if (m_mappedData == nullptr) {
        throw std::runtime_error(
            "Buffer::Upload: buffer is not host-mapped (was it created with BufferMemoryUsage::GpuOnly?).");
    }
    // Guard against writing past the end of this buffer's actual allocation -
    // offset/size are plain caller-supplied values with nothing else
    // validating them before the memcpy below, so a mistake here would
    // otherwise silently corrupt whatever host-visible memory happens to
    // follow this allocation instead of failing loudly right at the call
    // that got it wrong. The offset > m_size check comes first so the size
    // comparison below can never underflow (size_t is unsigned).
    const auto bufferSize = static_cast<std::size_t>(m_size);
    if (offset > bufferSize || size > bufferSize - offset) {
        throw std::out_of_range(
            "Buffer::Upload: offset (" + std::to_string(offset) + ") + size (" + std::to_string(size) +
            ") exceeds this buffer's size (" + std::to_string(bufferSize) + " bytes).");
    }
    std::memcpy(static_cast<std::uint8_t*>(m_mappedData) + offset, data, size);
}

void Buffer::Destroy() noexcept
{
    if (m_buffer != VK_NULL_HANDLE) {
        m_tracker->Untrack(m_handle);
        m_handle = kInvalidGpuResourceHandle;

        vmaDestroyBuffer(m_allocator, m_buffer, m_allocation);
        m_buffer = VK_NULL_HANDLE;
        m_allocation = VK_NULL_HANDLE;
        m_mappedData = nullptr;
    }
}

} // namespace gte
