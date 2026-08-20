#pragma once

#include <volk.h>

namespace gte {

// Plain, non-owning description of "a place to render color+depth into" for
// a single Renderer pass: either the swapchain image for the current frame
// (paired with one of FramePresenter's own per-swapchain-image DepthBuffers)
// or a RenderTexture's backing image (paired with that RenderTexture's own
// DepthBuffer - see RenderTexture.h/DepthBuffer.h). FrameRecorder's internal
// draw recording (RecordFrame) works against this alone, so it never needs
// two copies of the same clear/barrier logic for "drawing to the window" vs.
// "drawing to an off-screen texture".
//
// Deliberately just Vulkan handles + metadata, no ownership - Renderer
// builds one of these on the fly each call (from the swapchain + its own
// depth image, or from a RenderTexture + its own DepthBuffer passed in by
// the caller).
struct RenderTarget {
    VkImage image = VK_NULL_HANDLE;
    VkImageView imageView = VK_NULL_HANDLE;
    VkExtent2D extent{};
    VkFormat format = VK_FORMAT_UNDEFINED;

    // Depth counterpart - see DepthBuffer.h. Populated by every caller that
    // builds a RenderTarget today (FramePresenter::Present()/
    // RenderOffscreen()), so real (non-coplanar) 3D geometry is correctly
    // depth-tested/occluded instead of drawing in whatever order it happened
    // to be submitted in - see AGENTS.md ("Render Target Format Matching")
    // for why depthFormat must match whatever the shared Pipeline was built
    // against (Renderer::DepthFormat()).
    VkImage depthImage = VK_NULL_HANDLE;
    VkImageView depthImageView = VK_NULL_HANDLE;
    VkFormat depthFormat = VK_FORMAT_UNDEFINED;
    bool depthHasStencil = false;
};

} // namespace gte
