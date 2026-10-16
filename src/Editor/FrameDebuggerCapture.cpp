#include "FrameDebuggerCapture.h"

#include "../Renderer/Renderer.h"

#include <algorithm>
#include <utility>

namespace gte {

FrameDebuggerStandardPipelineState DescribeStandardPipelineState()
{
    // Values mirror Pipeline.cpp: opaque, depth clip on, depth test Less, depth write on, no culling, no stencil.
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
    // Values mirror the sky pipeline (see the sky background renderer): depth test Equal, depth write Off.
    // The pass only survives at pixels still holding the clear depth, and must never occlude geometry.
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

void FrameDebuggerCaptureContext::Reset()
{
    m_pipelineDebugNames.clear();
    m_materialTextureDebugNames.clear();
    m_drawCallCount = 0;
    m_lastViewProjection = Mat4::Identity();
    m_drawRecords.clear();
    // Leftover replay-step targets must not outlive the capture frame.
    m_replayStepPreviews.clear();
    // Remember the previous frame's event total before zeroing the counter.
    m_lastFrameEventCount = m_nextEventIndex;
    m_nextEventIndex = 0;
}

void FrameDebuggerCaptureContext::InitializeEventCapture(Renderer& renderer) noexcept
{
    m_eventCaptureRenderer = &renderer;
}

void FrameDebuggerCaptureContext::NoteCommandResult(const AttachmentWrite& write)
{
    if (m_armedEventIndex < 0) {
        NextEventIndex(); // Keep this frame's counter advancing even while disarmed.
        return;
    }
    const int index = NextEventIndex();
    if (index != m_armedEventIndex) {
        return;
    }
    m_pendingEventCapture = write;
}

void FrameDebuggerCaptureContext::FlushPendingCapture(const rg::RecordingContext& cmd)
{
    if (!m_pendingEventCapture.has_value() || m_eventCaptureRenderer == nullptr) {
        return;
    }
    const AttachmentWrite capture = *m_pendingEventCapture;
    m_pendingEventCapture.reset();

    rg::CaptureAttachmentIntoSnapshot(
        cmd, *m_eventCaptureRenderer, m_retainedEventImage, capture, "FrameDebuggerPerDrawEventSnapshot");
    m_retainedEventIndex = m_armedEventIndex;
    m_retainedEventIsDepth = capture.isDepth;
}

} // namespace gte
