#include "RenderGraphDebugVolumeTextureRegistry.h"

#include "../VulkanFormatInfo.h"

namespace gte::rg {

std::string DebugVolumeTextureSnapshot::ColorFormatName() const
{
    return VkFormatDebugName(target.format);
}

} // namespace gte::rg
