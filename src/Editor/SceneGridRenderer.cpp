#include "SceneGridRenderer.h"

#include "../Core/PipelineDesc.h"
#include "../Renderer/Renderer.h"
#include "../Renderer/ShaderFile.h"

#include <cstring>
#include <stdexcept>
#include <vector>

namespace gte {

SceneGridRenderer::~SceneGridRenderer()
{
    Reset();
}

void SceneGridRenderer::Reset()
{
    if (m_renderer != nullptr && m_pipeline) {
        m_renderer->WaitForGpuIdle();
    }
    m_pipeline.reset();
}

void SceneGridRenderer::EnsurePipeline(Renderer& renderer)
{
    if (m_pipeline) {
        return;
    }
    m_renderer = &renderer;

    // Fullscreen triangle from gl_VertexIndex. Depth test on, depth write off: a translucent overlay must not touch depth.
    PipelineDesc desc = PipelinePreset::AlphaBlend(renderer.ColorPixelFormat(), renderer.DepthPixelFormat());
    desc.vertexLayout = VertexLayout::None;
    desc.cullMode = CullMode::None;
    // Fragment-only push constants (invViewProj + viewProj); must match SceneGrid.frag's push block.
    desc.layout.pushConstantByteSize = sizeof(float) * 32; // 2 x mat4
    desc.layout.pushConstantStages = ShaderStageMask::Fragment;

    const std::vector<std::byte> vertSpirv = ReadShaderFileBytes("shaders/SceneGrid.vert.spv");
    const std::vector<std::byte> fragSpirv = ReadShaderFileBytes("shaders/SceneGrid.frag.spv");
    m_pipeline = std::make_unique<Pipeline>(renderer.CreatePipeline(desc, vertSpirv, fragSpirv));
}

void SceneGridRenderer::Draw(Renderer& renderer, VkCommandBuffer cmd, const Mat4& sceneViewProjection)
{
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
