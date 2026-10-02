#pragma once

#include <volk.h>

#include <cstdint>

namespace gte {

// Plain, non-owning description of a 2D array / cubemap image - the
// TextureArray2D (see TextureArray2D.h) counterpart of RenderTarget.h (2D
// color+depth target) and VolumeTarget.h (true 3D image). Deliberately just
// Vulkan handles + metadata, no ownership, same layering discipline as those
// two: this is what lets RenderGraphBuilder.h stay decoupled from
// TextureArray2D.h's own (heavier) header.
//
// There is only ONE VkImageView here, covering the WHOLE array/cube - see
// TextureArray2D's own header comment for exactly why there is no
// per-layer/per-face view in this resource yet (better-render-pass-3
// campaign, BLOCK5, Section 5).
struct TextureArrayTarget {
    VkImage image = VK_NULL_HANDLE;
    VkImageView imageView = VK_NULL_HANDLE; // whole-array/whole-cube view - see TextureArray2D.h
    VkExtent2D extent{};                     // per-layer width/height (every layer is the same size)
    std::uint32_t arrayLayers = 1;
    VkFormat format = VK_FORMAT_UNDEFINED;
    // Which aspect every layer of this image is - lets a consumer tell which
    // aspect this view exposes without a second lookup (mirrors
    // TextureArrayDesc::hasDepth's own meaning exactly - see
    // RenderGraphTypes.h once a future phase adds TextureArrayDesc).
    bool hasDepth = false;
};

} // namespace gte
