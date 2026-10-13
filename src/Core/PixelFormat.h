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

// Human-readable label for each PixelFormat value above - the engine-native
// equivalent of Editor/MemoryPanelData.h's ToString(VkFormat), but zero
// Vulkan dependency (no VkFormat involved, no ToVkFormat() round-trip
// needed) so it can be called from any layer, exactly like PixelFormat
// itself (see the file-level comment above). Falls back to "Unknown" only
// for a value outside the enum's real range (e.g. a corrupted/garbage
// byte) - every declared enumerator above has its own case.
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
    }
    return "Unknown";
}

} // namespace gte
