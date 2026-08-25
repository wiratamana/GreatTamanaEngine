#pragma once

#include "../../Assets/AssetTypes.h"
#include "../../Renderer/TextureHandle.h"

#include <unordered_map>

namespace gte {

class Renderer;
class RenderSystem;
class AssetDatabase;

// Replaces Game::EnsureMaterialTexture()/m_materialTextureCache - lazily
// decodes/uploads (once per distinct Guid, then cached) a PMX material's
// diffuse texture into a MaterialTexture (Renderer/MaterialTexture.h),
// shared across every submesh/model that happens to reference the exact
// same texture asset. Owned internally by MeshAssetGpuCatalog (see
// MeshAssetGpuCatalog.h) - nothing outside mesh loading needs to resolve a
// material texture, so this doesn't need to be shared any further out.
//
// Genuinely needs a live Renderer to do its real job - "Tier 2, no
// automated coverage yet" (see AGENTS.md), same as PrimitiveGpuCatalog.
class MaterialTextureGpuCache {
public:
    // Resolves `textureGuid` through `database` (an AssetDatabase freshly
    // scanned over the mesh's own directory - see MeshAssetGpuCatalog) into
    // that Texture *.gta's absolute path, reads it via ReadGtaFile(), and
    // decodes its KTX2 payload via Ktx2Decoder.h's DecodeKtx2ToRgba8() - the
    // exact same asset-by-Guid resolution path the Editor's own Inspector
    // preview (AssetPreviewTexture) uses for a *.gta Texture asset. Returns
    // kInvalidTextureHandle (never throws) if `textureGuid` is
    // Guid::Invalid(), isn't currently tracked by `database`, or fails to
    // decode (missing/corrupt/moved *.gta) - deliberately NOT cached as a
    // failure, so a texture that shows up later (or is fixed) can succeed
    // on a subsequent spawn without restarting the process.
    TextureHandle Resolve(RenderSystem& renderSystem, Renderer& renderer, const AssetDatabase& database, const Guid& textureGuid);

private:
    std::unordered_map<Guid, TextureHandle> m_cache;
};

} // namespace gte
