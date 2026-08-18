#pragma once

#include "Buffer.h"
#include "Memory/GpuMemoryTracker.h"
#include "Mesh.h"
#include "Pipeline.h"
#include "RenderTexture.h"
#include "Vulkan/VulkanAllocator.h"

#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace gte {

// Owns everything needed to manufacture GPU resources (buffers, off-screen
// render textures, pipelines, meshes) on behalf of Renderer, plus the
// bookkeeping (GpuMemoryTracker) and one-off command submission
// (ImmediateSubmit()) those factories need - see the individual method
// comments (moved here verbatim from the old Renderer.h/.cpp) for the
// reasoning behind each one.
//
// Does NOT own the VkDevice/VmaAllocator/VkQueue passed in - all three must
// outlive this object (same non-ownership convention as VulkanSwapchain/
// VulkanAllocator not owning the instance/device/surface passed to them).
// DOES own its own VkCommandPool (for ImmediateSubmit()), entirely separate
// from FramePresenter's per-frame/offscreen command buffers, so a resource
// upload (e.g. CreateDeviceLocalBuffer() at load time) never contends with
// in-flight presentation/offscreen work.
class GpuResourceFactory {
public:
    GpuResourceFactory(VkDevice device, VmaAllocator allocator, VkQueue graphicsQueue,
        std::uint32_t graphicsQueueFamily, std::shared_ptr<GpuMemoryTracker> memoryTracker);
    ~GpuResourceFactory();

    GpuResourceFactory(const GpuResourceFactory&) = delete;
    GpuResourceFactory& operator=(const GpuResourceFactory&) = delete;

    GpuResourceFactory(GpuResourceFactory&& other) noexcept;
    GpuResourceFactory& operator=(GpuResourceFactory&& other) noexcept;

    // See Renderer::CreateRenderTexture(). `format` here must already be
    // fully resolved - VK_FORMAT_UNDEFINED-as-"match ColorFormat()" is
    // resolved by Renderer itself, since only Renderer (via FramePresenter)
    // knows ColorFormat().
    RenderTexture CreateRenderTexture(int width, int height, VkFormat format, const char* debugName) const;

    // See Renderer::CreateBuffer().
    Buffer CreateBuffer(VkDeviceSize size, VkBufferUsageFlags usage, BufferMemoryUsage memoryUsage,
        const char* debugName = nullptr) const;

    // See Renderer::CreateDeviceLocalBuffer().
    Buffer CreateDeviceLocalBuffer(
        const void* data, VkDeviceSize size, VkBufferUsageFlags usage, const char* debugName = nullptr) const;

    // See Renderer::ImmediateSubmit().
    void ImmediateSubmit(const std::function<void(VkCommandBuffer)>& recordFn) const;

    // See Renderer::CreatePipeline(). `colorFormat` here is always exactly
    // Renderer::ColorFormat(), passed in by Renderer since only Renderer
    // knows it.
    Pipeline CreatePipeline(VkFormat colorFormat, const std::string& vertexShaderSpirvPath,
        const std::string& fragmentShaderSpirvPath) const;

    // See Renderer::CreateMesh().
    Mesh CreateMesh(const void* vertexData, VkDeviceSize vertexDataSize, std::uint32_t vertexCount,
        const char* debugName = nullptr) const;

    // See Renderer::GetMemoryTotals()/GetMemoryResources().
    GpuMemoryTracker::Totals GetMemoryTotals() const;
    std::vector<GpuMemoryTracker::Entry> GetMemoryResources() const;

private:
    void Destroy() noexcept;

    VkDevice m_device = VK_NULL_HANDLE;
    VmaAllocator m_allocator = VK_NULL_HANDLE;
    VkQueue m_graphicsQueue = VK_NULL_HANDLE;

    // Owned via shared_ptr (not by value) so it can be handed out to every
    // Buffer/RenderTexture this factory creates without any risk of
    // dangling if this factory (or the Renderer owning it) is later moved -
    // see GpuMemoryTracker's class comment.
    std::shared_ptr<GpuMemoryTracker> m_memoryTracker;

    // Dedicated to ImmediateSubmit() - never shared with FramePresenter's
    // own per-frame/offscreen command buffers, so a buffer/mesh upload can
    // never contend with in-flight presentation work.
    VkCommandPool m_commandPool = VK_NULL_HANDLE;
};

} // namespace gte
