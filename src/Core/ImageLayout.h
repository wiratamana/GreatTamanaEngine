#pragma once

#include <cstdint>

namespace gte {

// Engine-native image-layout state - zero Vulkan dependency, usable from any
// layer (RenderGraphBuilder::ImportTexture()/ImportVolumeTexture()/
// ImportTextureArray(), and every call site across Core/Editor/Features that
// used to pass a raw VK_IMAGE_LAYOUT_* literal directly). Renderer/
// VulkanFormatInfo.h is the ONLY place this ever converts to/from a real
// VkImageLayout (see ToVkImageLayout()/FromVkImageLayout() there) - mirrors
// PixelFormat's own "one translation choke point" convention exactly (see
// Core/PixelFormat.h and Renderer/VulkanFormatInfo.h).
//
// Only the subset of VkImageLayout this engine's IMPORT boundary (the
// "what layout is this already-live external resource in right now"
// statement at ImportTexture()/ImportVolumeTexture()/ImportTextureArray()
// call time, plus RenderGraphPersistentResourceCache's own honest
// cross-frame layout history feeding back into it) actually has a real use
// for today is declared - extend this (and ToVkImageLayout()/
// FromVkImageLayout()) if a future feature genuinely needs another
// VK_IMAGE_LAYOUT_* value at this boundary. Deep Vulkan-synchronization
// internals (RenderGraphBarrierPlanner::ResourceState, RenderGraph.h's own
// PhysicalTexture/PhysicalVolumeTexture/PhysicalTextureArray tracking, the
// raw VkImageMemoryBarrier2 fields themselves) are NOT this boundary - those
// remain real VkImageLayout, exactly like RenderTarget/VolumeTarget/
// TextureArrayTarget deliberately stay raw VkFormat (internal plumbing).
enum class ImageLayout : std::uint8_t {
    // The caller has no meaningful prior state to report - e.g. the first
    // pass this frame fully overwrites the resource via a clear load op, so
    // whatever the image's previous layout was is irrelevant.
    Undefined,
    // Read/write via imageLoad/imageStore (a storage image bound to a
    // compute shader) - the only layout a storage image can be accessed
    // through in core Vulkan.
    General,
    ColorAttachmentOptimal,
    DepthStencilAttachmentOptimal,
    ShaderReadOnlyOptimal,
    TransferSrcOptimal,
    TransferDstOptimal,
    // The swapchain-presentation layout (VK_IMAGE_LAYOUT_PRESENT_SRC_KHR) -
    // never actually passed INTO ImportTexture() today (FramePresenter
    // always imports the swapchain image as Undefined, then the render
    // graph itself transitions it to this layout before present), but
    // declared here so RenderGraphPersistentResourceCache's own honest
    // layout history can represent it if a future entry is ever left in it.
    PresentSrc,
};

} // namespace gte
