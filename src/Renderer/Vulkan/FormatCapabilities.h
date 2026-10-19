#pragma once

#include <volk.h>

namespace gte {

// True if `format` supports storage-image use on `physicalDevice`. Not every format does (a swapchain
// format such as B8G8R8A8_UNORM may not), so callers requesting storage access must check this and fail
// loudly (throw), never silently fall back.
bool SupportsStorageImageUsage(VkPhysicalDevice physicalDevice, VkFormat format);

// True if `format` can be sampled with linear filtering on `physicalDevice`.
bool SupportsLinearSampling(VkPhysicalDevice physicalDevice, VkFormat format);

// True if `format` supports both BLIT_SRC and BLIT_DST on `physicalDevice`. Depth-format blit support is
// not guaranteed by the spec and is often missing on real drivers; see VulkanDevice::SupportsDepthBlit().
bool SupportsBlitSrcDst(VkPhysicalDevice physicalDevice, VkFormat format);

} // namespace gte
