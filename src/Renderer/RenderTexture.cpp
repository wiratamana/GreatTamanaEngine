#include "RenderTexture.h"

#include <algorithm>
#include <cassert>
#include <utility>

namespace gte {

RenderTexture::RenderTexture(VmaAllocator allocator, std::shared_ptr<GpuMemoryTracker> tracker, VkDevice device,
    int width, int height, PixelFormat format, PixelFormat depthFormat, const char* debugName,
    const char* depthDebugName, bool allowStorageImageAccess, bool allowDepthSampledAccess, bool createDepthCompanion,
    bool createColorImage)
    : m_format(format)
    , m_extent{ static_cast<std::uint32_t>(std::max(width, 1)), static_cast<std::uint32_t>(std::max(height, 1)) }
{
    assert((createColorImage || createDepthCompanion) && "RenderTexture needs a color image or a depth companion.");
    if (createColorImage) {
        m_color.emplace(allocator, tracker, device,
            TextureCreateInfo{ .width = width,
                .height = height,
                .format = format,
                .renderTarget = true,
                .storage = allowStorageImageAccess,
                .debugName = debugName });
    }
    if (createDepthCompanion) {
        m_depth.emplace(
            allocator, std::move(tracker), device, width, height, depthFormat, depthDebugName, allowDepthSampledAccess);
    }
}

RenderTexture::~RenderTexture()
{
    ReleaseSamplerHandles();
}

RenderTexture::RenderTexture(RenderTexture&& other) noexcept
    : m_format(other.m_format)
    , m_extent(other.m_extent)
    , m_color(std::move(other.m_color))
    , m_depth(std::move(other.m_depth))
    , m_samplerObjectHandle(std::exchange(other.m_samplerObjectHandle, CachedSamplerHandle{}))
    , m_depthSamplerObjectHandle(std::exchange(other.m_depthSamplerObjectHandle, CachedSamplerHandle{}))
    , m_currentState(std::exchange(other.m_currentState, rg::ResourceState{}))
{
    other.m_color.reset();
    other.m_depth.reset();
}

RenderTexture& RenderTexture::operator=(RenderTexture&& other) noexcept
{
    if (this != &other) {
        ReleaseSamplerHandles();
        m_format = other.m_format;
        m_extent = other.m_extent;
        m_color = std::move(other.m_color);
        m_depth = std::move(other.m_depth);
        other.m_color.reset();
        other.m_depth.reset();
        m_samplerObjectHandle = std::exchange(other.m_samplerObjectHandle, CachedSamplerHandle{});
        m_depthSamplerObjectHandle = std::exchange(other.m_depthSamplerObjectHandle, CachedSamplerHandle{});
        m_currentState = std::exchange(other.m_currentState, rg::ResourceState{});
    }
    return *this;
}

void RenderTexture::ReleaseSamplerHandles() noexcept
{
    // Unregister before the VkSampler goes away so the registry never holds a stale entry.
    m_samplerObjectHandle.Resolve(VK_NULL_HANDLE);
    m_depthSamplerObjectHandle.Resolve(VK_NULL_HANDLE);
}

void RenderTexture::Resize(int width, int height)
{
    ReleaseSamplerHandles();
    if (m_color) {
        m_color->Resize(width, height);
    }
    if (m_depth) {
        m_depth->Resize(width, height);
    }
    m_extent = Extent2D{ static_cast<std::uint32_t>(std::max(width, 1)), static_cast<std::uint32_t>(std::max(height, 1)) };
    m_currentState = rg::ResourceState{}; // A new image starts at UNDEFINED.
}

RenderTarget RenderTexture::Target() const noexcept
{
    RenderTarget target;
    target.image = Image();
    target.imageView = View();
    target.extent = ToVkExtent2D(m_extent);
    target.format = NativeFormat();
    if (m_depth) {
        target.depthImage = m_depth->Image();
        target.depthImageView = m_depth->View();
        target.depthFormat = m_depth->NativeFormat();
        target.depthHasStencil = m_depth->HasStencilComponent();
    }
    return target;
}

void RenderTexture::FinalizeForExternalSampling(VkCommandBuffer cmd)
{
    assert(m_color && "FinalizeForExternalSampling: no color image to transition");
    const rg::ResourceState next = rg::RequiredStateFor(rg::ResourceAccess::ShaderRead, false);
    const VkImageSubresourceRange range{ VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 };
    rg::EmitImageBarrier(cmd, m_color->Image(), range, m_currentState, next);
    m_currentState = next;
}

} // namespace gte
