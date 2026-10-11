#include "SceneGridRenderer.h"

#include "../Core/PipelineDesc.h"
#include "../Renderer/Renderer.h"

#include <cstring>
#include <fstream>
#include <stdexcept>
#include <vector>

namespace gte {

namespace {

std::vector<std::byte> ReadShaderFileBytes(const std::string& path)
{
    std::ifstream file(path, std::ios::ate | std::ios::binary);
    if (!file.is_open()) {
        throw std::runtime_error(
            "SceneGridRenderer: failed to open shader file '" + path + "' - was it compiled? See cmake/CompileShaders.cmake.");
    }
    const std::size_t size = static_cast<std::size_t>(file.tellg());
    std::vector<std::byte> buffer(size);
    file.seekg(0);
    file.read(reinterpret_cast<char*>(buffer.data()), static_cast<std::streamsize>(size));
    return buffer;
}

} // namespace

SceneGridRenderer::~SceneGridRenderer()
{
    Reset();
}

void SceneGridRenderer::Reset()
{
    if (m_device != VK_NULL_HANDLE && m_pipeline) {
        vkDeviceWaitIdle(m_device);
    }
    m_pipeline.reset();
}

void SceneGridRenderer::EnsurePipeline(Renderer& renderer)
{
    if (m_pipeline) {
        return;
    }
    m_device = renderer.GetVulkanContextInfo().device;

    // Full-screen triangle synthesized from gl_VertexIndex - no vertex
    // input. Depth TEST enabled (occluded correctly by real scene geometry),
    // depth WRITE disabled (a semi-transparent overlay must never poison the
    // depth buffer), standard alpha blending.
    PipelineDesc desc = PipelinePreset::AlphaBlend(renderer.ColorPixelFormat(), renderer.DepthPixelFormat());
    desc.vertexLayout = VertexLayout::None;
    desc.cullMode = CullMode::None;
    // mat4 invViewProj immediately followed by mat4 viewProj, 128 bytes
    // total, FRAGMENT stage only - matches SceneGrid.frag's own
    // layout(push_constant) block exactly. Deliberately NOT
    // ShaderStageMask::Vertex (unlike every other pipeline in this engine) -
    // SceneGrid.vert reads no push constants at all.
    desc.layout.pushConstantByteSize = sizeof(float) * 32; // 2 x mat4
    desc.layout.pushConstantStages = ShaderStageMask::Fragment;

    const std::vector<std::byte> vertSpirv = ReadShaderFileBytes("shaders/SceneGrid.vert.spv");
    const std::vector<std::byte> fragSpirv = ReadShaderFileBytes("shaders/SceneGrid.frag.spv");
    m_pipeline = std::make_unique<Pipeline>(renderer.CreatePipeline(desc, vertSpirv, fragSpirv));
}

void SceneGridRenderer::Draw(Renderer& renderer, VkCommandBuffer cmd, const Mat4& sceneViewProjection)
{
    m_device = renderer.GetVulkanContextInfo().device;

    Mat4 invViewProj;
    if (!sceneViewProjection.TryInverse(invViewProj)) {
        return; // Degenerate camera matrix this frame - draw nothing rather than assert/crash.
    }

    EnsurePipeline(renderer);

    struct PushConstants {
        float invViewProj[16];
        float viewProj[16];
    } pushConstants;
    std::memcpy(pushConstants.invViewProj, invViewProj.Data(), sizeof(pushConstants.invViewProj));
    std::memcpy(pushConstants.viewProj, sceneViewProjection.Data(), sizeof(pushConstants.viewProj));

    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, m_pipeline->Native());
    vkCmdPushConstants(
        cmd, m_pipeline->Layout(), VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(pushConstants), &pushConstants);
    vkCmdDraw(cmd, 3, 1, 0, 0);
}

} // namespace gte
