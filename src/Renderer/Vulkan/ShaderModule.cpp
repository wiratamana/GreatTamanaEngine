#include "ShaderModule.h"

#include "../ShaderFile.h"

#include <cstdint>
#include <stdexcept>
#include <vector>

namespace gte {

namespace {

VkShaderModule CreateModuleFromBytes(VkDevice device, const void* code, std::size_t codeSize)
{
    VkShaderModuleCreateInfo createInfo{};
    createInfo.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
    createInfo.codeSize = codeSize;
    createInfo.pCode = reinterpret_cast<const std::uint32_t*>(code);

    VkShaderModule module = VK_NULL_HANDLE;
    if (vkCreateShaderModule(device, &createInfo, nullptr, &module) != VK_SUCCESS) {
        throw std::runtime_error("ShaderModule: vkCreateShaderModule failed.");
    }
    return module;
}

} // namespace

VkShaderModule LoadShaderModule(VkDevice device, const std::string& spirvPath)
{
    const std::vector<std::byte> spirv = ReadShaderFileBytes(spirvPath);
    return CreateModuleFromBytes(device, spirv.data(), spirv.size());
}

VkShaderModule LoadShaderModule(VkDevice device, std::span<const std::byte> spirv)
{
    return CreateModuleFromBytes(device, spirv.data(), spirv.size());
}

} // namespace gte
