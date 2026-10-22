#pragma once

#include <cstdint>

namespace gte {

// Indirect draw capabilities as plain data (no Vulkan types).
struct IndirectDrawCapsView {
    bool drawIndirectCount = false;
    bool multiDrawIndirect = false;
    bool drawIndirectFirstInstance = false;
    std::uint32_t maxDrawIndirectCount = 0;
};

// The cluster path needs all three features and a non-zero draw count limit.
constexpr bool ClusterPathSupported(const IndirectDrawCapsView& caps) noexcept
{
    return caps.drawIndirectCount && caps.multiDrawIndirect && caps.drawIndirectFirstInstance
        && caps.maxDrawIndirectCount > 0;
}

// The instanced batch path writes firstInstance per command and uses a counted multi draw.
constexpr bool OldBatchPathSupported(const IndirectDrawCapsView& caps) noexcept
{
    return caps.drawIndirectCount && caps.multiDrawIndirect && caps.drawIndirectFirstInstance;
}

} // namespace gte
