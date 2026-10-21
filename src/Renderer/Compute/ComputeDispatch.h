#pragma once

#include "../../Core/Extent.h"

#include <cassert>
#include <cstdint>

namespace gte {

// Vulkan guarantees at least this many work groups per dispatch axis.
inline constexpr std::uint32_t kGuaranteedMaxGroupCountPerAxis = 65535;

// Calculates ceiling division: ceil(totalItems / localGroupSize). Cannot overflow.
constexpr std::uint32_t ComputeGroupCount(std::uint32_t totalItems, std::uint32_t localGroupSize) noexcept
{
    if (localGroupSize == 0) {
        assert(false && "ComputeGroupCount: localGroupSize must be > 0");
        return 0;
    }
    return totalItems / localGroupSize + (totalItems % localGroupSize != 0 ? 1u : 0u);
}

// 3D version: calculates group count for width, height, and depth
constexpr Extent3D ComputeGroupCount3D(const Extent3D& totalItems, const Extent3D& localGroupSize) noexcept
{
    Extent3D result;
    result.width = ComputeGroupCount(totalItems.width, localGroupSize.width);
    result.height = ComputeGroupCount(totalItems.height, localGroupSize.height);
    result.depth = ComputeGroupCount(totalItems.depth, localGroupSize.depth);
    return result;
}

// True when any axis needs more groups than the guaranteed per-axis limit.
constexpr bool ExceedsGuaranteedGroupCount(const Extent3D& groups) noexcept
{
    return groups.width > kGuaranteedMaxGroupCountPerAxis || groups.height > kGuaranteedMaxGroupCountPerAxis
        || groups.depth > kGuaranteedMaxGroupCountPerAxis;
}

} // namespace gte
