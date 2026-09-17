#include "FrameDebuggerCapture.h"

#include <algorithm>
#include <utility>

namespace gte {

FrameDebuggerStandardPipelineState DescribeStandardPipelineState()
{
    // Every value below is transcribed directly from Pipeline.cpp's own
    // real, hardcoded construction code (confirmed at implementation
    // time - never guessed from an operator's name alone):
    //   - colorBlendAttachment.blendEnable = VK_FALSE                      -> "Opaque (no blend)".
    //   - rasterizer.depthClampEnable left at its zero-initialized default
    //     (VK_FALSE) -> real geometry IS clipped against the near/far
    //     planes -> ZClip "On".
    //   - depthStencil.depthTestEnable = VK_TRUE, depthCompareOp =
    //     VK_COMPARE_OP_LESS - this is "Less", NOT "LEqual"/"LessEqual"
    //     (that would be the currently-unused VK_COMPARE_OP_LESS_OR_EQUAL).
    //   - depthStencil.depthWriteEnable = VK_TRUE -> ZWrite "On".
    //   - rasterizer.cullMode = VK_CULL_MODE_NONE -> Cull "None".
    //   - depthStencil.stencilTestEnable = VK_FALSE, no stencil struct
    //     populated anywhere -> every stencil field "n/a (no stencil test)".
    FrameDebuggerStandardPipelineState state;
    state.blendMode = "Opaque (no blend)";
    state.zClip = "On";
    state.zTest = "Less";
    state.zWrite = "On";
    state.cull = "None";
    state.stencilRef = "n/a (no stencil test)";
    state.stencilComp = "n/a (no stencil test)";
    state.stencilPass = "n/a (no stencil test)";
    state.stencilFail = "n/a (no stencil test)";
    state.stencilZFail = "n/a (no stencil test)";
    return state;
}

FrameDebuggerStandardPipelineState DescribeSkyBackgroundPipelineState()
{
    // Every value below is transcribed directly from
    // AtmosphereSkyBackgroundRenderer.cpp's own EnsurePipeline() real,
    // hardcoded construction code (confirmed at implementation time -
    // never guessed):
    //   - colorBlendAttachment.blendEnable = VK_FALSE -> "Opaque (no blend)".
    //   - rasterizer.depthClampEnable left at its zero-initialized default
    //     (VK_FALSE) -> ZClip "On" (same as DescribeStandardPipelineState()).
    //   - depthStencil.depthTestEnable = VK_TRUE, depthCompareOp =
    //     VK_COMPARE_OP_EQUAL - "Equal", DELIBERATELY DIFFERENT from
    //     DescribeStandardPipelineState()'s own "Less" - this pass only
    //     ever survives at a pixel whose depth is still exactly the
    //     frame's own clear value (see that renderer class's own header
    //     comment for the full reasoning).
    //   - depthStencil.depthWriteEnable = VK_FALSE -> "Off", DELIBERATELY
    //     DIFFERENT from DescribeStandardPipelineState()'s own "On" -
    //     nothing is ever meant to occlude behind the sky.
    //   - rasterizer.cullMode = VK_CULL_MODE_NONE -> "None" (same).
    //   - depthStencil.stencilTestEnable = VK_FALSE, no stencil struct
    //     populated -> every stencil field "n/a (no stencil test)" (same).
    FrameDebuggerStandardPipelineState state;
    state.blendMode = "Opaque (no blend)";
    state.zClip = "On";
    state.zTest = "Equal";
    state.zWrite = "Off";
    state.cull = "None";
    state.stencilRef = "n/a (no stencil test)";
    state.stencilComp = "n/a (no stencil test)";
    state.stencilPass = "n/a (no stencil test)";
    state.stencilFail = "n/a (no stencil test)";
    state.stencilZFail = "n/a (no stencil test)";
    return state;
}

void FrameDebuggerCaptureContext::RecordDraw(
    const std::string& pipelineDebugName, const std::string& materialTextureDebugName, const Mat4& viewProjection)
{
    ++m_drawCallCount;

    if (!pipelineDebugName.empty()
        && std::find(m_pipelineDebugNames.begin(), m_pipelineDebugNames.end(), pipelineDebugName)
            == m_pipelineDebugNames.end()) {
        m_pipelineDebugNames.push_back(pipelineDebugName);
    }

    if (!materialTextureDebugName.empty()
        && std::find(m_materialTextureDebugNames.begin(), m_materialTextureDebugNames.end(), materialTextureDebugName)
            == m_materialTextureDebugNames.end()) {
        m_materialTextureDebugNames.push_back(materialTextureDebugName);
    }

    m_lastViewProjection = viewProjection;
}

void FrameDebuggerCaptureContext::RecordEntityDraw(std::uint32_t entityIndex, std::uint32_t entityGeneration,
    const std::string& displayName, const std::string& pipelineDebugName, const std::string& materialTextureDebugName,
    std::uint32_t triangleCount)
{
    FrameDebuggerDrawRecord record;
    record.entityIndex = entityIndex;
    record.entityGeneration = entityGeneration;
    record.displayName = displayName;
    record.pipelineDebugName = pipelineDebugName;
    record.materialTextureDebugName = materialTextureDebugName;
    record.triangleCount = triangleCount;
    m_drawRecords.push_back(std::move(record));
}

void FrameDebuggerCaptureContext::RecordSkyBackgroundDraw(const std::string& pipelineDebugName)
{
    // Reuses RecordDraw()'s own existing dedup/draw-call-count/last-view-
    // projection bookkeeping (Locked Design Decision 6,
    // PHASE0_MASTER_STRATEGY.md) - this is what makes the PARENT "GameView"
    // leaf's own aggregate `shaderName` (built by joining
    // capture.PipelineDebugNames() in FrameDebuggerData.cpp's
    // BuildGameViewLeaf()) correctly include the sky's own real shader pair
    // too, alongside whatever mesh shaders ran this frame.
    RecordDraw(pipelineDebugName, /*materialTextureDebugName=*/std::string(), m_lastViewProjection);

    FrameDebuggerDrawRecord record;
    record.displayName = pipelineDebugName;
    record.pipelineDebugName = pipelineDebugName;
    // A real, honest fact - AtmosphereSkyBackgroundRenderer::Draw() issues
    // exactly ONE vkCmdDraw(cmd, 3, 1, 0, 0) call (one full-screen
    // triangle, 3 vertices) - never a placeholder/invented number.
    record.triangleCount = 1;
    record.isSkyBackgroundDraw = true;
    m_drawRecords.push_back(std::move(record));
}

void FrameDebuggerCaptureContext::Reset()
{
    m_pipelineDebugNames.clear();
    m_materialTextureDebugNames.clear();
    m_drawCallCount = 0;
    m_lastViewProjection = Mat4::Identity();
    m_drawRecords.clear();
    // task_manager/frame-debugger-7 campaign, PHASE3 - RAII-destroys any
    // leftover replay-step RenderTextures from a previous armed frame (see
    // AGENTS.md's own RAII rule) - see SetReplayStepPreviews()'s own doc
    // comment (FrameDebuggerCapture.h) for why this must be empty on every
    // frame that isn't itself a capture-trigger frame.
    m_replayStepPreviews.clear();
}

} // namespace gte
