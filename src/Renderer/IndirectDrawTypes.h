#pragma once

// Indirect draw command layout shared by culling and the indirect draw pass.

#include <volk.h>

#include <cstdint>

namespace gte {

// Engine-named mirror of VkDrawIndexedIndirectCommand (needs <volk.h> only for the static_assert below).
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
