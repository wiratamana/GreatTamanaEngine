#include "Buffer.h"

#include <cstdint>
#include <cstring>
#include <stdexcept>
#include <utility>

namespace gte {

Buffer::Buffer(VmaAllocator allocator, VkDeviceSize size, VkBufferUsageFlags usage, BufferMemoryUsage memoryUsage)
    : m_allocator(allocator)
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
}

Buffer::~Buffer()
{
    Destroy();
}

Buffer::Buffer(Buffer&& other) noexcept
    : m_allocator(std::exchange(other.m_allocator, VK_NULL_HANDLE))
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
    std::memcpy(static_cast<std::uint8_t*>(m_mappedData) + offset, data, size);
}

void Buffer::Destroy() noexcept
{
    if (m_buffer != VK_NULL_HANDLE) {
        vmaDestroyBuffer(m_allocator, m_buffer, m_allocation);
        m_buffer = VK_NULL_HANDLE;
        m_allocation = VK_NULL_HANDLE;
        m_mappedData = nullptr;
    }
}

} // namespace gte
