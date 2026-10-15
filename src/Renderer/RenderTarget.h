#pragma once

#include <volk.h>

namespace gte {

// Non-owning description of a color+depth place to render into: a swapchain image or a RenderTexture,
// each with its depth buffer. Built per call from Vulkan handles; owns nothing.
struct RenderTarget {
    VkImage image = VK_NULL_HANDLE;
    VkImageView imageView = VK_NULL_HANDLE;
    VkExtent2D extent{};
    VkFormat format = VK_FORMAT_UNDEFINED;

    // Depth counterpart; its format must match the one the pipelines were built with (Renderer::DepthFormat()).
    VkImage depthImage = VK_NULL_HANDLE;
    VkImageView depthImageView = VK_NULL_HANDLE;
    VkFormat depthFormat = VK_FORMAT_UNDEFINED;
    bool depthHasStencil = false;
};

} // namespace gte
