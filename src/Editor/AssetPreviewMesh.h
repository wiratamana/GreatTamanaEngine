#pragma once

#include "../Math/Vec3.h"

#include <volk.h>

#include <cstdint>
#include <filesystem>
#include <memory>
#include <optional>
#include <string>

namespace gte {

class Renderer;
class Buffer;
class RenderTexture;

// Renders a live, slowly-spinning 3D preview of an on-disk *.gta
// AssetType::Mesh file (see src/Assets/MeshFile.h/AssetImporter.h - the
// result of importing a .pmx model) for the Editor's "Inspector" panel to
// display via ImGui::Image() when such an asset is selected (see
// Panels/InspectorPanel.cpp) - the mesh equivalent of AssetPreviewTexture's
// role for AssetType::Texture, but rendering a live 3D view instead of
// displaying a static decoded image.
//
// Unlike AssetPreviewTexture (which decodes+uploads ONCE and reuses the same
// static texture every frame), this class re-renders EVERY call - the
// preview auto-rotates (a fixed-speed spin around the mesh's own up axis,
// driven by ImGui::GetTime() - no per-frame state needs to be tracked for
// this), so a fresh frame is genuinely needed each time the Inspector shows
// it. The GPU vertex/index buffers themselves (and the bounding-sphere
// auto-framing they were computed from) ARE still cached exactly like
// AssetPreviewTexture caches its decoded texture - only re-uploaded when the
// selected path or that file's own last-write-time changes.
//
// Deliberately bypasses Renderer::CreatePipeline()/Renderer::CreateMesh()/
// Renderer::Submit() entirely: those are hardcoded to this engine's shared
// position+color Vertex layout (src/Renderer/Vertex.h), whereas a lit mesh
// preview needs a per-vertex NORMAL instead. Builds its own small
// VkPipeline/VkPipelineLayout (MeshPreview.vert/.frag) directly and records
// its own vkCmdBindPipeline/vkCmdBindVertexBuffers/vkCmdBindIndexBuffer/
// vkCmdDrawIndexed calls via a Renderer::RenderOffscreen() recordExtra
// callback - this is exactly the kind of thing AGENTS.md's "Editor Module
// Structure" section already sanctions ("Vulkan types are fine to use
// directly anywhere in this folder... precisely so an external Vulkan-based
// rendering backend... owned by the Editor module can use them directly"),
// the same boundary Dear ImGui's own Vulkan backend already sits behind.
// Still uses Renderer's public factories for everything else (CreateBuffer/
// CreateDeviceLocalBuffer/CreateRenderTexture/RenderOffscreen), so it never
// touches a raw VmaAllocator/VkCommandPool itself.
//
// Owns its GPU buffers/RenderTexture/ImGui descriptor/pipeline for as long
// as they're needed - all released by Reset() (called by the destructor,
// and MUST also be called explicitly by ImGuiEditorLayer's destructor
// BEFORE ImGui_ImplVulkan_Shutdown(), same requirement as
// AssetPreviewTexture::Reset()).
class AssetPreviewMesh {
public:
    AssetPreviewMesh() = default;
    ~AssetPreviewMesh();

    AssetPreviewMesh(const AssetPreviewMesh&) = delete;
    AssetPreviewMesh& operator=(const AssetPreviewMesh&) = delete;

    struct Preview {
        VkDescriptorSet descriptor = VK_NULL_HANDLE; // Ready to pass straight to ImGui::Image().
        int width = 0;
        int height = 0;
        std::size_t vertexCount = 0;
        std::size_t triangleCount = 0;
    };

    // Renders one fresh frame of `absolutePath`'s mesh into an internally-
    // owned off-screen RenderTexture sized `viewportWidth` x
    // `viewportHeight` (resized on demand, same as the Editor's Game/Scene
    // views), auto-framed to the mesh's own bounding sphere and slowly
    // spinning, and returns a ready-to-display preview - or std::nullopt if
    // `absolutePath` doesn't currently resolve to a valid *.gta
    // AssetType::Mesh file (missing file, bad magic/wrong asset type,
    // corrupt/truncated payload, or an empty mesh with zero vertices) - the
    // caller (InspectorPanel) falls back to plain AssetMetadata in that
    // case, exactly like AssetPreviewTexture::Resolve()'s own failure mode.
    // `viewportWidth`/`viewportHeight` are clamped to at least 1 internally,
    // so a momentarily-zero-sized panel never attempts to create a
    // zero-sized RenderTexture.
    std::optional<Preview> Render(Renderer& renderer, const std::string& absolutePath, int viewportWidth, int viewportHeight);

    // Releases every currently-held GPU resource (vertex/index buffers,
    // RenderTexture, ImGui descriptor, pipeline) - waiting for the GPU to be
    // idle first, same reasoning as AssetPreviewTexture::Reset(). Called by
    // the destructor, and safe to call repeatedly/on an already-empty
    // instance.
    void Reset();

private:
    struct MeshGpuData;

    void EnsurePipeline(Renderer& renderer);
    bool EnsureMeshUploaded(Renderer& renderer, const std::string& absolutePath);
    void EnsureRenderTexture(Renderer& renderer, int width, int height);

    VkDevice m_device = VK_NULL_HANDLE;

    // Lazily built on first Render() call, then reused for every
    // subsequently-selected mesh asset - unlike the per-asset vertex/index
    // buffers below, this pipeline's shape (vertex layout, push constant
    // range, color/depth format) never depends on WHICH mesh is selected.
    VkPipelineLayout m_pipelineLayout = VK_NULL_HANDLE;
    VkPipeline m_pipeline = VK_NULL_HANDLE;

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
    VkDescriptorSet m_descriptor = VK_NULL_HANDLE;
    int m_texWidth = 0;
    int m_texHeight = 0;
};

} // namespace gte
