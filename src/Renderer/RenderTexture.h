#pragma once

#include "../Core/PixelFormat.h"
#include "DepthBuffer.h"
#include "GraphicsHandleRegistry.h" // CachedSamplerHandle
#include "RenderGraph/RenderGraphBarrierPlanner.h"
#include "RenderTarget.h"
#include "Texture.h"

#include <memory>
#include <optional>

namespace gte {

// Off-screen render target: an optional color Texture plus an optional companion DepthBuffer.
// A depth-only RenderTexture (shadow map) simply has no color image.
class RenderTexture {
public:
    // At least one of createColorImage/createDepthCompanion must be true.
    // debugName/depthDebugName are stored by pointer and must outlive this object.
    RenderTexture(VmaAllocator allocator, std::shared_ptr<GpuMemoryTracker> tracker, VkDevice device, int width,
        int height, PixelFormat format = PixelFormat::BGRA8_UNorm, PixelFormat depthFormat = PixelFormat::D32_Float,
        const char* debugName = nullptr, const char* depthDebugName = nullptr, bool allowStorageImageAccess = false,
        bool allowDepthSampledAccess = false, bool createDepthCompanion = true, bool createColorImage = true);
    ~RenderTexture();

    RenderTexture(const RenderTexture&) = delete;
    RenderTexture& operator=(const RenderTexture&) = delete;
    RenderTexture(RenderTexture&& other) noexcept;
    RenderTexture& operator=(RenderTexture&& other) noexcept;

    // Recreates color and depth at a new size. Handle() changes; contents are undefined.
    void Resize(int width, int height);

    RenderTarget Target() const noexcept;

    // Null when this RenderTexture has no color image.
    VkImage Image() const noexcept { return m_color ? m_color->Image() : VK_NULL_HANDLE; }
    VkImageView View() const noexcept { return m_color ? m_color->View() : VK_NULL_HANDLE; }
    VkSampler Sampler() const noexcept { return m_color ? m_color->Sampler() : VK_NULL_HANDLE; }
    // Null unless the depth companion was created with allowDepthSampledAccess.
    VkSampler DepthSampler() const noexcept { return m_depth ? m_depth->Sampler() : VK_NULL_HANDLE; }

    Extent2D Extent() const noexcept { return m_extent; }
    PixelFormat Format() const noexcept { return m_format; }
    VkFormat NativeFormat() const noexcept { return ToVkFormat(m_format); }
    GpuResourceHandle Handle() const noexcept { return m_color ? m_color->Handle() : kInvalidGpuResourceHandle; }
    bool AllowsStorageImageAccess() const noexcept { return m_color && m_color->AllowsStorageImageAccess(); }

    // Opaque sampler handles for RenderGraphBuilder::ImportTexture(); re-registered automatically after Resize().
    SamplerHandle SamplerObjectHandle() const noexcept { return m_samplerObjectHandle.Resolve(Sampler()); }
    SamplerHandle DepthSamplerObjectHandle() const noexcept { return m_depthSamplerObjectHandle.Resolve(DepthSampler()); }

    // Last known GPU barrier state of the color image; written back by the render graph.
    rg::ResourceState CurrentState() const noexcept { return m_currentState; }
    void SetCurrentState(rg::ResourceState state) noexcept { m_currentState = state; }

    // Transitions the color image from CurrentState() to ShaderRead.
    void FinalizeForExternalSampling(VkCommandBuffer cmd);

private:
    void ReleaseSamplerHandles() noexcept;

    PixelFormat m_format = PixelFormat::BGRA8_UNorm;
    Extent2D m_extent{};
    std::optional<Texture> m_color;
    std::optional<DepthBuffer> m_depth;

    mutable CachedSamplerHandle m_samplerObjectHandle;
    mutable CachedSamplerHandle m_depthSamplerObjectHandle;
    rg::ResourceState m_currentState{};
};

} // namespace gte
