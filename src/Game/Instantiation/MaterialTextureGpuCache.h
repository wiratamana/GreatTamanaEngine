#pragma once

#include "../../Assets/AssetTypes.h"
#include "../../Renderer/MaterialParams.h"
#include "../../Renderer/MaterialTextureHandle.h"

#include <memory>
#include <unordered_map>
#include <utility>
#include <vector>

namespace gte {

class AssetDatabase;
class Renderer;
class RenderSystem;
class Texture2D;

// Lazily uploads base-color images and creates materials over them. An image is uploaded once per
// texture Guid; each distinct MaterialParams over it gets its own material (params block + descriptor set)
// that shares the image. Owned by MeshAssetGpuCatalog. Needs a live Renderer, so it has no automated test.
class MaterialTextureGpuCache {
public:
    // Material for the texture `textureGuid` with `params`. The texture *.gta is found through `database`
    // and decoded from KTX2. Returns kInvalidMaterialTextureHandle (never throws for a bad asset) when the
    // Guid is invalid, unknown, or undecodable; failures are not cached, so a fixed asset works on the next call.
    MaterialTextureHandle Resolve(RenderSystem& renderSystem, Renderer& renderer, const AssetDatabase& database,
        const Guid& textureGuid, const MaterialParams& params);

    // Material over a 1x1 white image: a flat color given by `params` for materials without a texture.
    MaterialTextureHandle ResolveSolidColor(RenderSystem& renderSystem, Renderer& renderer, const MaterialParams& params);

private:
    struct Image {
        std::shared_ptr<Texture2D> texture;
        std::vector<std::pair<MaterialParams, MaterialTextureHandle>> materials;
    };

    // Material for `image` with `params`; creates it on first use. `image.texture` must be set.
    static MaterialTextureHandle GetMaterial(RenderSystem& renderSystem, Renderer& renderer, Image& image,
        const MaterialParams& params);

    std::unordered_map<Guid, Image> m_images;
    Image m_whiteImage;
};

} // namespace gte
