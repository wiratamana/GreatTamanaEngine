#pragma once

#include <volk.h>

#include <string>

namespace gte {

// RAII wrapper around a VkPipeline + its VkPipelineLayout, built for dynamic
// rendering (no VkRenderPass/VkFramebuffer) against an exact color format.
// Owns both for its entire lifetime: created in the constructor, destroyed
// in the destructor. Construct via Renderer::CreatePipeline() rather than
// directly (same convention as Buffer/RenderTexture/Mesh) - it needs the
// device and the exact color format Renderer's swapchain negotiated (see
// AGENTS.md, "Render Target Format Matching": a pipeline built for one
// format cannot legally draw into a target of a different one).
//
// Deliberately minimal today: one hardcoded vertex format (see Vertex.h),
// no descriptor sets/push constants, and viewport/scissor left as dynamic
// state so the exact same pipeline can draw into either the swapchain or an
// Editor RenderTexture, whatever size each currently is. Grow this (a real
// shader/material system, descriptor sets for textures/uniforms, multiple
// vertex formats, ...) once there's more than one hardcoded triangle to
// draw.
class Pipeline {
public:
    // vertexShaderSpirvPath/fragmentShaderSpirvPath point at compiled
    // SPIR-V binaries - see cmake/CompileShaders.cmake, which compiles
    // src/Shaders/*.vert/*.frag into "<exe dir>/shaders/*.spv" at build
    // time (gitignored; only the GLSL source is version-controlled). NOT
    // paths to GLSL source.
    Pipeline(VkDevice device, VkFormat colorFormat, const std::string& vertexShaderSpirvPath,
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
