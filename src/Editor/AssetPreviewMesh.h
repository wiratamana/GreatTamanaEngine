#pragma once

#include "../Math/Vec3.h"
#include "../Core/GraphicsHandles.h"
#include "../Core/UiTextureHandle.h"
#include "../Renderer/Compute/ComputeDescriptorSet.h"
#include "../Renderer/Pipeline.h"

#include <cstdint>
#include <filesystem>
#include <memory>
#include <optional>
#include <string>

namespace gte {

class Renderer;
class Buffer;
class Texture2D;
class RenderTexture;

// Live, slowly spinning 3D preview of a mesh asset (*.gta) for the Inspector panel, shown through ImGui::Image().
// It re-renders every call; the GPU buffers and the framing are cached until the path or its write time changes.
// Builds its pipeline through the configurable pipeline API and records through CommandBuffer in a
// Renderer::RenderOffscreen() callback.
// Call Reset() before the UI backend shuts down (the destructor also calls it).
class AssetPreviewMesh {
public:
    AssetPreviewMesh() = default;
    ~AssetPreviewMesh();

    AssetPreviewMesh(const AssetPreviewMesh&) = delete;
    AssetPreviewMesh& operator=(const AssetPreviewMesh&) = delete;

    struct Preview {
        UiTextureHandle descriptor = UiTextureHandle::Invalid; // Ready to pass straight to ImGui::Image().
        int width = 0;
        int height = 0;
        std::size_t vertexCount = 0;
        std::size_t triangleCount = 0;
    };

    // Renders one fresh frame of `absolutePath`'s mesh into an internal off-screen texture of the given size
    // (at least 1x1) and returns it ready to display, or nullopt when the path is not a valid non-empty mesh asset.
    std::optional<Preview> Render(Renderer& renderer, const std::string& absolutePath, int viewportWidth, int viewportHeight);

    // Releases every held GPU resource after the GPU is idle. Also called by the destructor; safe to call repeatedly.
    void Reset();

private:
    struct MeshGpuData;

    void EnsurePipeline(Renderer& renderer);
    bool EnsureMeshUploaded(Renderer& renderer, const std::string& absolutePath);
    void EnsureRenderTexture(Renderer& renderer, int width, int height);

    Renderer* m_renderer = nullptr; // Non-owning - needed to destroy/release every handle below.

    // Built on the first Render() call, then reused for every mesh.
    std::unique_ptr<Pipeline> m_pipeline;
    DescriptorSetLayoutHandle m_setZeroFillerLayoutHandle = DescriptorSetLayoutHandle::Invalid;
    DescriptorSetLayoutHandle m_setOneLayoutHandle = DescriptorSetLayoutHandle::Invalid;

    // Neutral stand-in for the shared set=1 lighting interface: a 1x1 white texture at binding 0 and a
    // zero-filled UBO at binding 8. The set comes from Renderer's shared pool and is never freed individually.
    ComputeDescriptorSet m_localSetOneDescriptorSet;
    std::unique_ptr<Texture2D> m_localWhiteTexture;
    std::unique_ptr<Buffer> m_localUniformBuffer;

    std::string m_cachedPath;
    std::filesystem::file_time_type m_cachedWriteTime{};
    bool m_cachedIsValid = false; // True if m_cachedPath resolved to a valid, non-empty Mesh *.gta last time.

    std::unique_ptr<Buffer> m_vertexBuffer;
    std::unique_ptr<Buffer> m_indexBuffer;
    std::uint32_t m_vertexCount = 0;
    std::uint32_t m_indexCount = 0;

    // Bounding sphere of the currently-uploaded mesh (object space) - used
    // to auto-frame the fixed preview camera and to recenter the mesh at
    // the origin before spinning it (see Render()'s own model-matrix
    // comment). Recomputed only when the mesh itself is re-uploaded above.
    Vec3 m_boundsCenter = Vec3::Zero();
    float m_boundsRadius = 1.0f;

    std::unique_ptr<RenderTexture> m_renderTexture;
    UiTextureHandle m_descriptor = UiTextureHandle::Invalid;
    int m_texWidth = 0;
    int m_texHeight = 0;
};

} // namespace gte
