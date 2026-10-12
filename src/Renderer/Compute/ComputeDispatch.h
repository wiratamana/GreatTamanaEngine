#pragma once

#include "../../Core/Extent.h"

#include <cassert>
#include <cstdint>

namespace gte {

// Calculates ceiling division: ceil(totalItems / localGroupSize)
constexpr std::uint32_t ComputeGroupCount(std::uint32_t totalItems, std::uint32_t localGroupSize) noexcept
{
    if (localGroupSize == 0) {
        assert(false && "ComputeGroupCount: localGroupSize must be > 0");
        return 0;
    }
    return (totalItems + localGroupSize - 1) / localGroupSize;
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

} // namespace gte
