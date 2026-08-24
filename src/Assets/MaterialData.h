#pragma once

#include "../Math/Vec3.h"
#include "../Math/Vec4.h"

#include <cstdint>
#include <string>
#include <vector>

namespace gte {

// How a material's "sphere map" texture (a PMX-specific environment-map
// trick - see saba::PMXSphereMode) is combined with the base diffuse
// texture. Data only - nothing in this engine's render path evaluates a
// sphere map yet (see Material::sphereTextureIndex below); kept so a future
// shader can without another PmxLoader.cpp/RigFile.cpp round of changes.
enum class SphereTextureMode : std::uint8_t {
    Disabled,
    Multiply,
    Add,
    SubTexture,
};

// Plain, engine-native material data - the "Materials / Textures" half of
// this engine's MMD import support (see SkeletonData.h's own file comment
// for the other four: bone weights/skinning, bones, morphs, physics).
// Produced by PmxLoader.h's LoadPmxModel() (from saba::PMXFile's
// m_materials/m_textures - see third_party/saba/src/Saba/Model/MMD/
// PMXFile.h), same "engine-native, saba/glm-free copy of the third-party
// format's data" shape as SkeletonData/MorphData/PhysicsData/MeshData.
//
// A PMX material always owns a CONTIGUOUS run of face-vertex indices (see
// Material::indexCount below) - materials are never interleaved with each
// other in the index list, so MeshData::indices can always be split into
// materials.size() contiguous slices with no separate per-triangle
// material-index array needed (unlike e.g. an OBJ/glTF importer, which
// would typically need one).
struct Material {
    std::string name;
    std::string englishName;

    Vec4 diffuse = Vec4(1.0f, 1.0f, 1.0f, 1.0f);
    Vec3 specular = Vec3::Zero();
    float specularPower = 0.0f;
    Vec3 ambient = Vec3::Zero();

    bool bothFacesVisible = false; // PMX "double-sided" draw-mode flag - no backface culling for this material.
    bool drawEdge = false;
    Vec4 edgeColor = Vec4(0.0f, 0.0f, 0.0f, 1.0f);
    float edgeSize = 1.0f;

    // Index into MaterialData::textures below, or -1 for "no diffuse
    // texture" (a flat-shaded material - PMX allows this).
    std::int32_t textureIndex = -1;

    // Index into MaterialData::textures below, or -1 for "no sphere map".
    // Data only for now - see SphereTextureMode above.
    std::int32_t sphereTextureIndex = -1;
    SphereTextureMode sphereMode = SphereTextureMode::Disabled;

    // Toon shading reference - EITHER a model-local texture (useSharedToon
    // == false, toonTextureIndex indexes MaterialData::textures) OR one of
    // MMD's 10 built-in shared "toon01.bmp".."toon10.bmp" textures
    // (useSharedToon == true, sharedToonIndex is 0-9) - matches
    // saba::PMXToonMode's own Separate/Common split. Data only; this
    // engine's render path does not evaluate toon shading yet.
    bool useSharedToon = false;
    std::int32_t toonTextureIndex = -1;
    std::uint8_t sharedToonIndex = 0;

    // How many CONSECUTIVE entries of MeshData::indices (starting right
    // after the previous material's own run, in MaterialData::materials
    // order, with the very first material starting at index 0) this
    // material draws - PMX's own PMXMaterial::m_numFaceVertices convention.
    // Summing every Material::indexCount in order always equals
    // MeshData::indices.size() for a well-formed PMX import.
    std::uint32_t indexCount = 0;
};

struct MaterialData {
    // Absolute filesystem paths (UTF-8), resolved ONCE at PMX-import time
    // against the source .pmx file's own directory (see PmxLoader.cpp) -
    // deliberately NOT the raw relative path a .pmx itself stores: a *.gta
    // this engine writes is meant to be loadable standalone, with no
    // knowledge of where its original source .pmx lived on disk, so the
    // resolution has to happen exactly once, at import time, while that
    // directory is still known. An entry may point at a file that no
    // longer exists (moved/deleted since import, or the .pmx referenced a
    // texture that was never actually present) - a consumer (e.g.
    // Game::EnsureMeshAsset()) must handle a failed decode of one of these
    // paths gracefully rather than assuming every entry is loadable.
    std::vector<std::string> textures;

    std::vector<Material> materials;
};

} // namespace gte
