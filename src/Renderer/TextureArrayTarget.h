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
// Two kinds of VkImageView are exposed: `imageView` (the WHOLE array/cube)
// and `layerViews` (one per layer, via LayerView()) - both owned and
// destroyed by the TextureArray2D instance this struct describes, never by a
// consumer of this struct. `layerViews` is a non-owning pointer into that
// instance's own, stable per-layer view storage - safe to hold onto for as
// long as the owning TextureArray2D is alive.
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

    // Sampler suitable for reading this array/cubemap in a shader - see
    // TextureArray2D::Sampler() for its exact policy.
    VkSampler sampler = VK_NULL_HANDLE;

    // Non-owning pointer into the owning TextureArray2D's own per-layer view
    // vector (built once, at construction - see TextureArray2D.h). Stable for
    // as long as the owning TextureArray2D is alive (a pooled instance lives
    // inside RenderGraphResourcePool's own std::deque, which never relocates
    // already-constructed elements).
    const VkImageView* layerViews = nullptr;
    std::uint32_t layerViewCount = 0;

    // Bounds-checked accessor - VK_NULL_HANDLE if layerViews is null or
    // layerIndex is out of range.
    VkImageView LayerView(std::uint32_t layerIndex) const noexcept
    {
        return (layerViews != nullptr && layerIndex < layerViewCount) ? layerViews[layerIndex] : VK_NULL_HANDLE;
    }
};

} // namespace gte
