#pragma once

#include "../Math/Vec3.h"
#include "../Core/UiTextureHandle.h"

#include <cstdint>
#include <filesystem>
#include <memory>
#include <optional>
#include <string>

namespace gte {

class MeshPreviewRenderer;
class Renderer;

// Spinning 3D preview of a mesh asset (*.gta) for the Inspector, shown via ImGui::Image(). The GPU mesh is cached
// until the path or write time changes. Call Reset() before the UI backend shuts down (the destructor also does).
class AssetPreviewMesh {
public:
    AssetPreviewMesh();
    ~AssetPreviewMesh();

    AssetPreviewMesh(const AssetPreviewMesh&) = delete;
    AssetPreviewMesh& operator=(const AssetPreviewMesh&) = delete;

    struct Preview {
        UiTextureHandle descriptor = UiTextureHandle::Invalid; // Ready to pass straight to ImGui::Image().
        int width = 0;
        int height = 0;
        std::size_t vertexCount = 0;
        std::size_t triangleCount = 0;
        std::size_t clusterCount = 0; // 0 = the mesh has no cluster table.
    };

    // Renders one fresh frame of `absolutePath`'s mesh into an internal off-screen texture of the given size
    // (at least 1x1) and returns it ready to display, or nullopt when the path is not a valid non-empty mesh asset.
    std::optional<Preview> Render(Renderer& renderer, const std::string& absolutePath, int viewportWidth, int viewportHeight);

    // Releases every held GPU resource after the GPU is idle. Also called by the destructor; safe to call repeatedly.
    void Reset();

private:
    bool EnsureMeshUploaded(Renderer& renderer, const std::string& absolutePath);

    std::unique_ptr<MeshPreviewRenderer> m_preview; // Built on the first Render().
    bool m_previewBuildFailed = false;

    std::string m_cachedPath;
    std::filesystem::file_time_type m_cachedWriteTime{};
    bool m_cachedIsValid = false; // True if m_cachedPath resolved to a valid, non-empty Mesh *.gta last time.

    std::size_t m_vertexCount = 0;
    std::size_t m_indexCount = 0;
    std::size_t m_clusterCount = 0;

    // Bounding sphere of the uploaded mesh (object space): frames the camera and centers the spin.
    Vec3 m_boundsCenter = Vec3::Zero();
    float m_boundsRadius = 1.0f;
};

} // namespace gte
