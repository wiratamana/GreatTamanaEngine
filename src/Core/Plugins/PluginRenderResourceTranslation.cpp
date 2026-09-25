#include "PluginRenderResourceTranslation.h"

namespace gte {

rg::ResourceAccess ToRgAccess(PluginResourceAccess access) noexcept
{
    switch (access) {
    case PluginResourceAccess::ColorAttachmentWrite:
        return rg::ResourceAccess::ColorAttachmentWrite;
    case PluginResourceAccess::ShaderRead:
        return rg::ResourceAccess::ShaderRead;
    case PluginResourceAccess::ComputeShaderRead:
        return rg::ResourceAccess::ComputeShaderRead;
    case PluginResourceAccess::ComputeShaderWrite:
        return rg::ResourceAccess::ComputeShaderWrite;
    }
    // Unreachable in practice - the switch above is exhaustive over all 4
    // current PluginResourceAccess enumerators with no default: case, so
    // this line is only ever reached if a caller somehow constructed an
    // out-of-range PluginResourceAccess value directly. Mirrors
    // rg::DispatchByKind()'s own "fail loudly rather than guess" precedent
    // (RenderGraphTypes.h) - there is no safe sentinel rg::ResourceAccess
    // value to fall back to here.
    return rg::ResourceAccess::ShaderRead;
}

rg::TextureDesc ToRgTextureDesc(const PluginTextureDesc& desc) noexcept
{
    rg::TextureDesc result;
    result.width = desc.width;
    result.height = desc.height;
    result.hasDepth = false;

    switch (desc.format) {
    case PluginTextureDesc::Format::Rgba8Unorm:
        result.format = VK_FORMAT_R8G8B8A8_UNORM;
        break;
    case PluginTextureDesc::Format::Rgba16Float:
        result.format = VK_FORMAT_R16G16B16A16_SFLOAT;
        break;
    case PluginTextureDesc::Format::R32Float:
        result.format = VK_FORMAT_R32_SFLOAT;
        break;
    }
    return result;
}

rg::BufferDesc ToRgBufferDesc(const PluginBufferDesc& desc) noexcept
{
    rg::BufferDesc result;
    result.size = static_cast<VkDeviceSize>(desc.sizeBytes);
    result.usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT;
    return result;
}

} // namespace gte
