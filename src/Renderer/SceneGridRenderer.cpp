#include "SceneGridRenderer.h"

#include "Pipeline.h"
#include "Renderer.h"
#include "ShaderFile.h"
#include "RenderGraph/CommandBuffer.h"
#include "../Core/Logging.h"
#include "../Core/PipelineDesc.h"

#include <exception>
#include <string>
#include <vector>

namespace gte {

namespace {

// Fragment-only push constants of SceneGrid.frag.
struct GridPush {
    Mat4 invViewProjection;
    Mat4 viewProjection;
};
static_assert(sizeof(GridPush) == 128, "layout differs from SceneGrid.frag");

} // namespace

SceneGridRenderer::~SceneGridRenderer()
{
    if (m_pipeline) {
        m_renderer.WaitForGpuIdle();
    }
}

bool SceneGridRenderer::EnsurePipeline()
{
    if (m_pipeline) {
        return true;
    }
    if (m_buildFailed) {
        return false;
    }

    // Fullscreen triangle from gl_VertexIndex. Depth test on, depth write off: a translucent overlay must not touch depth.
    PipelineDesc desc = PipelinePreset::AlphaBlend(m_renderer.ScenePixelFormat(), m_renderer.DepthPixelFormat());
    desc.vertexLayout = VertexLayout::None;
    desc.cullMode = CullMode::None;
    desc.layout.pushConstantByteSize = sizeof(GridPush);
    desc.layout.pushConstantStages = ShaderStageMask::Fragment;

    try {
        const std::vector<std::byte> vertSpirv = ReadShaderFileBytes("shaders/SceneGrid.vert.spv");
        const std::vector<std::byte> fragSpirv = ReadShaderFileBytes("shaders/SceneGrid.frag.spv");
        m_pipeline = std::make_unique<Pipeline>(m_renderer.CreatePipeline(desc, vertSpirv, fragSpirv));
    } catch (const std::exception& e) {
        m_buildFailed = true;
        GTE_LOG_ERROR("SceneGrid", std::string("Grid pipeline could not be built: ") + e.what());
        return false;
    }
    return true;
}

void SceneGridRenderer::Draw(const rg::RecordingContext& ctx, const Mat4& sceneViewProjection)
{
    Mat4 invViewProjection;
    if (!sceneViewProjection.TryInverse(invViewProjection)) {
        return; // Degenerate camera matrix this frame.
    }
    if (!EnsurePipeline()) {
        return;
    }

    const GridPush push{ invViewProjection, sceneViewProjection };
    rg::CommandBuffer cmd = ctx.Commands();
    cmd.BindPipeline(*m_pipeline);
    cmd.PushConstants(push, ShaderStageMask::Fragment);
    cmd.Draw(3);
}

} // namespace gte
