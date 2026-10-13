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

VkImageLayout ToVkImageLayout(ImageLayout layout) noexcept
{
    switch (layout) {
        case ImageLayout::General:
            return VK_IMAGE_LAYOUT_GENERAL;
        case ImageLayout::ColorAttachmentOptimal:
            return VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
        case ImageLayout::DepthStencilAttachmentOptimal:
            return VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
        case ImageLayout::ShaderReadOnlyOptimal:
            return VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        case ImageLayout::TransferSrcOptimal:
            return VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
        case ImageLayout::TransferDstOptimal:
            return VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
        case ImageLayout::PresentSrc:
            return VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
        case ImageLayout::Undefined:
        default:
            return VK_IMAGE_LAYOUT_UNDEFINED;
    }
}

ImageLayout FromVkImageLayout(VkImageLayout layout) noexcept
{
    switch (layout) {
        case VK_IMAGE_LAYOUT_GENERAL:
            return ImageLayout::General;
        case VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL:
            return ImageLayout::ColorAttachmentOptimal;
        case VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL:
            return ImageLayout::DepthStencilAttachmentOptimal;
        case VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL:
            return ImageLayout::ShaderReadOnlyOptimal;
        case VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL:
            return ImageLayout::TransferSrcOptimal;
        case VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL:
            return ImageLayout::TransferDstOptimal;
        case VK_IMAGE_LAYOUT_PRESENT_SRC_KHR:
            return ImageLayout::PresentSrc;
        default:
            return ImageLayout::Undefined;
    }
}

} // namespace gte
