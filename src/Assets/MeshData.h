#pragma once

#include "../Math/Vec2.h"
#include "../Math/Vec3.h"

#include <cstdint>
#include <vector>

namespace gte {

// Plain, engine-native mesh geometry - positions/normals/UVs/triangle
// indices, index-aligned (positions[i]/normals[i]/uvs[i] together describe
// vertex i). This is the one shared shape every mesh IMPORTER (PmxLoader.h
// today; a future OBJ/glTF loader tomorrow) produces, and the one shape
// MeshFile.h's EncodeMeshDataToBytes()/DecodeMeshDataFromBytes() (de)
// serializes as a *.gta AssetType::Mesh payload (see AssetImporter.h) - the
// neutral home for this shape so neither of those needs to depend on the
// other, or on any one specific source format.
//
// Deliberately a plain data struct with no behavior of its own, same "ECS
// component"-style philosophy as the rest of this engine (see AGENTS.md,
// "Entity-Component-System").
struct MeshData {
    std::vector<Vec3> positions;
    std::vector<Vec3> normals;
    std::vector<Vec2> uvs;

    // Triangle list, 3 indices per face, indexing into positions/normals/uvs
    // above - directly usable as a GPU index buffer.
    std::vector<std::uint32_t> indices;
};

} // namespace gte
