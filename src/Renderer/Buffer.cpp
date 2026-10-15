#include "Buffer.h"

#include <cstdint>
#include <cstring>
#include <stdexcept>
#include <string>
#include <utility>

namespace {

// The only place a BufferUsage bitmask becomes VkBufferUsageFlags.
VkBufferUsageFlags ToVkBufferUsageFlags(gte::BufferUsage usage) noexcept
{
    VkBufferUsageFlags flags = 0;
    if (gte::HasUsage(usage, gte::BufferUsage::TransferSrc)) {
        flags |= VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
    }
    if (gte::HasUsage(usage, gte::BufferUsage::TransferDst)) {
        flags |= VK_BUFFER_USAGE_TRANSFER_DST_BIT;
    }
    if (gte::HasUsage(usage, gte::BufferUsage::UniformBuffer)) {
        flags |= VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT;
    }
    if (gte::HasUsage(usage, gte::BufferUsage::StorageBuffer)) {
        flags |= VK_BUFFER_USAGE_STORAGE_BUFFER_BIT;
    }
    if (gte::HasUsage(usage, gte::BufferUsage::IndexBuffer)) {
        flags |= VK_BUFFER_USAGE_INDEX_BUFFER_BIT;
    }
    if (gte::HasUsage(usage, gte::BufferUsage::VertexBuffer)) {
        flags |= VK_BUFFER_USAGE_VERTEX_BUFFER_BIT;
    }
    if (gte::HasUsage(usage, gte::BufferUsage::IndirectBuffer)) {
        flags |= VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT;
    }
    return flags;
}

} // namespace

namespace gte {

Buffer::Buffer(VmaAllocator allocator, std::shared_ptr<GpuMemoryTracker> tracker, uint64_t size,
    BufferUsage usage, BufferMemoryUsage memoryUsage, const char* debugName)
    : m_allocator(allocator)
    , m_tracker(std::move(tracker))
    , m_size(size)
{
    VkBufferCreateInfo bufferInfo{};
    bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    bufferInfo.size = static_cast<VkDeviceSize>(size);
    bufferInfo.usage = ToVkBufferUsageFlags(usage);
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

    // Non-null exactly for CpuToGpu/GpuToCpu; VMA unmaps it in vmaDestroyBuffer().
    m_mappedData = allocationInfo.pMappedData;

    // The record uses the real allocation size and memory location VMA picked, not the requested ones.
    const GpuMemoryLocation location = ClassifyGpuMemoryLocation(m_allocator, m_allocation);
    m_handle = m_tracker->Track(GpuResourceType::Buffer, location, allocationInfo.size);
    // Cheap no-op when no debug-name observer is installed.
    if (debugName != nullptr) {
        m_tracker->SetDebugName(m_handle, debugName);
    }
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
    , m_size(std::exchange(other.m_size, uint64_t{ 0 }))
    , m_mappedData(std::exchange(other.m_mappedData, nullptr))
    , m_objectHandle(std::exchange(other.m_objectHandle, CachedBufferObjectHandle{}))
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
        m_size = std::exchange(other.m_size, uint64_t{ 0 });
        m_mappedData = std::exchange(other.m_mappedData, nullptr);
        m_objectHandle = std::exchange(other.m_objectHandle, CachedBufferObjectHandle{});
    }
    return *this;
}

void Buffer::Upload(const void* data, std::size_t size, std::size_t offset)
{
    if (m_mappedData == nullptr) {
        throw std::runtime_error(
            "Buffer::Upload: buffer is not host-mapped (was it created with BufferMemoryUsage::GpuOnly?).");
    }
    // Bounds are checked before the memcpy; the offset test comes first so the subtraction cannot underflow.
    const auto bufferSize = static_cast<std::size_t>(m_size);
    if (offset > bufferSize || size > bufferSize - offset) {
        throw std::out_of_range(
            "Buffer::Upload: offset (" + std::to_string(offset) + ") + size (" + std::to_string(size) +
            ") exceeds this buffer's size (" + std::to_string(bufferSize) + " bytes).");
    }
    std::memcpy(static_cast<std::uint8_t*>(m_mappedData) + offset, data, size);
}

bool Buffer::Download(void* data, std::size_t size, std::size_t offset) const
{
    const auto bufferSize = static_cast<std::size_t>(m_size);
    if (m_mappedData == nullptr || offset > bufferSize || size > bufferSize - offset) {
        return false;
    }
    // Host-visible memory is not guaranteed coherent; this is a no-op when it is.
    vmaInvalidateAllocation(m_allocator, m_allocation, static_cast<VkDeviceSize>(offset), static_cast<VkDeviceSize>(size));
    std::memcpy(data, static_cast<const std::uint8_t*>(m_mappedData) + offset, size);
    return true;
}

void Buffer::Destroy() noexcept
{
    if (m_buffer != VK_NULL_HANDLE) {
        m_tracker->Untrack(m_handle);
        m_handle = kInvalidGpuResourceHandle;

        // Never leave a stale registry entry pointing at a VkBuffer that's about to be destroyed.
        m_objectHandle.Resolve(VK_NULL_HANDLE);

        vmaDestroyBuffer(m_allocator, m_buffer, m_allocation);
        m_buffer = VK_NULL_HANDLE;
        m_allocation = VK_NULL_HANDLE;
        m_mappedData = nullptr;
    }
}

} // namespace gte
