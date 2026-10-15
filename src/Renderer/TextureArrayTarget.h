#pragma once

#include <volk.h>

#include <cstdint>

namespace gte {

// Non-owning description of a 2D array or cubemap image (the TextureArray2D counterpart of RenderTarget).
// `imageView` covers the whole array; per-layer views come from LayerView(). All views are owned by the
// TextureArray2D this describes.
struct TextureArrayTarget {
    VkImage image = VK_NULL_HANDLE;
    VkImageView imageView = VK_NULL_HANDLE; // whole-array/whole-cube view - see TextureArray2D.h
    VkExtent2D extent{};                     // per-layer width/height (every layer is the same size)
    std::uint32_t arrayLayers = 1;
    VkFormat format = VK_FORMAT_UNDEFINED;
    // True for a depth array, false for a color array.
    bool hasDepth = false;

    // Sampler suitable for reading this array/cubemap in a shader - see
    // TextureArray2D::Sampler() for its exact policy.
    VkSampler sampler = VK_NULL_HANDLE;

    // Non-owning pointer into the owning TextureArray2D's per-layer views; valid while it is alive.
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
