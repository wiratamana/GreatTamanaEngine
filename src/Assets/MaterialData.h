#pragma once

#include "AssetTypes.h"
#include "../Math/Vec3.h"
#include "../Math/Vec4.h"

#include <cstdint>
#include <string>
#include <vector>

namespace gte {

// How a PMX sphere-map texture combines with the base texture. Data only; no shader evaluates it.
enum class SphereTextureMode : std::uint8_t {
    Disabled,
    Multiply,
    Add,
    SubTexture,
};

// How a material's alpha is used.
enum class AlphaMode : std::uint8_t {
    Opaque,
    Mask,
    Blend,
};

// One texture slot of a model. Loaders fill `sourcePath` only; the importer
// writes a Texture *.gta and stores its `guid`. Runtime code must resolve
// textures through `guid` and treat Guid::Invalid() as "no texture".
struct MaterialTextureRef {
    std::string sourcePath;
    Guid guid = Guid::Invalid();
};

// Engine-native material. Each material owns a CONTIGUOUS run of
// MeshData::indices (see indexCount), so no per-triangle material array is needed.
struct Material {
    std::string name;
    std::string englishName;

    Vec4 diffuse = Vec4(1.0f, 1.0f, 1.0f, 1.0f);
    Vec3 specular = Vec3::Zero();
    float specularPower = 0.0f;
    Vec3 ambient = Vec3::Zero();

    bool bothFacesVisible = false; // Double-sided: no backface culling.
    bool drawEdge = false;
    Vec4 edgeColor = Vec4(0.0f, 0.0f, 0.0f, 1.0f);
    float edgeSize = 1.0f;

    // Index into MaterialData::textures, or -1 for none. This is the base-color slot.
    std::int32_t textureIndex = -1;

    // Sphere map slot (-1 = none). Data only.
    std::int32_t sphereTextureIndex = -1;
    SphereTextureMode sphereMode = SphereTextureMode::Disabled;

    // Toon reference: a model texture (useSharedToon == false) or one of the 10 shared toon textures.
    bool useSharedToon = false;
    std::int32_t toonTextureIndex = -1;
    std::uint8_t sharedToonIndex = 0;

    // glTF slots: index into MaterialData::textures, or -1 for none. Imported, not yet sampled.
    std::int32_t normalTextureIndex = -1;
    std::int32_t metallicRoughnessTextureIndex = -1;
    std::int32_t occlusionTextureIndex = -1;
    std::int32_t emissiveTextureIndex = -1;
    AlphaMode alphaMode = AlphaMode::Opaque;
    float alphaCutoff = 0.5f;

    // True for glTF materials: `diffuse` is a base color factor, and a material without a texture draws as flat color.
    // False (PMX, STL): `diffuse` is not applied to rendering.
    bool useBaseColorFactor = false;

    // Number of consecutive MeshData::indices this material draws, in materials order starting at 0.
    std::uint32_t indexCount = 0;
};

struct MaterialData {
    // May hold entries with guid == Guid::Invalid() (source missing or undecodable).
    std::vector<MaterialTextureRef> textures;

    std::vector<Material> materials;
};

} // namespace gte
