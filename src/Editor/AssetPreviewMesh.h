#pragma once

#include "../Math/Vec3.h"
#include "../Core/GraphicsHandles.h"
#include "../Core/UiTextureHandle.h"
#include "../Renderer/Pipeline.h"

#include <volk.h>

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
// Needs a per-vertex NORMAL (not this engine's shared position+color
// Vertex layout), so it builds its pipeline through Renderer's configurable
// pipeline API (Core/PipelineDesc.h, VertexLayout::PositionNormal) instead
// of the fixed-shape path-based CreatePipeline() overload. Records its draw
// through gte::rg::CommandBuffer's own high-level methods (BindPipeline/
// BindDescriptorSet/PushConstants/BindVertexBuffer/BindIndexBuffer/
// DrawIndexed) via a Renderer::RenderOffscreen() recordExtra callback -
// never touches CommandBuffer::Native()/a raw vkCmd* call.
//
// Owns its GPU buffers/RenderTexture/UI-texture handle/pipeline for as long
// as they're needed - all released by Reset() (called by the destructor,
// and MUST also be called explicitly by ImGuiEditorLayer's destructor
// BEFORE the ImGui Vulkan backend shuts down, same requirement as
// AssetPreviewTexture::Reset()).
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
    // RenderTexture, UI-texture handle, pipeline) - waiting for the GPU to
    // be idle first, same reasoning as AssetPreviewTexture::Reset(). Called
    // by the destructor, and safe to call repeatedly/on an already-empty
    // instance.
    void Reset();

private:
    struct MeshGpuData;

    void EnsurePipeline(Renderer& renderer);
    bool EnsureMeshUploaded(Renderer& renderer, const std::string& absolutePath);
    void EnsureRenderTexture(Renderer& renderer, int width, int height);

    Renderer* m_renderer = nullptr; // Non-owning - needed to destroy/release every handle below.

    // Lazily built on first Render() call, then reused for every
    // subsequently-selected mesh asset - built via Renderer's configurable
    // pipeline API (Core/PipelineDesc.h) instead of hand-rolled Vulkan calls.
    std::unique_ptr<Pipeline> m_pipeline;
    DescriptorSetLayoutHandle m_setZeroFillerLayoutHandle = DescriptorSetLayoutHandle::Invalid;
    DescriptorSetLayoutHandle m_setOneLayoutHandle = DescriptorSetLayoutHandle::Invalid;

    // MeshPreview.frag samples the shared set=1 lighting/shadow interface -
    // this preview has no real scene light/shadow context, so it builds its
    // own tiny, always-neutral stand-in: a 1x1 white dummy texture at
    // binding 0 + a zero-filled dummy UBO at binding 8. Allocated directly
    // against m_setOneLayoutHandle via Renderer::AllocateComputeDescriptorSet()
    // - no second, hand-built VkDescriptorSetLayout/VkDescriptorPool of its
    // own anymore; the returned set comes from Renderer's own shared
    // descriptor pool and is never individually freed (same convention as
    // every other compute-shaped descriptor set in this engine).
    VkDescriptorSet m_localSetOneDescriptorSet = VK_NULL_HANDLE;
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
