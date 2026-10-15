#include "RenderTexture.h"

#include <cassert>
#include <utility>

namespace gte {

RenderTexture::RenderTexture(VmaAllocator allocator, std::shared_ptr<GpuMemoryTracker> tracker, VkDevice device,
    int width, int height, PixelFormat format, PixelFormat depthFormat, const char* debugName,
    const char* depthDebugName, bool allowStorageImageAccess, bool allowDepthSampledAccess, bool createDepthCompanion,
    bool createColorImage, bool transferSrc, bool transferDst)
    : Texture(allocator, tracker, device,
          TextureCreateInfo{ .width = width,
              .height = height,
              .format = format,
              .renderTarget = true,
              .storage = allowStorageImageAccess,
              .transferSrc = transferSrc,
              .transferDst = transferDst,
              .createImage = createColorImage,
              .debugName = debugName })
{
    assert((createColorImage || createDepthCompanion) && "RenderTexture needs a color image or a depth companion.");
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
    : Texture(std::move(other))
    , m_depth(std::move(other.m_depth))
    , m_samplerObjectHandle(std::exchange(other.m_samplerObjectHandle, CachedSamplerHandle{}))
    , m_depthSamplerObjectHandle(std::exchange(other.m_depthSamplerObjectHandle, CachedSamplerHandle{}))
    , m_currentState(std::exchange(other.m_currentState, rg::ResourceState{}))
{
    other.m_depth.reset();
}

RenderTexture& RenderTexture::operator=(RenderTexture&& other) noexcept
{
    if (this != &other) {
        ReleaseSamplerHandles();
        Texture::operator=(std::move(other));
        m_depth = std::move(other.m_depth);
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
    Texture::Resize(width, height);
    if (m_depth) {
        m_depth->Resize(width, height);
    }
    m_currentState = rg::ResourceState{}; // A new image starts at UNDEFINED.
}

RenderTarget RenderTexture::Target() const noexcept
{
    RenderTarget target;
    target.image = Image();
    target.imageView = View();
    target.extent = ToVkExtent2D(Extent());
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
    assert(Image() != VK_NULL_HANDLE && "FinalizeForExternalSampling: no color image to transition");
    const rg::ResourceState next = rg::RequiredStateFor(rg::ResourceAccess::ShaderRead, false);
    const VkImageSubresourceRange range{ VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 };
    rg::EmitImageBarrier(cmd, Image(), range, m_currentState, next);
    m_currentState = next;
}

} // namespace gte
