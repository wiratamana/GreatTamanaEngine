#include "VulkanFormatInfo.h"

namespace gte {

int BytesPerTexelForFormat(VkFormat format) noexcept
{
    switch (format) {
        case VK_FORMAT_R8_UNORM:
            return 1;
        case VK_FORMAT_R8G8B8A8_UNORM:
        case VK_FORMAT_R8G8B8A8_SRGB:
        case VK_FORMAT_B8G8R8A8_UNORM:
        case VK_FORMAT_B8G8R8A8_SRGB:
        case VK_FORMAT_D32_SFLOAT:
        case VK_FORMAT_D32_SFLOAT_S8_UINT:
        case VK_FORMAT_D24_UNORM_S8_UINT:
            return 4;
        case VK_FORMAT_R16G16B16A16_SFLOAT:
            return 8;
        default:
            return 0;
    }
}

VkFormat ToVkFormat(PixelFormat format) noexcept
{
    switch (format) {
        case PixelFormat::RGBA8_UNorm:
            return VK_FORMAT_R8G8B8A8_UNORM;
        case PixelFormat::BGRA8_UNorm:
            return VK_FORMAT_B8G8R8A8_UNORM;
        case PixelFormat::RGBA16_Float:
            return VK_FORMAT_R16G16B16A16_SFLOAT;
        case PixelFormat::R8_UNorm:
            return VK_FORMAT_R8_UNORM;
        case PixelFormat::D32_Float:
            return VK_FORMAT_D32_SFLOAT;
        case PixelFormat::D32_Float_S8_UInt:
            return VK_FORMAT_D32_SFLOAT_S8_UINT;
        case PixelFormat::D24_UNorm_S8_UInt:
            return VK_FORMAT_D24_UNORM_S8_UINT;
        case PixelFormat::Undefined:
        default:
            return VK_FORMAT_UNDEFINED;
    }
}

PixelFormat FromVkFormat(VkFormat format) noexcept
{
    switch (format) {
        case VK_FORMAT_R8G8B8A8_UNORM:
            return PixelFormat::RGBA8_UNorm;
        case VK_FORMAT_B8G8R8A8_UNORM:
            return PixelFormat::BGRA8_UNorm;
        case VK_FORMAT_R16G16B16A16_SFLOAT:
            return PixelFormat::RGBA16_Float;
        case VK_FORMAT_R8_UNORM:
            return PixelFormat::R8_UNorm;
        case VK_FORMAT_D32_SFLOAT:
            return PixelFormat::D32_Float;
        case VK_FORMAT_D32_SFLOAT_S8_UINT:
            return PixelFormat::D32_Float_S8_UInt;
        case VK_FORMAT_D24_UNORM_S8_UINT:
            return PixelFormat::D24_UNorm_S8_UInt;
        default:
            return PixelFormat::Undefined;
    }
}

} // namespace gte
