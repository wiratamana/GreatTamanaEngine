#include "MeshNormalRecompute.h"

#include "../Math/Vec3.h"

#include <cstdint>
#include <cstddef>
#include <vector>

namespace gte {

void RecomputeMeshNormalsFromGeometry(MeshData& mesh)
{
    if (mesh.normals.size() != mesh.positions.size()) {
        mesh.normals.resize(mesh.positions.size());
    }

    std::vector<Vec3> accumulators(mesh.positions.size(), Vec3::Zero());

    const std::size_t triangleAlignedCount = (mesh.indices.size() / 3) * 3;
    for (std::size_t i = 0; i < triangleAlignedCount; i += 3) {
        const std::uint32_t i0 = mesh.indices[i];
        const std::uint32_t i1 = mesh.indices[i + 1];
        const std::uint32_t i2 = mesh.indices[i + 2];
        if (i0 >= mesh.positions.size() || i1 >= mesh.positions.size() || i2 >= mesh.positions.size()) {
            continue; // Defensive: never index out of bounds on a corrupt/mismatched index buffer.
        }

        const Vec3 faceVector = Cross(mesh.positions[i1] - mesh.positions[i0], mesh.positions[i2] - mesh.positions[i0]);
        accumulators[i0] += faceVector;
        accumulators[i1] += faceVector;
        accumulators[i2] += faceVector;
    }

    for (std::size_t v = 0; v < mesh.normals.size(); ++v) {
        mesh.normals[v] = Normalize(accumulators[v]);
    }
}

} // namespace gte
