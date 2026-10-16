#include "CommandBuffer.h"

#include "RecordingAccess.h"
#include "../VulkanFormatInfo.h"

#include "../Buffer.h"
#include "../Compute/ComputeDispatch.h"
#include "../Compute/ComputePipeline.h"
#include "../Pipeline.h"
#include "../Renderer.h"
#include "../Vulkan/ShaderStageMask.h"
#include "../../Core/Logging.h"

#include <atomic>
#include <cassert>
#include <cstring>

namespace gte::rg {

namespace {

void LogUndefinedWriteFormatOnce()
{
    static std::atomic_flag logged;
    if (!logged.test_and_set()) {
        GTE_LOG_ERROR("CommandBuffer", "Draw target has a pixel format the engine does not know; Frame Debugger capture skipped.");
    }
}

} // namespace

CommandBuffer::CommandBuffer(VkCommandBuffer cmd, Renderer* renderer, DrawStats* passDrawStats,
    WriteTargetInfo writeTarget, FrameDebuggerEventSink* eventSink) noexcept
    : m_cmd(cmd)
    , m_renderer(renderer)
    , m_passDrawStats(passDrawStats)
    , m_writeTarget(writeTarget)
    , m_eventSink(eventSink)
{
}

void CommandBuffer::SetPushConstants(const void* data, std::uint32_t size) noexcept
{
    assert((m_boundComputePipeline == nullptr
               || PushConstantSizeMatches(size, m_boundComputePipeline->PushConstantSize()))
        && "CommandBuffer::SetPushConstants(): supplied size does not match the bound ComputePipeline's own "
           "reflected push-constant size - see ComputePipeline::PushConstantSize()/ShaderReflection.h.");
    assert(size <= kMaxPushConstantBytes && "CommandBuffer::SetPushConstants(): push constants exceed 128 bytes.");
    if (size > kMaxPushConstantBytes) {
        static std::atomic_flag logged;
        if (!logged.test_and_set()) {
            GTE_LOG_ERROR("CommandBuffer", "SetPushConstants: block is larger than 128 bytes; push constants dropped.");
        }
        m_pushConstantSize = 0;
        return;
    }
    if (size > 0 && data == nullptr) {
        static std::atomic_flag logged;
        if (!logged.test_and_set()) {
            GTE_LOG_ERROR("CommandBuffer", "SetPushConstants: null data with a non-zero size; push constants dropped.");
        }
        m_pushConstantSize = 0;
        return;
    }
    if (size > 0) {
        std::memcpy(m_pushConstantData.data(), data, size);
    }
    m_pushConstantSize = size;
}

std::function<void(bool, std::uint32_t, std::uint32_t)> CommandBuffer::MakeRecordDrawStatsCallback() const
{
    DrawStats* passDrawStats = m_passDrawStats;
    return [passDrawStats](bool hasIndexBuffer, std::uint32_t vertexCount, std::uint32_t indexCount) {
        if (passDrawStats != nullptr) {
            AccumulateDrawStats(*passDrawStats, hasIndexBuffer, vertexCount, indexCount);
        }
    };
}

void CommandBuffer::Dispatch(std::uint32_t groupX, std::uint32_t groupY, std::uint32_t groupZ)
{
    assert(m_renderer != nullptr
        && "CommandBuffer::Dispatch(): no Renderer available - this CommandBuffer was not built from a real "
           "PassContext (see RenderGraph::BuildPassContext()).");
    assert(m_boundComputePipeline != nullptr
        && "CommandBuffer::Dispatch(): no compute pipeline bound - call BindComputePipeline() first.");
    if (m_renderer == nullptr || m_boundComputePipeline == nullptr) {
        return;
    }

    m_renderer->BeginGraphPassRecording(m_cmd, MakeRecordDrawStatsCallback());
    m_renderer->Dispatch(*m_boundComputePipeline, m_boundDescriptorSet,
        m_pushConstantSize > 0 ? m_pushConstantData.data() : nullptr, m_pushConstantSize, groupX, groupY, groupZ);
    m_renderer->EndGraphPassRecording();
}

void CommandBuffer::DispatchOverSize(std::uint32_t width, std::uint32_t height, std::uint32_t depth)
{
    assert(m_boundComputePipeline != nullptr
        && "CommandBuffer::DispatchOverSize(): no compute pipeline bound - call BindComputePipeline() first.");
    if (m_boundComputePipeline == nullptr) {
        return;
    }

    const Extent3D localSize = m_boundComputePipeline->LocalGroupSize();
    const Extent3D groupCounts = ComputeGroupCount3D(Extent3D{ width, height, depth }, localSize);
    Dispatch(groupCounts.width, groupCounts.height, groupCounts.depth);
}

void CommandBuffer::Draw(const Pipeline& pipeline, const Mesh& mesh, const Mat4& modelMatrix,
    const Mat4& viewProjMatrix, DescriptorSetHandle materialDescriptorSet, DescriptorSetHandle sceneServicesSet)
{
    assert(m_renderer != nullptr
        && "CommandBuffer::Draw(): no Renderer available - this CommandBuffer was not built from a real "
           "PassContext (see RenderGraph::BuildPassContext()).");
    if (m_renderer == nullptr) {
        return;
    }

    m_renderer->BeginGraphPassRecording(m_cmd, MakeRecordDrawStatsCallback());
    m_renderer->Submit(pipeline, mesh, modelMatrix, viewProjMatrix, materialDescriptorSet, sceneServicesSet);
    m_renderer->EndGraphPassRecording();

    // Notify the frame debugger (prefers the color target over depth). A format the engine does not know is
    // not reported: the debugger could not copy it.
    if (m_eventSink == nullptr) {
        return;
    }
    if (m_writeTarget.colorImage != VK_NULL_HANDLE) {
        const PixelFormat format = CapturedPixelFormatFromVk(m_writeTarget.colorFormat);
        if (format == PixelFormat::Undefined) {
            LogUndefinedWriteFormatOnce();
            return;
        }
        m_eventSink->NoteCommandResult(AttachmentWrite{ RecordingAccess::ImageToken(m_writeTarget.colorImage),
            Extent2D{ m_writeTarget.colorExtent.width, m_writeTarget.colorExtent.height }, format, false });
    } else if (m_writeTarget.depthImage != VK_NULL_HANDLE) {
        const PixelFormat format = FromVkFormat(m_writeTarget.depthFormat);
        if (format == PixelFormat::Undefined) {
            LogUndefinedWriteFormatOnce();
            return;
        }
        m_eventSink->NoteCommandResult(AttachmentWrite{ RecordingAccess::ImageToken(m_writeTarget.depthImage),
            Extent2D{ m_writeTarget.colorExtent.width, m_writeTarget.colorExtent.height }, format, true });
    }
}

void CommandBuffer::BindPipeline(const Pipeline& pipeline) noexcept
{
    m_boundGraphicsLayout = pipeline.Layout();
    vkCmdBindPipeline(m_cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline.Native());
}

void CommandBuffer::BindDescriptorSet(std::uint32_t setIndex, VkDescriptorSet set) noexcept
{
    assert(m_boundGraphicsLayout != VK_NULL_HANDLE
        && "CommandBuffer::BindDescriptorSet(uint32_t, VkDescriptorSet): no graphics pipeline bound - call "
           "BindPipeline() first.");
    if (m_boundGraphicsLayout == VK_NULL_HANDLE) {
        return;
    }
    vkCmdBindDescriptorSets(m_cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, m_boundGraphicsLayout, setIndex, 1, &set, 0, nullptr);
}

void CommandBuffer::PushConstants(const void* data, std::uint32_t size, ShaderStageMask stages) noexcept
{
    assert(m_boundGraphicsLayout != VK_NULL_HANDLE
        && "CommandBuffer::PushConstants(): no graphics pipeline bound - call BindPipeline() first.");
    if (m_boundGraphicsLayout == VK_NULL_HANDLE) {
        return;
    }
    vkCmdPushConstants(m_cmd, m_boundGraphicsLayout, ToVkShaderStageFlags(stages), 0, size, data);
}

void CommandBuffer::SetViewportAndScissor(std::uint32_t width, std::uint32_t height) noexcept
{
    VkViewport viewport{};
    viewport.width = static_cast<float>(width);
    viewport.height = static_cast<float>(height);
    viewport.minDepth = 0.0f;
    viewport.maxDepth = 1.0f;
    vkCmdSetViewport(m_cmd, 0, 1, &viewport);

    VkRect2D scissor{};
    scissor.extent = VkExtent2D{ width, height };
    vkCmdSetScissor(m_cmd, 0, 1, &scissor);
}

void CommandBuffer::BindVertexBuffer(const Buffer& buffer, std::uint32_t binding) noexcept
{
    const VkBuffer native = buffer.Native();
    const VkDeviceSize offset = 0;
    vkCmdBindVertexBuffers(m_cmd, binding, 1, &native, &offset);
}

void CommandBuffer::BindIndexBuffer(const Buffer& buffer) noexcept
{
    vkCmdBindIndexBuffer(m_cmd, buffer.Native(), 0, VK_INDEX_TYPE_UINT32);
}

void CommandBuffer::DrawIndexed(std::uint32_t indexCount, std::uint32_t firstIndex, std::int32_t vertexOffset) noexcept
{
    vkCmdDrawIndexed(m_cmd, indexCount, 1, firstIndex, vertexOffset, 0);
}

void CommandBuffer::Draw(std::uint32_t vertexCount, std::uint32_t firstVertex) noexcept
{
    vkCmdDraw(m_cmd, vertexCount, 1, firstVertex, 0);
}

} // namespace gte::rg
