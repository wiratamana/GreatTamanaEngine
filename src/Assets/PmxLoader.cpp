#include "PmxLoader.h"

#include <Saba/Model/MMD/PMXFile.h>

namespace gte {

PmxLoadResult LoadPmxModel(const std::string& filePath)
{
    PmxLoadResult result;

    saba::PMXFile pmxFile;
    if (!saba::ReadPMXFile(&pmxFile, filePath.c_str()))
    {
        result.success = false;
        result.message = "Failed to read PMX file: " + filePath;
        return result;
    }

    const std::size_t vertexCount = pmxFile.m_vertices.size();
    result.mesh.positions.reserve(vertexCount);
    result.mesh.normals.reserve(vertexCount);
    result.mesh.uvs.reserve(vertexCount);

    // Straight glm -> gte::Vec3/Vec2 field copies, nothing more - see this
    // function's own doc comment (PmxLoader.h) for why no axis/winding
    // remapping happens here.
    for (const auto& vertex : pmxFile.m_vertices)
    {
        result.mesh.positions.emplace_back(vertex.m_position.x, vertex.m_position.y, vertex.m_position.z);
        result.mesh.normals.emplace_back(vertex.m_normal.x, vertex.m_normal.y, vertex.m_normal.z);
        result.mesh.uvs.emplace_back(vertex.m_uv.x, vertex.m_uv.y);
    }

    result.mesh.indices.reserve(pmxFile.m_faces.size() * 3);
    for (const auto& face : pmxFile.m_faces)
    {
        result.mesh.indices.push_back(face.m_vertices[0]);
        result.mesh.indices.push_back(face.m_vertices[1]);
        result.mesh.indices.push_back(face.m_vertices[2]);
    }

    result.success = true;
    result.message = "Loaded PMX file: " + filePath
        + " (" + std::to_string(vertexCount) + " vertices, "
        + std::to_string(pmxFile.m_faces.size()) + " faces)";
    return result;
}

} // namespace gte
