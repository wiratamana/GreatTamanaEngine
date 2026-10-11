#pragma once

#include <cstdint>

namespace gte {

// Engine-native pixel/depth format - zero Vulkan dependency, usable from any
// layer. Renderer/Vulkan/VulkanFormatInfo.h is the ONLY place this converts
// to/from a real VkFormat.
enum class PixelFormat : std::uint8_t {
    Undefined,
    RGBA8_UNorm,
    BGRA8_UNorm,
    RGBA16_Float,
    R8_UNorm,
    D32_Float,
    D32_Float_S8_UInt,
    D24_UNorm_S8_UInt,
};

} // namespace gte
