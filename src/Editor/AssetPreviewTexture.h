#pragma once

#include "../Core/UiTextureHandle.h"

#include <filesystem>
#include <memory>
#include <optional>
#include <string>

namespace gte {

class Renderer;
class Texture2D;

// Decodes an on-disk image asset (a plain image file, or a *.gta-wrapped KTX2 texture) and uploads it to a GPU
// texture for the Inspector's ImGui::Image() preview.
// Caches only the most recently resolved texture; it is re-decoded only when the path or the file's write time changes.
// Call Reset() before the UI backend shuts down (the destructor also calls it).
class AssetPreviewTexture {
public:
    AssetPreviewTexture() = default;
    ~AssetPreviewTexture();

    AssetPreviewTexture(const AssetPreviewTexture&) = delete;
    AssetPreviewTexture& operator=(const AssetPreviewTexture&) = delete;

    struct Preview {
        UiTextureHandle descriptor = UiTextureHandle::Invalid; // Ready to pass to ImGui::Image().
        int width = 0;
        int height = 0;
    };

    // Returns the decoded preview, or nullopt when `absolutePath` is not a decodable image or texture asset.
    // The caller then falls back to plain asset metadata.
    std::optional<Preview> Resolve(Renderer& renderer, const std::string& absolutePath);

    // Releases the cached texture and its UI handle after the GPU is idle. Safe to call repeatedly.
    void Reset();

private:
    Renderer* m_renderer = nullptr; // Non-owning; used to retire the cached texture.

    std::string m_cachedPath;
    std::filesystem::file_time_type m_cachedWriteTime{};
    bool m_cachedIsValid = false; // True if m_cachedPath decoded successfully last time.
    std::unique_ptr<Texture2D> m_texture;
    UiTextureHandle m_descriptor = UiTextureHandle::Invalid;
    int m_width = 0;
    int m_height = 0;
};

} // namespace gte
