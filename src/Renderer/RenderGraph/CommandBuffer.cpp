#include "CommandBuffer.h"

#include "../Buffer.h"
#include "../Compute/ComputeDispatch.h"
#include "../Compute/ComputePipeline.h"
#include "../Pipeline.h"
#include "../Renderer.h"
#include "../Vulkan/ShaderStageMask.h"

#include <cassert>

namespace gte::rg {

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
    m_pushConstantData = data;
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
    m_renderer->Dispatch(*m_boundComputePipeline, m_boundDescriptorSet, m_pushConstantData, m_pushConstantSize,
        groupX, groupY, groupZ);
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

    // Notify frame debugger (prefers color over depth target).
    if (m_eventSink != nullptr) {
        if (m_writeTarget.colorImage != VK_NULL_HANDLE) {
            m_eventSink->NoteCommandResult(
                m_writeTarget.colorImage, m_writeTarget.colorExtent, m_writeTarget.colorFormat,
                VK_IMAGE_ASPECT_COLOR_BIT);
        } else if (m_writeTarget.depthImage != VK_NULL_HANDLE) {
            m_eventSink->NoteCommandResult(
                m_writeTarget.depthImage, m_writeTarget.colorExtent, m_writeTarget.depthFormat,
                VK_IMAGE_ASPECT_DEPTH_BIT);
        }
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
