#include "ShaderModule.h"

#include <cstdint>
#include <fstream>
#include <stdexcept>
#include <vector>

namespace gte {

namespace {

std::vector<char> ReadFile(const std::string& path)
{
    std::ifstream file(path, std::ios::ate | std::ios::binary);
    if (!file.is_open()) {
        throw std::runtime_error(
            "ShaderModule: failed to open shader file '" + path + "' - was it compiled? See cmake/CompileShaders.cmake.");
    }

    const std::size_t size = static_cast<std::size_t>(file.tellg());
    std::vector<char> buffer(size);
    file.seekg(0);
    file.read(buffer.data(), static_cast<std::streamsize>(size));
    return buffer;
}

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
    const std::vector<char> spirv = ReadFile(spirvPath);
    return CreateModuleFromBytes(device, spirv.data(), spirv.size());
}

VkShaderModule LoadShaderModule(VkDevice device, std::span<const std::byte> spirv)
{
    return CreateModuleFromBytes(device, spirv.data(), spirv.size());
}

} // namespace gte
