#include "MeshAssetPartGrouping.h"

#include "../../Renderer/Mesh.h"

#include <algorithm>

namespace gte {

std::vector<MeshAssetPartGroup> GroupMeshAssetPartsBySharedVertexBuffer(
    RenderSystem& renderSystem, const std::vector<MeshAssetPart>& parts)
{
    std::vector<MeshAssetPartGroup> groups;
    groups.reserve(parts.size());

    for (std::size_t i = 0; i < parts.size(); ++i) {
        Mesh* gpuMesh = renderSystem.TryGetMesh(parts[i].mesh);
        if (gpuMesh == nullptr) {
            continue;
        }

        const void* identity = gpuMesh->VertexBufferIdentity();
        const auto found = std::find_if(groups.begin(), groups.end(),
            [identity](const MeshAssetPartGroup& group) { return group.vertexBufferIdentity == identity; });

        if (found == groups.end()) {
            MeshAssetPartGroup group;
            group.vertexBufferIdentity = identity;
            group.representativeMesh = gpuMesh;
            group.textured = parts[i].texture.IsValid();
            group.partIndices.push_back(i);
            groups.push_back(std::move(group));
        } else {
            found->partIndices.push_back(i);
        }
    }

    return groups;
}

} // namespace gte
