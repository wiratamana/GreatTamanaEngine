#include "MaterialTextureGpuCache.h"

#include "../../Assets/AssetDatabase.h"
#include "../../Assets/GtaFile.h"
#include "../../Assets/Ktx2Decoder.h"
#include "../../Core/Logging.h"
#include "../../Encoding/Utf8Path.h"
#include "../../Renderer/Renderer.h"
#include "../RenderSystem.h"

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <exception>
#include <optional>
#include <string>

namespace gte {

namespace {

// Raw pixel bytes decoded before a flush; bounds the CPU memory while the factory still batches uploads.
constexpr std::uint64_t kDecodeGroupBytes = 48ull * 1024 * 1024;

constexpr const char* kLogCategory = "MaterialTextureGpuCache";

MaterialImageKey KeyOf(const Guid& guid, MaterialImageRole role) noexcept
{
    return MaterialImageKey{ guid, role == MaterialImageRole::BaseColor };
}

// Distinct per image so the Frame Debugger and the memory panel list each one under its own name.
std::string DebugNameOf(const Guid& guid, MaterialImageRole role)
{
    const char* prefix = "MaterialTexture ";
    switch (role) {
    case MaterialImageRole::BaseColor: break;
    case MaterialImageRole::Normal: prefix = "MaterialNormal "; break;
    case MaterialImageRole::MetallicRoughness: prefix = "MaterialMetallicRoughness "; break;
    case MaterialImageRole::Occlusion: prefix = "MaterialOcclusion "; break;
    }
    return prefix + guid.ToString();
}

// An image decoded and waiting for its upload.
struct PendingImage {
    MaterialImageKey key;
    MaterialImageRole role = MaterialImageRole::BaseColor;
    std::vector<std::uint8_t> pixels;
    int width = 0;
    int height = 0;
    std::string name;
};

} // namespace

void AppendImageRequests(const MaterialTextureSet& set, std::vector<ImageRequest>& out)
{
    const auto add = [&](const Guid& guid, MaterialImageRole role) {
        if (guid.IsValid()) {
            out.push_back({ guid, role });
        }
    };
    add(set.baseColor, MaterialImageRole::BaseColor);
    add(set.normal, MaterialImageRole::Normal);
    add(set.metallicRoughness, MaterialImageRole::MetallicRoughness);
    add(set.occlusion, MaterialImageRole::Occlusion);
}

void MaterialTextureGpuCache::MarkFailed(const MaterialImageKey& key, MaterialImageRole role, const std::string& reason)
{
    if (m_failedImages.insert(key).second) {
        GTE_LOG_WARNING(kLogCategory,
            std::string("Material image ") + key.guid.ToString() + " (" + MaterialImageRoleLabel(role)
                + ") is not available: " + reason + ". The slot uses its default image.");
    }
}

std::shared_ptr<Texture2D> MaterialTextureGpuCache::FindImage(const Guid& guid, MaterialImageRole role) const
{
    const auto found = m_images.find(KeyOf(guid, role));
    return found != m_images.end() ? found->second : nullptr;
}

void MaterialTextureGpuCache::PrepareImages(
    Renderer& renderer, const AssetDatabase& database, std::span<const ImageRequest> requests)
{
    const GpuUploadStats statsBefore = renderer.GetUploadStats();
    const auto timeBefore = std::chrono::steady_clock::now();

    std::vector<PendingImage> pending;
    std::unordered_set<MaterialImageKey, MaterialImageKeyHash> pendingKeys;
    std::uint64_t pendingBytes = 0;

    const auto flush = [&] {
        if (pending.empty()) {
            return;
        }
        std::vector<MaterialImageUpload> uploads;
        uploads.reserve(pending.size());
        for (const PendingImage& image : pending) {
            uploads.push_back({ image.pixels.data(), image.width, image.height, image.role, image.name });
        }
        try {
            const std::vector<std::shared_ptr<Texture2D>> created = renderer.CreateMaterialImages(uploads);
            for (std::size_t i = 0; i < pending.size(); ++i) {
                if (i < created.size() && created[i]) {
                    m_images.emplace(pending[i].key, created[i]);
                } else {
                    MarkFailed(pending[i].key, pending[i].role, "upload rejected the image");
                }
            }
        } catch (const std::exception& error) {
            for (const PendingImage& image : pending) {
                MarkFailed(image.key, image.role, error.what());
            }
        }
        pending.clear();
        pendingKeys.clear();
        pendingBytes = 0;
    };

    for (const ImageRequest& request : requests) {
        if (!request.guid.IsValid()) {
            continue;
        }
        const MaterialImageKey key = KeyOf(request.guid, request.role);
        if (m_images.contains(key) || m_failedImages.contains(key) || pendingKeys.contains(key)) {
            continue;
        }

        try {
            const AssetRecord* record = database.FindByGuid(request.guid);
            if (record == nullptr || record->type != AssetType::Texture) {
                MarkFailed(key, request.role, "unknown texture asset");
                continue;
            }
            const std::optional<GtaFileData> gta = ReadGtaFile(PathFromUtf8(record->gtaPath));
            if (!gta.has_value()) {
                MarkFailed(key, request.role, "asset file cannot be read");
                continue;
            }
            std::optional<Ktx2DecodeResult> decoded = DecodeKtx2ToRgba8(gta->payload);
            if (!decoded.has_value() || decoded->width == 0 || decoded->height == 0) {
                MarkFailed(key, request.role, "image cannot be decoded");
                continue;
            }

            PendingImage image;
            image.key = key;
            image.role = request.role;
            image.width = static_cast<int>(decoded->width);
            image.height = static_cast<int>(decoded->height);
            image.pixels = std::move(decoded->rgba8Pixels);
            image.name = DebugNameOf(request.guid, request.role);
            pendingBytes += image.pixels.size();
            pendingKeys.insert(key);
            pending.push_back(std::move(image));
        } catch (const std::exception& error) {
            MarkFailed(key, request.role, error.what());
            continue;
        }

        if (pendingBytes >= kDecodeGroupBytes) {
            flush();
        }
    }
    flush();

    const GpuUploadStats statsAfter = renderer.GetUploadStats();
    const std::uint64_t uploaded = statsAfter.imageCount - statsBefore.imageCount;
    if (uploaded > 0) {
        const auto elapsed = std::chrono::steady_clock::now() - timeBefore;
        const long long milliseconds = std::chrono::duration_cast<std::chrono::milliseconds>(elapsed).count();
        const std::uint64_t stagingMiB = (statsAfter.stagingBytes - statsBefore.stagingBytes) / (1024 * 1024);
        GTE_LOG_INFO(kLogCategory,
            std::to_string(uploaded) + " images uploaded in "
                + std::to_string(statsAfter.submitCount - statsBefore.submitCount) + " submits ("
                + std::to_string(stagingMiB) + " MiB), " + std::to_string(milliseconds) + " ms");
    }
}

MaterialTextureHandle MaterialTextureGpuCache::Resolve(RenderSystem& renderSystem, Renderer& renderer,
    const AssetDatabase& database, const MaterialTextureSet& set, const MaterialParams& params)
{
    const MaterialKey key{ { set.baseColor, set.normal, set.metallicRoughness, set.occlusion }, params };
    if (const auto found = m_materials.find(key); found != m_materials.end()) {
        return found->second;
    }

    try {
        std::vector<ImageRequest> requests;
        AppendImageRequests(set, requests);
        PrepareImages(renderer, database, requests);

        MaterialImages images;
        for (const ImageRequest& request : requests) {
            images[static_cast<std::size_t>(request.role)] = FindImage(request.guid, request.role);
        }
        if (set.baseColor.IsValid() && !images[static_cast<std::size_t>(MaterialImageRole::BaseColor)]) {
            return kInvalidMaterialTextureHandle;
        }

        const MaterialTextureHandle handle = renderSystem.RegisterTexture(renderer.CreateMaterialVariant(images, params));
        m_materials.emplace(key, handle);
        return handle;
    } catch (const std::exception& error) {
        GTE_LOG_WARNING(kLogCategory, std::string("Material creation failed: ") + error.what());
        return kInvalidMaterialTextureHandle;
    }
}

MaterialReleaseResult MaterialTextureGpuCache::ReleaseUnused(
    RenderSystem& renderSystem, Renderer& renderer, const std::unordered_set<MaterialTextureHandle>& inUse)
{
    MaterialReleaseResult result;

    std::vector<MaterialKey> unusedMaterials;
    for (const auto& [key, handle] : m_materials) {
        if (!inUse.contains(handle)) {
            unusedMaterials.push_back(key);
        }
    }
    const bool anyUnusedImage = std::any_of(m_images.begin(), m_images.end(),
        [](const auto& entry) { return entry.second.use_count() == 1; });
    if (unusedMaterials.empty() && !anyUnusedImage) {
        return result;
    }

    // Nothing may be destroyed while a submitted command buffer can still read it.
    if (renderer.DrainGpuForResourceRelease("material release sweep") != BlockOutcome::Completed) {
        GTE_LOG_ERROR(kLogCategory, "Release skipped, the GPU did not go idle.");
        return result;
    }

    for (const MaterialKey& key : unusedMaterials) {
        const auto found = m_materials.find(key);
        if (found == m_materials.end()) {
            continue;
        }
        renderSystem.UnregisterTexture(found->second);
        m_materials.erase(found);
        ++result.materialsFreed;
    }

    for (auto it = m_images.begin(); it != m_images.end();) {
        if (it->second.use_count() == 1) {
            it = m_images.erase(it);
            ++result.imagesFreed;
        } else {
            ++it;
        }
    }
    return result;
}

std::vector<MaterialTextureHandle> MaterialTextureGpuCache::Materials() const
{
    std::vector<MaterialTextureHandle> handles;
    handles.reserve(m_materials.size());
    for (const auto& entry : m_materials) {
        handles.push_back(entry.second);
    }
    return handles;
}

MaterialCacheStats MaterialTextureGpuCache::GetStats() const noexcept
{
    return MaterialCacheStats{ m_images.size(), m_materials.size(), m_failedImages.size() };
}

void MaterialTextureGpuCache::ForgetFailedImages() noexcept
{
    m_failedImages.clear();
}

} // namespace gte
