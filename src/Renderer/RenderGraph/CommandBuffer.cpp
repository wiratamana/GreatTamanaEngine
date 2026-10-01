#include "CommandBuffer.h"

#include "../ComputeDispatch.h"
#include "../ComputePipeline.h"
#include "../Renderer.h"

#include <cassert>

namespace gte::rg {

CommandBuffer::CommandBuffer(VkCommandBuffer cmd, Renderer* renderer, DrawStats* passDrawStats) noexcept
    : m_cmd(cmd)
    , m_renderer(renderer)
    , m_passDrawStats(passDrawStats)
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
    const Mat4& viewProjMatrix, VkDescriptorSet materialDescriptorSet)
{
    assert(m_renderer != nullptr
        && "CommandBuffer::Draw(): no Renderer available - this CommandBuffer was not built from a real "
           "PassContext (see RenderGraph::BuildPassContext()).");
    if (m_renderer == nullptr) {
        return;
    }

    m_renderer->BeginGraphPassRecording(m_cmd, MakeRecordDrawStatsCallback());
    m_renderer->Submit(pipeline, mesh, modelMatrix, viewProjMatrix, materialDescriptorSet);
    m_renderer->EndGraphPassRecording();
}

} // namespace gte::rg
