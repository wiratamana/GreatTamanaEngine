#pragma once

#include "Memory/GpuMemoryTracker.h"
#include "Vulkan/VulkanAllocator.h"

#include <cstddef>
#include <memory>

namespace gte {

// How a Buffer's underlying GPU memory should be allocated - passed to
// Renderer::CreateBuffer() (see Renderer.h) and translated into the right
// VMA allocation flags under the hood.
enum class BufferMemoryUsage {
    // Device-local memory, not mappable from the CPU. Fastest for the GPU
    // to read - the right choice for vertex/index buffers and other static
    // data, uploaded once via a staging buffer (see
    // Renderer::CreateDeviceLocalBuffer()).
    GpuOnly,
    // Host-visible memory, persistently mapped for the buffer's entire
    // lifetime (MappedData() is valid immediately after construction).
    // Sequential-write access pattern - the common case for staging
    // buffers and per-frame uniform buffers the CPU writes to every frame.
    CpuToGpu,
    // Host-visible memory, persistently mapped, optimized for the GPU
    // writing and the CPU later reading it back (e.g. a query/readback
    // buffer). Less common than CpuToGpu.
    GpuToCpu,
};

// RAII wrapper around a VkBuffer + its backing VmaAllocation (GPU memory,
// sub-allocated by VMA - see Vulkan/VulkanAllocator.h): owns both for its
// entire lifetime, created in the constructor, destroyed in the destructor.
// The general-purpose GPU buffer primitive for vertex/index/uniform/staging
// data - construct via Renderer::CreateBuffer()/CreateDeviceLocalBuffer()
// rather than directly, same convention as RenderTexture.
//
// Does NOT own the VmaAllocator passed in - it must outlive this Buffer.
//
// Registers itself with a GpuMemoryTracker (see Memory/GpuMemoryTracker.h)
// on construction and deregisters on destruction, so the engine always
// knows exactly how much memory is live and where (GPU-only/host-visible/
// shared) without needing a debugger or an external tool.
class Buffer {
public:
    // debugName is optional and Editor-only (see GpuMemoryTracker) - a
    // plain, cheap `const char*` rather than a std::string, so passing one
    // costs nothing beyond a pointer copy even when it IS used, and the
    // pointed-to string is never stored anywhere outside the Editor build.
    Buffer(VmaAllocator allocator, std::shared_ptr<GpuMemoryTracker> tracker, VkDeviceSize size,
        VkBufferUsageFlags usage, BufferMemoryUsage memoryUsage, const char* debugName = nullptr);
    ~Buffer();

    Buffer(const Buffer&) = delete;
    Buffer& operator=(const Buffer&) = delete;

    Buffer(Buffer&& other) noexcept;
    Buffer& operator=(Buffer&& other) noexcept;

    VkBuffer Native() const noexcept { return m_buffer; }
    VkDeviceSize Size() const noexcept { return m_size; }

    // Non-null only for buffers created with BufferMemoryUsage::CpuToGpu/
    // GpuToCpu (persistently mapped by VMA) - nullptr for GpuOnly buffers,
    // which cannot be written to directly from the CPU.
    void* MappedData() const noexcept { return m_mappedData; }

    // Copies `size` bytes from `data` into this buffer's mapped memory at
    // `offset`. Only valid for buffers with non-null MappedData() - throws
    // otherwise.
    void Upload(const void* data, std::size_t size, std::size_t offset = 0);

    // Handle into the GpuMemoryTracker this Buffer is registered with -
    // valid for this Buffer's entire lifetime, kInvalidGpuResourceHandle
    // once moved-from.
    GpuResourceHandle Handle() const noexcept { return m_handle; }

private:
    void Destroy() noexcept;

    VmaAllocator m_allocator = VK_NULL_HANDLE;
    std::shared_ptr<GpuMemoryTracker> m_tracker;
    GpuResourceHandle m_handle;

    VkBuffer m_buffer = VK_NULL_HANDLE;
    VmaAllocation m_allocation = VK_NULL_HANDLE;
    VkDeviceSize m_size = 0;
    void* m_mappedData = nullptr;
};

} // namespace gte
