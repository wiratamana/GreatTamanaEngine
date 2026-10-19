#pragma once

#include "MaterialData.h"
#include "MeshData.h"

#include <string>

namespace gte {

// Result of one LoadGltfModel() call; always fully populated.
struct GltfLoadResult {
    bool success = false;

    // Engine-space (left-handed) geometry with node transforms baked in.
    // Triangles are grouped per material, in `materials` order.
    MeshData mesh;

    // Materials + texture file references. Each texture's `sourcePath` is an absolute
    // UTF-8 path; its `guid` stays invalid until the importer runs.
    MaterialData materials;

    std::string message;
};

// Parses an external-file .gltf (not .glb) into engine-native data. Never throws.
// `filePath` is UTF-8.
GltfLoadResult LoadGltfModel(const std::string& filePath);

} // namespace gte
