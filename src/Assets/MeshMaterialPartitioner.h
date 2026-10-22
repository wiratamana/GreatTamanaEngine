#pragma once

#include "MaterialData.h"

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace gte {

// One contiguous run of a mesh's index buffer, owned by a single material.
struct MeshMaterialSlice {
    std::size_t start = 0;
    std::size_t count = 0;

    // Index into the `materials` vector, or -1 for the trailing slice no material claims.
    std::int32_t materialIndex = -1;

    // Copy of Material::name; empty for the trailing slice.
    std::string name;
};

// Splits [0, totalIndexCount) into contiguous per-material slices in `materials` order.
// Over-claiming runs are clamped; the unclaimed tail becomes one slice with materialIndex -1.
// Zero-length slices are never returned. Pure index math, never fails.
std::vector<MeshMaterialSlice> PartitionMeshMaterials(std::size_t totalIndexCount, const std::vector<Material>& materials);

} // namespace gte
