#pragma once

#include <volk.h>

namespace gte {

// Plain, non-owning description of "a place to render color into" for a
// single Renderer pass: either the swapchain image for the current frame,
// or a RenderTexture's backing image (see RenderTexture.h). Renderer's
// internal draw recording (RecordClearAndTransition) works against this
// alone, so it never needs two copies of the same clear/barrier logic for
// "drawing to the window" vs. "drawing to an off-screen texture".
//
// Deliberately just Vulkan handles + metadata, no ownership - Renderer
// builds one of these on the fly each call (from the swapchain, or from a
// RenderTexture passed in by the caller).
struct RenderTarget {
    VkImage image = VK_NULL_HANDLE;
    VkImageView imageView = VK_NULL_HANDLE;
    VkExtent2D extent{};
    VkFormat format = VK_FORMAT_UNDEFINED;
};

} // namespace gte
