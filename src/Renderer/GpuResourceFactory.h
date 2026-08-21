#pragma once

#include "Buffer.h"
#include "Memory/GpuMemoryTracker.h"
#include "Mesh.h"
#include "Pipeline.h"
#include "RenderTexture.h"
#include "Texture2D.h"
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
        std::uint32_t graphicsQueueFamily, VkFormat depthFormat, std::shared_ptr<GpuMemoryTracker> memoryTracker);
    ~GpuResourceFactory();

    GpuResourceFactory(const GpuResourceFactory&) = delete;
    GpuResourceFactory& operator=(const GpuResourceFactory&) = delete;

    GpuResourceFactory(GpuResourceFactory&& other) noexcept;
    GpuResourceFactory& operator=(GpuResourceFactory&& other) noexcept;

    // See Renderer::CreateRenderTexture(). `format` here must already be
    // fully resolved - VK_FORMAT_UNDEFINED-as-"match ColorFormat()" is
    // resolved by Renderer itself, since only Renderer (via FramePresenter)
    // knows ColorFormat(). depthDebugName optionally names the companion
    // DepthBuffer separately from the color image's debugName - see
    // RenderTexture's constructor comment.
    RenderTexture CreateRenderTexture(
        int width, int height, VkFormat format, const char* debugName, const char* depthDebugName = nullptr) const;

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

    // See Renderer::CreateTexture2D(). `pixelsRgba8` must be
    // width*height*4 tightly-packed bytes (e.g. straight out of
    // stbi_load(..., desired_channels=4)) - uploaded via a temporary
    // staging Buffer + ImmediateSubmit(), the same pattern
    // CreateDeviceLocalBuffer() above already uses.
    Texture2D CreateTexture2D(const void* pixelsRgba8, int width, int height, const char* debugName = nullptr) const;

    // See Renderer::GetMemoryTotals()/GetMemoryResources().
    GpuMemoryTracker::Totals GetMemoryTotals() const;
    std::vector<GpuMemoryTracker::Entry> GetMemoryResources() const;

#if GTE_ENABLE_EDITOR
    // See Renderer::GetMemoryDebugName() - Editor-only, forwards straight to
    // GpuMemoryTracker::GetDebugName(). Compiled out entirely when
    // GTE_ENABLE_EDITOR is OFF, same as GpuMemoryTracker's own debug-name
    // storage (see AGENTS.md, "GPU Resource Memory Tracking").
    const std::string& GetMemoryDebugName(GpuResourceHandle handle) const;
#endif

private:
    void Destroy() noexcept;

    VkDevice m_device = VK_NULL_HANDLE;
    VmaAllocator m_allocator = VK_NULL_HANDLE;
    VkQueue m_graphicsQueue = VK_NULL_HANDLE;

    // The single depth format every RenderTexture/Pipeline this factory
    // creates is built against - always exactly Renderer::DepthFormat()
    // (VulkanDevice::PickDepthFormat()), stored once here rather than
    // threaded through every CreateRenderTexture()/CreatePipeline() call
    // (unlike colorFormat, which a caller can deliberately override per
    // RenderTexture - see CreateRenderTexture() above - depth never needs
    // that: it's never sampled/displayed, so there's no reason for it to
    // ever differ from the one shared format). See AGENTS.md ("Render
    // Target Format Matching").
    VkFormat m_depthFormat = VK_FORMAT_UNDEFINED;

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
