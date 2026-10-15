#pragma once

#include "GraphicsHandleRegistry.h"
#include "Memory/GpuMemoryTracker.h"
#include "Vulkan/VulkanAllocator.h"

#include <cstddef>
#include <cstdint>
#include <memory>

namespace gte {

// How a Buffer's GPU memory is allocated (translated to VMA flags by Renderer::CreateBuffer()).
enum class BufferMemoryUsage {
    // Device-local, not CPU-mappable; upload through a staging buffer.
    GpuOnly,
    // Host-visible, persistently mapped, sequential CPU writes (staging and per-frame uniform buffers).
    CpuToGpu,
    // Host-visible, persistently mapped, for GPU writes read back by the CPU.
    GpuToCpu,
};

// Engine buffer-usage flags with no Vulkan dependency; Buffer.cpp is the only place they become VkBufferUsageFlags.
// Only the bits the engine uses are declared.
enum class BufferUsage : std::uint32_t {
    None = 0,
    TransferSrc = 1u << 0,
    TransferDst = 1u << 1,
    UniformBuffer = 1u << 2,
    StorageBuffer = 1u << 3,
    IndexBuffer = 1u << 4,
    VertexBuffer = 1u << 5,
    IndirectBuffer = 1u << 6,
};

constexpr BufferUsage operator|(BufferUsage a, BufferUsage b) noexcept
{
    return static_cast<BufferUsage>(static_cast<std::uint32_t>(a) | static_cast<std::uint32_t>(b));
}
constexpr BufferUsage& operator|=(BufferUsage& a, BufferUsage b) noexcept
{
    a = a | b;
    return a;
}
constexpr bool HasUsage(BufferUsage mask, BufferUsage flag) noexcept
{
    return (static_cast<std::uint32_t>(mask) & static_cast<std::uint32_t>(flag)) != 0u;
}

// RAII owner of a VkBuffer and its VMA allocation; registers with the GpuMemoryTracker for its lifetime.
// Create through Renderer::CreateBuffer()/CreateDeviceLocalBuffer(). The VmaAllocator must outlive it.
class Buffer {
public:
    // `debugName` is cosmetic and never stored by the tracker; only the installed debug-name observer sees it.
    // `usage` is the engine bitmask; this constructor is the one place it becomes VkBufferUsageFlags.
    Buffer(VmaAllocator allocator, std::shared_ptr<GpuMemoryTracker> tracker, uint64_t size, BufferUsage usage,
        BufferMemoryUsage memoryUsage, const char* debugName = nullptr);
    ~Buffer();

    Buffer(const Buffer&) = delete;
    Buffer& operator=(const Buffer&) = delete;

    Buffer(Buffer&& other) noexcept;
    Buffer& operator=(Buffer&& other) noexcept;

    VkBuffer Native() const noexcept { return m_buffer; }
    uint64_t Size() const noexcept { return m_size; }

    // Non-null only for CpuToGpu/GpuToCpu buffers (persistently mapped); null for GpuOnly.
    void* MappedData() const noexcept { return m_mappedData; }

    // Copies into the mapped memory at `offset`. Throws std::runtime_error when not host-mapped and
    // std::out_of_range when `offset + size` passes Size().
    void Upload(const void* data, std::size_t size, std::size_t offset = 0);

    // Copies `size` bytes at `offset` out of the mapped memory, invalidating the CPU cache first so GPU writes are
    // seen on non-coherent memory. False when the buffer is not host-mapped or the range is out of bounds.
    bool Download(void* data, std::size_t size, std::size_t offset = 0) const;

    // Memory-tracker handle; kInvalidGpuResourceHandle once moved-from.
    GpuResourceHandle Handle() const noexcept { return m_handle; }

    // Opaque handle for the graph's compute-binding API; registered on first call, then reused.
    BufferObjectHandle ObjectHandle() const noexcept { return m_objectHandle.Resolve(m_buffer); }

private:
    void Destroy() noexcept;

    VmaAllocator m_allocator = VK_NULL_HANDLE;
    std::shared_ptr<GpuMemoryTracker> m_tracker;
    GpuResourceHandle m_handle;

    VkBuffer m_buffer = VK_NULL_HANDLE;
    VmaAllocation m_allocation = VK_NULL_HANDLE;
    uint64_t m_size = 0;
    void* m_mappedData = nullptr;

    // Filled lazily by ObjectHandle(); mutable because it only caches a registry handle.
    mutable CachedBufferObjectHandle m_objectHandle;
};

} // namespace gte
