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

} // namespace gte
