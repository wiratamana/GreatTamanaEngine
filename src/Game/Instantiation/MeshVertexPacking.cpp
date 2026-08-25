#include "MeshVertexPacking.h"

namespace gte {

std::vector<MeshVertex> PackMeshVertices(const std::vector<Vec3>& positions, const std::vector<Vec3>& normals)
{
    const bool hasNormals = normals.size() == positions.size();

    std::vector<MeshVertex> vertices(positions.size());
    for (std::size_t i = 0; i < positions.size(); ++i) {
        const Vec3& p = positions[i];
        vertices[i].position[0] = p.x;
        vertices[i].position[1] = p.y;
        vertices[i].position[2] = p.z;

        const Vec3 n = hasNormals ? normals[i] : Vec3::Up();
        vertices[i].normal[0] = n.x;
        vertices[i].normal[1] = n.y;
        vertices[i].normal[2] = n.z;
    }
    return vertices;
}

std::vector<MeshVertexUv> PackMeshVertexUvs(
    const std::vector<Vec3>& positions, const std::vector<Vec3>& normals, const std::vector<Vec2>& uvs)
{
    const bool hasNormals = normals.size() == positions.size();
    const bool hasUvs = uvs.size() == positions.size();

    std::vector<MeshVertexUv> vertices(positions.size());
    for (std::size_t i = 0; i < positions.size(); ++i) {
        const Vec3& p = positions[i];
        vertices[i].position[0] = p.x;
        vertices[i].position[1] = p.y;
        vertices[i].position[2] = p.z;

        const Vec3 n = hasNormals ? normals[i] : Vec3::Up();
        vertices[i].normal[0] = n.x;
        vertices[i].normal[1] = n.y;
        vertices[i].normal[2] = n.z;

        const Vec2 uv = hasUvs ? uvs[i] : Vec2::Zero();
        vertices[i].uv[0] = uv.x;
        vertices[i].uv[1] = uv.y;
    }
    return vertices;
}

} // namespace gte
