#pragma once

#include "../../Core/PipelineDesc.h"

#include <volk.h>

namespace gte {

// Engine stage mask -> Vulkan stage flags.
constexpr VkShaderStageFlags ToVkShaderStageFlags(ShaderStageMask mask) noexcept
{
    VkShaderStageFlags flags = 0;
    if (HasStage(mask, ShaderStageMask::Vertex)) {
        flags |= VK_SHADER_STAGE_VERTEX_BIT;
    }
    if (HasStage(mask, ShaderStageMask::Fragment)) {
        flags |= VK_SHADER_STAGE_FRAGMENT_BIT;
    }
    if (HasStage(mask, ShaderStageMask::Compute)) {
        flags |= VK_SHADER_STAGE_COMPUTE_BIT;
    }
    return flags;
}

// Vulkan stage flags -> engine stage mask. Stages the engine does not model are dropped.
constexpr ShaderStageMask ToShaderStageMask(VkShaderStageFlags flags) noexcept
{
    ShaderStageMask mask = ShaderStageMask::None;
    if ((flags & VK_SHADER_STAGE_VERTEX_BIT) != 0) {
        mask = mask | ShaderStageMask::Vertex;
    }
    if ((flags & VK_SHADER_STAGE_FRAGMENT_BIT) != 0) {
        mask = mask | ShaderStageMask::Fragment;
    }
    if ((flags & VK_SHADER_STAGE_COMPUTE_BIT) != 0) {
        mask = mask | ShaderStageMask::Compute;
    }
    return mask;
}

} // namespace gte
