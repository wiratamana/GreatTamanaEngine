#include "MaterialTextureGpuCache.h"

#include "../../Assets/AssetDatabase.h"
#include "../../Assets/GtaFile.h"
#include "../../Assets/Ktx2Decoder.h"
#include "../../Encoding/Utf8Path.h"
#include "../../Renderer/Renderer.h"
#include "../RenderSystem.h"

#include <algorithm>
#include <filesystem>
#include <optional>
#include <string>

namespace gte {

MaterialTextureHandle MaterialTextureGpuCache::GetMaterial(
    RenderSystem& renderSystem, Renderer& renderer, Image& image, const MaterialParams& params)
{
    const auto found = std::find_if(image.materials.begin(), image.materials.end(),
        [&](const auto& entry) { return entry.first == params; });
    if (found != image.materials.end()) {
        return found->second;
    }
    const MaterialTextureHandle handle
        = renderSystem.RegisterTexture(renderer.CreateMaterialVariant(image.texture, params));
    image.materials.emplace_back(params, handle);
    return handle;
}

MaterialTextureHandle MaterialTextureGpuCache::Resolve(RenderSystem& renderSystem, Renderer& renderer,
    const AssetDatabase& database, const Guid& textureGuid, const MaterialParams& params)
{
    if (!textureGuid.IsValid()) {
        return kInvalidMaterialTextureHandle;
    }

    if (const auto found = m_images.find(textureGuid); found != m_images.end()) {
        return GetMaterial(renderSystem, renderer, found->second, params);
    }

    const AssetRecord* record = database.FindByGuid(textureGuid);
    if (record == nullptr || record->type != AssetType::Texture) {
        return kInvalidMaterialTextureHandle;
    }

    const std::optional<GtaFileData> gta = ReadGtaFile(PathFromUtf8(record->gtaPath));
    if (!gta.has_value()) {
        return kInvalidMaterialTextureHandle;
    }

    const std::optional<Ktx2DecodeResult> decoded = DecodeKtx2ToRgba8(gta->payload);
    if (!decoded.has_value()) {
        return kInvalidMaterialTextureHandle;
    }

    // Distinct per texture so the Frame Debugger lists each bound texture under its own name.
    const std::string debugName = "MaterialTexture " + textureGuid.ToString();
    MaterialTexture material = renderer.CreateMaterialTexture2D(decoded->rgba8Pixels.data(),
        static_cast<int>(decoded->width), static_cast<int>(decoded->height), params, debugName.c_str());

    Image& image = m_images[textureGuid];
    image.texture = material.texture;
    const MaterialTextureHandle handle = renderSystem.RegisterTexture(std::move(material));
    image.materials.emplace_back(params, handle);
    return handle;
}

MaterialTextureHandle MaterialTextureGpuCache::ResolveSolidColor(
    RenderSystem& renderSystem, Renderer& renderer, const MaterialParams& params)
{
    if (!m_whiteImage.texture) {
        const std::uint8_t whitePixel[4] = { 255, 255, 255, 255 };
        MaterialTexture material = renderer.CreateMaterialTexture2D(whitePixel, 1, 1, params, "MaterialSolidColor");
        m_whiteImage.texture = material.texture;
        const MaterialTextureHandle handle = renderSystem.RegisterTexture(std::move(material));
        m_whiteImage.materials.emplace_back(params, handle);
        return handle;
    }
    return GetMaterial(renderSystem, renderer, m_whiteImage, params);
}

} // namespace gte
