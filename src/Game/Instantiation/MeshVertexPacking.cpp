#include "MeshVertexPacking.h"

#include <algorithm>

namespace gte {

void PackMeshVertexRange(std::uint32_t beginIndex, std::uint32_t endIndex, const std::vector<Vec3>& positions,
    const std::vector<Vec3>& normals, std::vector<MeshVertex>& out)
{
    const bool hasNormals = normals.size() == positions.size();

    const std::size_t clampedEnd = std::min(static_cast<std::size_t>(endIndex), positions.size());
    const std::size_t clampedOutEnd = std::min(clampedEnd, out.size());

    for (std::size_t i = beginIndex; i < clampedOutEnd; ++i) {
        const Vec3& p = positions[i];
        out[i].position[0] = p.x;
        out[i].position[1] = p.y;
        out[i].position[2] = p.z;

        const Vec3 n = hasNormals ? normals[i] : Vec3::Up();
        out[i].normal[0] = n.x;
        out[i].normal[1] = n.y;
        out[i].normal[2] = n.z;
    }
}

void PackMeshVertexUvRange(std::uint32_t beginIndex, std::uint32_t endIndex, const std::vector<Vec3>& positions,
    const std::vector<Vec3>& normals, const std::vector<Vec2>& uvs, std::vector<MeshVertexUv>& out)
{
    const bool hasNormals = normals.size() == positions.size();
    const bool hasUvs = uvs.size() == positions.size();

    const std::size_t clampedEnd = std::min(static_cast<std::size_t>(endIndex), positions.size());
    const std::size_t clampedOutEnd = std::min(clampedEnd, out.size());

    for (std::size_t i = beginIndex; i < clampedOutEnd; ++i) {
        const Vec3& p = positions[i];
        out[i].position[0] = p.x;
        out[i].position[1] = p.y;
        out[i].position[2] = p.z;

        const Vec3 n = hasNormals ? normals[i] : Vec3::Up();
        out[i].normal[0] = n.x;
        out[i].normal[1] = n.y;
        out[i].normal[2] = n.z;

        const Vec2 uv = hasUvs ? uvs[i] : Vec2::Zero();
        out[i].uv[0] = uv.x;
        out[i].uv[1] = uv.y;
    }
}

std::vector<MeshVertex> PackMeshVertices(const std::vector<Vec3>& positions, const std::vector<Vec3>& normals)
{
    std::vector<MeshVertex> vertices(positions.size());
    PackMeshVertexRange(0, static_cast<std::uint32_t>(positions.size()), positions, normals, vertices);
    return vertices;
}

std::vector<MeshVertexUv> PackMeshVertexUvs(
    const std::vector<Vec3>& positions, const std::vector<Vec3>& normals, const std::vector<Vec2>& uvs)
{
    std::vector<MeshVertexUv> vertices(positions.size());
    PackMeshVertexUvRange(0, static_cast<std::uint32_t>(positions.size()), positions, normals, uvs, vertices);
    return vertices;
}

} // namespace gte
