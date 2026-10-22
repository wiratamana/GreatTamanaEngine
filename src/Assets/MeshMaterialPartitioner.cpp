#include "MeshMaterialPartitioner.h"

namespace gte {

std::vector<MeshMaterialSlice> PartitionMeshMaterials(std::size_t totalIndexCount, const std::vector<Material>& materials)
{
    std::vector<MeshMaterialSlice> slices;

    std::size_t cursor = 0;
    for (std::size_t materialIndex = 0; materialIndex < materials.size(); ++materialIndex) {
        const Material& material = materials[materialIndex];

        std::size_t start = cursor;
        std::size_t count = material.indexCount;
        if (start > totalIndexCount) {
            start = totalIndexCount;
            count = 0;
        } else if (start + count > totalIndexCount) {
            // Clamp a run that overshoots the index buffer.
            count = totalIndexCount - start;
        }
        cursor = start + count;

        if (count > 0) {
            slices.push_back(MeshMaterialSlice{ start, count, static_cast<std::int32_t>(materialIndex), material.name });
        }
    }

    if (cursor < totalIndexCount) {
        slices.push_back(MeshMaterialSlice{ cursor, totalIndexCount - cursor, -1, std::string() });
    }

    return slices;
}

} // namespace gte
