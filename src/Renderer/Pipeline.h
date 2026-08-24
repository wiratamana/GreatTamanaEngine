#pragma once

#include <volk.h>

#include <string>

namespace gte {

// Which per-vertex GPU layout a Pipeline expects its bound Mesh's vertex
// buffer to already be in - see Vertex.h (PositionColor) and MeshVertex.h
// (PositionNormal). A Mesh itself carries no notion of "its own" vertex
// layout (see Mesh.h - it's just a VkBuffer + a count); it is entirely up to
// whoever builds a Pipeline/Mesh PAIR to make sure both agree on this, the
// same way AGENTS.md's "Render Target Format Matching" already requires a
// Pipeline and the render target it draws into to agree on their exact
// color/depth format.
enum class VertexLayout {
    // This engine's original position+color layout (Vertex.h) - every
    // built-in primitive shape (Renderer/Primitives/PrimitiveMeshGenerator.h)
    // and the original hardcoded triangle demo use this, via Triangle.vert/
    // .frag. The default, for backward compatibility with every existing
    // Renderer::CreatePipeline() call site.
    PositionColor,

    // Position+normal (MeshVertex.h) - for an imported, indexed mesh asset
    // (a *.gta AssetType::Mesh - see Game::CreateMeshEntityFromGtaFile(),
    // src/Game/Game.h/.cpp), via Shaders/Mesh.vert/.frag. Such an asset
    // carries no per-vertex color (see src/Assets/MeshData.h), only a real
    // per-vertex normal.
    PositionNormal,

    // Position+normal+UV (MeshVertex.h's MeshVertexUv) - for an imported,
    // indexed, TEXTURED mesh submesh (a per-material slice of a *.gta
    // AssetType::Mesh's triangle-index list whose material references a
    // resolvable diffuse texture - see Game::EnsureMeshAsset(),
    // src/Game/Game.h/.cpp), via Shaders/TexturedMesh.vert/.frag. Unlike
    // the other two layouts above, a Pipeline built with this layout ALSO
    // carries a descriptor-set-layout for a single combined-image-sampler
    // (set = 0, binding = 0) in its VkPipelineLayout - see
    // Pipeline's constructor `materialSetLayout` parameter and
    // GpuResourceFactory::MaterialDescriptorSetLayout() - so a per-submesh
    // MaterialTexture's own VkDescriptorSet (Renderer/MaterialTexture.h)
    // can be bound before each draw (see FrameRecorder::RecordFrame()).
    PositionNormalUv,
};

// RAII wrapper around a VkPipeline + its VkPipelineLayout, built for dynamic
// rendering (no VkRenderPass/VkFramebuffer) against an exact color AND
// depth format. Owns both for its entire lifetime: created in the
// constructor, destroyed in the destructor. Construct via
// Renderer::CreatePipeline() rather than directly (same convention as
// Buffer/RenderTexture/Mesh) - it needs the device and the exact color/depth
// formats Renderer negotiated (see AGENTS.md, "Render Target Format
// Matching": a pipeline built for one format cannot legally draw into a
// target of a different one - depth now follows this exact same rule as
// color, via Renderer::DepthFormat()/VulkanDevice::PickDepthFormat()).
// Always depth-tests/writes (VK_COMPARE_OP_LESS, standard "smaller = closer
// to the camera" convention matching this engine's PerspectiveFovLH_ZO's
// [0,1] depth range) - every render target this pipeline draws into is
// paired with a real DepthBuffer (see DepthBuffer.h/RenderTarget.h), so real
// (non-coplanar) 3D geometry is correctly occluded instead of drawing in
// whatever order it happened to be submitted in.
//
// `vertexLayout` (see VertexLayout above) selects which vertex
// binding/attribute description this pipeline is built against - still no
// descriptor sets, and viewport/scissor left as dynamic state so the exact
// same pipeline can draw into either the swapchain or an Editor
// RenderTexture, whatever size each currently is. DOES carry one push
// constant range: a "model" matrix followed immediately by a "viewProj"
// matrix (vertex stage only, offset 0, 128 bytes total - the guaranteed
// minimum maxPushConstantsSize on every conformant Vulkan implementation,
// so this never needs a per-GPU size check) - see RenderSystem.h/
// FrameRecorder.h for how a per-draw world matrix and the active Camera's
// view-projection matrix reach this via vkCmdPushConstants, and Shaders/
// Triangle.vert/Mesh.vert for the matching `layout(push_constant)` block,
// shared identically by both vertex layouts. Grow this further (a real
// shader/material system, descriptor sets for textures/uniforms, ...) once
// there's more than these two hardcoded vertex layouts to draw.
class Pipeline {
public:
    // vertexShaderSpirvPath/fragmentShaderSpirvPath point at compiled
    // SPIR-V binaries - see cmake/CompileShaders.cmake, which compiles
    // src/Shaders/*.vert/*.frag into "<exe dir>/shaders/*.spv" at build
    // time (gitignored; only the GLSL source is version-controlled). NOT
    // paths to GLSL source.
    // `materialSetLayout` is only meaningful (non-VK_NULL_HANDLE) for
    // VertexLayout::PositionNormalUv - see that enumerator's own comment -
    // and must be GpuResourceFactory::MaterialDescriptorSetLayout() exactly,
    // so every MaterialTexture's descriptor set (allocated against that
    // exact same layout) stays binding-compatible with this Pipeline's
    // layout. Left VK_NULL_HANDLE (the default) for every other vertex
    // layout, which then builds a VkPipelineLayout with no descriptor sets
    // at all, exactly as before this parameter existed.
    Pipeline(VkDevice device, VkFormat colorFormat, VkFormat depthFormat, const std::string& vertexShaderSpirvPath,
        const std::string& fragmentShaderSpirvPath, VertexLayout vertexLayout = VertexLayout::PositionColor,
        VkDescriptorSetLayout materialSetLayout = VK_NULL_HANDLE);
    ~Pipeline();

    Pipeline(const Pipeline&) = delete;
    Pipeline& operator=(const Pipeline&) = delete;

    Pipeline(Pipeline&& other) noexcept;
    Pipeline& operator=(Pipeline&& other) noexcept;

    VkPipeline Native() const noexcept { return m_pipeline; }
    VkPipelineLayout Layout() const noexcept { return m_layout; }

private:
    void Destroy() noexcept;

    VkDevice m_device = VK_NULL_HANDLE;
    VkPipelineLayout m_layout = VK_NULL_HANDLE;
    VkPipeline m_pipeline = VK_NULL_HANDLE;
};

} // namespace gte
