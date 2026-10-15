#pragma once

#include <volk.h>

namespace gte {

// Non-owning description of a 3D image (handles + metadata), the volume counterpart of RenderTarget.
// Lets the render graph builder import volumes without including VolumeTexture.h.
struct VolumeTarget {
    VkImage image = VK_NULL_HANDLE;
    VkImageView imageView = VK_NULL_HANDLE;
    VkExtent3D extent{};
    VkFormat format = VK_FORMAT_UNDEFINED;
};

} // namespace gte
