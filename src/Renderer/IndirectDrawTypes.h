#pragma once

// GPU-Driven Frustum Culling + Indirect Draw (render-pass-5), PHASE1 - see
// task_manager/render-pass-5/PHASE1_FOUNDATIONS_BOUNDS_INDIRECT_TYPES_AND_VOCABULARY.md.

#include <volk.h>

#include <cstdint>

namespace gte {

// Byte-for-byte mirror of VkDrawIndexedIndirectCommand - kept as this
// engine's OWN named type (rather than using the Vulkan struct directly
// everywhere) purely so a future reader/test never needs a live VkDevice
// just to talk about "one indirect draw command's shape" - mirrors
// GpuSkinningTypes.h's own "Vulkan-header-free where possible" discipline,
// EXCEPT this one file legitimately needs <volk.h> for the static_assert
// cross-check below (VkDrawIndexedIndirectCommand's own definition), same
// carve-out RenderGraphTypes.h already documents for itself.
struct IndirectDrawCommand {
    std::uint32_t indexCount = 0;
    std::uint32_t instanceCount = 0;
    std::uint32_t firstIndex = 0;
    std::int32_t vertexOffset = 0;
    std::uint32_t firstInstance = 0;
};
static_assert(sizeof(IndirectDrawCommand) == sizeof(VkDrawIndexedIndirectCommand),
    "IndirectDrawCommand must stay byte-for-byte identical to VkDrawIndexedIndirectCommand - "
    "this buffer is bound directly as the argument buffer to vkCmdDrawIndexedIndirect(Count)");
static_assert(sizeof(IndirectDrawCommand) == 20, "VkDrawIndexedIndirectCommand's own documented size");

} // namespace gte
