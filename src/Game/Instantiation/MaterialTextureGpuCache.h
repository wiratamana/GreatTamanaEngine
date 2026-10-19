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

// Uploads each base-color image once per Guid and keeps one material per (image, MaterialParams).
class MaterialTextureGpuCache {
public:
    // Material for `textureGuid` with `params`, or kInvalidMaterialTextureHandle if unknown or undecodable.
    // Failures are not cached.
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
