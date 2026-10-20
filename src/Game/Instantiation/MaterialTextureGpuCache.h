#pragma once

#include "MaterialCacheKeys.h"

#include "../../Assets/AssetTypes.h"
#include "../../Renderer/MaterialImageRole.h"
#include "../../Renderer/MaterialParams.h"
#include "../../Renderer/MaterialTextureHandle.h"

#include <cstddef>
#include <memory>
#include <span>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace gte {

class AssetDatabase;
class Renderer;
class RenderSystem;
class Texture2D;

// Guids of the images one material uses. Guid::Invalid() = the default image of that slot.
struct MaterialTextureSet {
    Guid baseColor = Guid::Invalid();
    Guid normal = Guid::Invalid();
    Guid metallicRoughness = Guid::Invalid();
    Guid occlusion = Guid::Invalid();
};

// One image the caller will need soon.
struct ImageRequest {
    Guid guid;
    MaterialImageRole role = MaterialImageRole::BaseColor;
};

// Appends the valid Guids of `set` as requests (one per slot).
void AppendImageRequests(const MaterialTextureSet& set, std::vector<ImageRequest>& out);

struct MaterialReleaseResult {
    std::size_t materialsFreed = 0;
    std::size_t imagesFreed = 0;
};

struct MaterialCacheStats {
    std::size_t images = 0;
    std::size_t materials = 0;
    std::size_t failedImages = 0;
};

// Uploads each image once per (Guid, color encoding) and keeps one material per (images, MaterialParams).
// Main thread only.
class MaterialTextureGpuCache {
public:
    // Loads every requested image that is not loaded and not known to fail: decode, then upload in chunks.
    // Never throws. A failure marks the image failed and logs one warning with the Guid and the role.
    void PrepareImages(Renderer& renderer, const AssetDatabase& database, std::span<const ImageRequest> requests);

    // Material for `set` with `params`. Never throws.
    // Invalid when set.baseColor is valid but cannot be loaded, or when the GPU objects cannot be created.
    // Any other slot that cannot be loaded uses its default image.
    MaterialTextureHandle Resolve(RenderSystem& renderSystem, Renderer& renderer, const AssetDatabase& database,
        const MaterialTextureSet& set, const MaterialParams& params);

    // Frees every material whose handle is not in `inUse`, then every cached image only the cache still holds.
    // Waits once for the GPU when it frees anything. Never throws.
    MaterialReleaseResult ReleaseUnused(RenderSystem& renderSystem, Renderer& renderer,
        const std::unordered_set<MaterialTextureHandle>& inUse);

    // Handles of every cached material.
    std::vector<MaterialTextureHandle> Materials() const;

    MaterialCacheStats GetStats() const noexcept;

    // Forgets which images failed to load, so the next load tries them again.
    void ForgetFailedImages() noexcept;

private:
    std::shared_ptr<Texture2D> FindImage(const Guid& guid, MaterialImageRole role) const;
    void MarkFailed(const MaterialImageKey& key, MaterialImageRole role, const std::string& reason);

    std::unordered_map<MaterialImageKey, std::shared_ptr<Texture2D>, MaterialImageKeyHash> m_images;
    std::unordered_set<MaterialImageKey, MaterialImageKeyHash> m_failedImages; // Warned once, not retried.
    std::unordered_map<MaterialKey, MaterialTextureHandle, MaterialKeyHash> m_materials;
};

} // namespace gte
