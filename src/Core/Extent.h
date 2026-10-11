#pragma once

#include <cstdint>

namespace gte {

// Plain, cache-friendly size/offset types - zero Vulkan dependency, usable
// from any layer. Renderer/Vulkan/VulkanFormatInfo.h is the ONLY place these
// convert to/from VkExtent2D/VkExtent3D/VkOffset3D.
struct Extent2D {
    std::uint32_t width = 0;
    std::uint32_t height = 0;
};

struct Extent3D {
    std::uint32_t width = 0;
    std::uint32_t height = 0;
    std::uint32_t depth = 1;
};

struct Offset3D {
    std::int32_t x = 0;
    std::int32_t y = 0;
    std::int32_t z = 0;
};

} // namespace gte
