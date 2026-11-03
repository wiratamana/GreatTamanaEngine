#pragma once

#include <volk.h>

#include "../Core/PipelineDesc.h"
#include "Pipeline.h"

namespace gte {

inline VkCullModeFlags ToVkCullMode(CullMode cullMode) noexcept
{
    switch (cullMode) {
        case CullMode::Front:
            return VK_CULL_MODE_FRONT_BIT;
        case CullMode::Back:
            return VK_CULL_MODE_BACK_BIT;
        case CullMode::None:
        default:
            return VK_CULL_MODE_NONE;
    }
}

inline VkCompareOp ToVkCompareOp(CompareOp op) noexcept
{
    switch (op) {
        case CompareOp::Never:
            return VK_COMPARE_OP_NEVER;
        case CompareOp::Less:
            return VK_COMPARE_OP_LESS;
        case CompareOp::Equal:
            return VK_COMPARE_OP_EQUAL;
        case CompareOp::LessEqual:
            return VK_COMPARE_OP_LESS_OR_EQUAL;
        case CompareOp::Greater:
            return VK_COMPARE_OP_GREATER;
        case CompareOp::GreaterEqual:
            return VK_COMPARE_OP_GREATER_OR_EQUAL;
        case CompareOp::Always:
        default:
            return VK_COMPARE_OP_ALWAYS;
    }
}

// Blend attachment state for a BlendMode; Additive is SRC_ALPHA + ONE (used by fullscreen passes).
inline VkPipelineColorBlendAttachmentState ToVkBlendState(BlendMode mode) noexcept
{
    VkPipelineColorBlendAttachmentState state = OpaqueBlendAttachment();
    switch (mode) {
        case BlendMode::AlphaBlend:
            state.blendEnable = VK_TRUE;
            state.srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
            state.dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
            state.colorBlendOp = VK_BLEND_OP_ADD;
            state.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
            state.dstAlphaBlendFactor = VK_BLEND_FACTOR_ZERO;
            state.alphaBlendOp = VK_BLEND_OP_ADD;
            break;
        case BlendMode::Additive:
            state.blendEnable = VK_TRUE;
            state.srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
            state.dstColorBlendFactor = VK_BLEND_FACTOR_ONE;
            state.colorBlendOp = VK_BLEND_OP_ADD;
            state.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
            state.dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
            state.alphaBlendOp = VK_BLEND_OP_ADD;
            break;
        case BlendMode::Opaque:
        default:
            break;
    }
    return state;
}

// Scene pipeline blend: Additive is ONE + ONE; AlphaBlend keeps the destination alpha. Other modes match ToVkBlendState.
inline VkPipelineColorBlendAttachmentState ToVkSceneBlendState(BlendMode mode) noexcept
{
    VkPipelineColorBlendAttachmentState state = ToVkBlendState(mode);
    if (mode == BlendMode::Additive) {
        state.srcColorBlendFactor = VK_BLEND_FACTOR_ONE;
    } else if (mode == BlendMode::AlphaBlend) {
        state.srcAlphaBlendFactor = VK_BLEND_FACTOR_ZERO;
        state.dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
    }
    return state;
}

// Vulkan-side fixed-function state of a scene pipeline.
inline PipelineFixedState ToPipelineFixedState(const ScenePipelineState& state) noexcept
{
    return PipelineFixedState{ ToVkCullMode(state.cull), ToVkCompareOp(state.depthCompare), state.depthWrite,
        ToVkSceneBlendState(state.blend) };
}

} // namespace gte
