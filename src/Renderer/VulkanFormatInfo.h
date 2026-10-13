#pragma once
#include <volk.h>

#include "../Core/Extent.h"
#include "../Core/ImageLayout.h"
#include "../Core/PixelFormat.h"

namespace gte {

// Bytes per texel for every VkFormat this engine creates a render target or
// depth buffer with. Returns 0 for anything else - callers must treat 0 as
// unsupported, never assume a size.
int BytesPerTexelForFormat(VkFormat format) noexcept;

// Renderer-internal only - the ONLY place gte::PixelFormat converts to/from
// a real VkFormat. Unknown/unsupported values map to
// VK_FORMAT_UNDEFINED/PixelFormat::Undefined rather than asserting.
VkFormat ToVkFormat(PixelFormat format) noexcept;
PixelFormat FromVkFormat(VkFormat format) noexcept;

// Renderer-internal only - the ONLY place gte::ImageLayout converts to/from
// a real VkImageLayout (see ImageLayout's own doc comment, Core/ImageLayout.h,
// for exactly which boundary this is and which internals deliberately stay
// raw VkImageLayout instead). Unknown/unsupported values map to
// VK_IMAGE_LAYOUT_UNDEFINED/ImageLayout::Undefined rather than asserting.
VkImageLayout ToVkImageLayout(ImageLayout layout) noexcept;
ImageLayout FromVkImageLayout(VkImageLayout layout) noexcept;

// Renderer-internal only - the ONLY place gte::Extent2D/Extent3D convert
// to/from VkExtent2D/VkExtent3D.
constexpr VkExtent2D ToVkExtent2D(Extent2D e) noexcept
{
    return VkExtent2D{ e.width, e.height };
}
constexpr Extent2D FromVkExtent2D(VkExtent2D e) noexcept
{
    return Extent2D{ e.width, e.height };
}
constexpr VkExtent3D ToVkExtent3D(Extent3D e) noexcept
{
    return VkExtent3D{ e.width, e.height, e.depth };
}
constexpr Extent3D FromVkExtent3D(VkExtent3D e) noexcept
{
    return Extent3D{ e.width, e.height, e.depth };
}

} // namespace gte
