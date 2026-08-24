#pragma once

#include "AssetTypes.h"
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

// One texture slot referenced by a PMX material (see MaterialData::textures
// below) - carries BOTH the raw, resolved-at-import-time source file path
// AND (once actually imported) the stable Guid of the *.gta
// AssetType::Texture asset that source file was turned into.
//
// PmxLoader.h's LoadPmxModel() only ever fills `sourcePath` (see
// PmxLoader.cpp's ResolveTexturePath()) - it has no AssetDatabase dependency
// and never writes any *.gta file itself, so `guid` always starts out
// Guid::Invalid() there. AssetImporter.cpp's mesh-import branch is what
// actually decodes/re-encodes `sourcePath` as its own standalone *.gta
// Texture asset (the exact same KTX2 pipeline a plain dropped PNG/JPEG
// already goes through - see Ktx2Encoder.h) and fills in `guid` with the
// result - this is what makes the imported model's own *.gta a
// self-contained, Project-relative reference instead of ever pointing back
// out at the original source .pmx's own folder on disk (which may not even
// exist on whatever machine later loads the model).
//
// A consumer resolving a material's texture at runtime (see
// Game::EnsureMeshAsset(), src/Game/Game.cpp) must go through `guid` and an
// AssetDatabase lookup ONLY - never `sourcePath` directly - and must treat
// Guid::Invalid() (the source image was missing/undecodable at import time,
// see sourcePath's own doc comment) exactly like "no texture", the same
// degrade-gracefully convention every other best-effort asset lookup in
// this engine already follows. `sourcePath` itself is kept around purely as
// import-time diagnostic/debug information (e.g. a future Inspector detail
// view) - it is NEVER read by anything at runtime.
struct MaterialTextureRef {
    std::string sourcePath;
    Guid guid = Guid::Invalid();
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
    // One entry per PMX texture record - see MaterialTextureRef's own doc
    // comment above for the sourcePath/guid split. An entry may have
    // guid == Guid::Invalid() (never imported - the source file was
    // missing/undecodable, or this MaterialData predates texture-import
    // support) - a consumer must handle that gracefully rather than
    // assuming every entry resolves to a real texture asset.
    std::vector<MaterialTextureRef> textures;

    std::vector<Material> materials;
};

} // namespace gte
