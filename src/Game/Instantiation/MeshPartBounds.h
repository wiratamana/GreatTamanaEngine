#pragma once

#include "../../Math/Vec3.h"
#include "../../Renderer/Culling/CullingTypes.h"

#include <cstdint>
#include <optional>
#include <vector>

namespace gte {

// Tight local-space AABB of the vertices `indices` references (out-of-range indices are skipped).
inline AABB ComputeSubsetLocalAABB(const std::vector<Vec3>& positions, const std::vector<std::uint32_t>& indices)
{
    std::vector<Vec3> referenced;
    referenced.reserve(indices.size());
    for (const std::uint32_t index : indices) {
        if (index < positions.size()) {
            referenced.push_back(positions[index]);
        }
    }
    return ComputeLocalAABB(referenced);
}

// Local bounds of one mesh part. Skinned parts get none: their vertices change at runtime.
inline std::optional<AABB> PartLocalBounds(
    bool skinned, const std::vector<Vec3>& positions, const std::vector<std::uint32_t>& indices)
{
    if (skinned) {
        return std::nullopt;
    }
    return ComputeSubsetLocalAABB(positions, indices);
}

} // namespace gte
