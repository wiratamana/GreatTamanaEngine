#pragma once

#include <volk.h>
#include <cstddef>
#include <span>
#include <string>

namespace gte {

// Loads a compiled SPIR-V binary (a path to the .spv, not GLSL source) into a VkShaderModule, the one
// primitive both graphics and compute pipelines build from. The caller destroys the module once its
// pipeline is built. Throws std::runtime_error if the file cannot be read or creation fails.
VkShaderModule LoadShaderModule(VkDevice device, const std::string& spirvPath);

// Same contract for SPIR-V that is already in memory.
VkShaderModule LoadShaderModule(VkDevice device, std::span<const std::byte> spirv);

} // namespace gte
