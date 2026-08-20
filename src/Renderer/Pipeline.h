#pragma once

#include <volk.h>

#include <string>

namespace gte {

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
// Deliberately minimal today: one hardcoded vertex format (see Vertex.h),
// still no descriptor sets, and viewport/scissor left as dynamic state so
// the exact same pipeline can draw into either the swapchain or an Editor
// RenderTexture, whatever size each currently is. DOES carry one push
// constant range: a "model" matrix followed immediately by a "viewProj"
// matrix (vertex stage only, offset 0, 128 bytes total - the guaranteed
// minimum maxPushConstantsSize on every conformant Vulkan implementation,
// so this never needs a per-GPU size check) - see RenderSystem.h/
// FrameRecorder.h for how a per-draw world matrix and the active Camera's
// view-projection matrix reach this via vkCmdPushConstants, and Shaders/
// Triangle.vert for the matching `layout(push_constant)` block. Grow this
// further (a real shader/material system, descriptor sets for
// textures/uniforms, multiple vertex formats, ...) once there's more than
// one hardcoded triangle mesh to draw.
class Pipeline {
public:
    // vertexShaderSpirvPath/fragmentShaderSpirvPath point at compiled
    // SPIR-V binaries - see cmake/CompileShaders.cmake, which compiles
    // src/Shaders/*.vert/*.frag into "<exe dir>/shaders/*.spv" at build
    // time (gitignored; only the GLSL source is version-controlled). NOT
    // paths to GLSL source.
    Pipeline(VkDevice device, VkFormat colorFormat, VkFormat depthFormat, const std::string& vertexShaderSpirvPath,
        const std::string& fragmentShaderSpirvPath);
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
