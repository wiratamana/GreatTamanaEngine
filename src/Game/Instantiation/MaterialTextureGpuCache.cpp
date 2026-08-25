#include "MaterialTextureGpuCache.h"

#include "../../Assets/AssetDatabase.h"
#include "../../Assets/GtaFile.h"
#include "../../Assets/Ktx2Decoder.h"
#include "../../Renderer/Renderer.h"
#include "../RenderSystem.h"

#include <filesystem>
#include <optional>

namespace gte {

namespace {

// Same std::u8string round-trip Game.cpp's own Utf8PathFromGamePath() uses -
// see that function's doc comment for the full "why".
std::filesystem::path Utf8PathFromGamePath(const std::string& utf8)
{
    return std::filesystem::path(std::u8string(reinterpret_cast<const char8_t*>(utf8.data()), utf8.size()));
}

} // namespace

TextureHandle MaterialTextureGpuCache::Resolve(
    RenderSystem& renderSystem, Renderer& renderer, const AssetDatabase& database, const Guid& textureGuid)
{
    if (!textureGuid.IsValid()) {
        return kInvalidTextureHandle;
    }

    if (const auto found = m_cache.find(textureGuid); found != m_cache.end()) {
        return found->second;
    }

    const AssetRecord* record = database.FindByGuid(textureGuid);
    if (record == nullptr || record->type != AssetType::Texture) {
        return kInvalidTextureHandle;
    }

    const std::optional<GtaFileData> gta = ReadGtaFile(Utf8PathFromGamePath(record->gtaPath));
    if (!gta.has_value()) {
        return kInvalidTextureHandle;
    }

    const std::optional<Ktx2DecodeResult> decoded = DecodeKtx2ToRgba8(gta->payload);
    if (!decoded.has_value()) {
        return kInvalidTextureHandle;
    }

    const TextureHandle handle = renderSystem.RegisterTexture(renderer.CreateMaterialTexture2D(
        decoded->rgba8Pixels.data(), static_cast<int>(decoded->width), static_cast<int>(decoded->height),
        "MaterialTexture"));
    m_cache.emplace(textureGuid, handle);
    return handle;
}

} // namespace gte
