#pragma once

#include <cstdint>

namespace gte {

// Engine-native pixel/depth format, free of Vulkan. Renderer/VulkanFormatInfo is the only
// place that converts to or from a real VkFormat.
enum class PixelFormat : std::uint8_t {
    Undefined,
    RGBA8_UNorm,
    BGRA8_UNorm,
    RGBA16_Float,
    R8_UNorm,
    D32_Float,
    D32_Float_S8_UInt,
    D24_UNorm_S8_UInt,
    RGBA8_SRGB, // Sampling decodes sRGB to linear in hardware.
};

// Human-readable label of a PixelFormat; "Unknown" for a value outside the enum.
inline const char* ToString(PixelFormat format) noexcept
{
    switch (format) {
    case PixelFormat::Undefined: return "Undefined";
    case PixelFormat::RGBA8_UNorm: return "RGBA8_UNorm";
    case PixelFormat::BGRA8_UNorm: return "BGRA8_UNorm";
    case PixelFormat::RGBA16_Float: return "RGBA16_Float";
    case PixelFormat::R8_UNorm: return "R8_UNorm";
    case PixelFormat::D32_Float: return "D32_Float";
    case PixelFormat::D32_Float_S8_UInt: return "D32_Float_S8_UInt";
    case PixelFormat::D24_UNorm_S8_UInt: return "D24_UNorm_S8_UInt";
    case PixelFormat::RGBA8_SRGB: return "RGBA8_SRGB";
    }
    return "Unknown";
}

} // namespace gte
