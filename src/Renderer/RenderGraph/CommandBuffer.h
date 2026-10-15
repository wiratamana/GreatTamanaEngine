#pragma once

// High-level command buffer wrapper for render graph passes.

#include <volk.h>

#include "../../Core/PipelineDesc.h"
#include "../GraphicsHandleRegistry.h"
#include "../DrawStats.h"
#include "../../Core/FrameDebuggerEventSink.h"
#include "../../Math/Mat4.h"

#include <cstdint>
#include <functional>

namespace gte {
class Buffer;
class ComputePipeline;
class Mesh;
class Pipeline;
class Renderer;
} // namespace gte

namespace gte::rg {
struct PassContext; // forward declaration, needed only for the CommandBufferKey friendship below

// Checks if supplied push constant size matches reflected size (0 means unreflected).
constexpr bool PushConstantSizeMatches(std::uint32_t suppliedSize, std::uint32_t reflectedSize) noexcept
{
    return reflectedSize == 0 || suppliedSize == reflectedSize;
}

// Render target attachments for a pass.
struct CommandBufferWriteTargetInfo {
    VkImage colorImage = VK_NULL_HANDLE;
    VkExtent2D colorExtent{};
    VkFormat colorFormat = VK_FORMAT_UNDEFINED;
    VkImage depthImage = VK_NULL_HANDLE;
    VkFormat depthFormat = VK_FORMAT_UNDEFINED;
};

// Passkey: only PassContext and CommandBuffer itself can mint one, so a CommandBuffer cannot be built anywhere else.
class CommandBufferKey {
private:
    CommandBufferKey() = default;
    friend struct PassContext;
    friend class CommandBuffer;
};

class CommandBuffer {
public:
    using WriteTargetInfo = CommandBufferWriteTargetInfo;

    // `key` is a passkey: only PassContext::Cmd() (or the factories below) can supply one.
    CommandBuffer(CommandBufferKey key, VkCommandBuffer cmd, Renderer* renderer, DrawStats* passDrawStats,
        WriteTargetInfo writeTarget = {}, FrameDebuggerEventSink* eventSink = nullptr) noexcept;

    // Wraps a raw command buffer for call sites that have no PassContext (hooks, replay steps, immediate submits).
    // Temporary route: NativeLeakCheck lists every use outside src/Renderer.
    static CommandBuffer AdoptRaw(VkCommandBuffer cmd, Renderer* renderer = nullptr,
        DrawStats* passDrawStats = nullptr, WriteTargetInfo writeTarget = {},
        FrameDebuggerEventSink* eventSink = nullptr) noexcept
    {
        return CommandBuffer(CommandBufferKey{}, cmd, renderer, passDrawStats, writeTarget, eventSink);
    }

    // The raw command buffer, for platform glue and not-yet-migrated hand recording. NativeLeakCheck tracks every
    // use outside src/Renderer.
    VkCommandBuffer RawCommandBuffer() const noexcept { return m_cmd; }

    // Stage compute state for the next dispatch.
    void BindComputePipeline(const ComputePipeline& pipeline) noexcept { m_boundComputePipeline = &pipeline; }
    void BindDescriptorSet(VkDescriptorSet set) noexcept { m_boundDescriptorSet = set; }
    void BindDescriptorSet(DescriptorSetHandle set) noexcept { BindDescriptorSet(::gte::Native(set)); }

    // --- Graphics Recording ---

    // Bind graphics pipeline and store its layout.
    void BindPipeline(const Pipeline& pipeline) noexcept;

    // Bind descriptor set at a specific index using the current pipeline layout.
    void BindDescriptorSet(std::uint32_t setIndex, VkDescriptorSet set) noexcept;
    void BindDescriptorSet(std::uint32_t setIndex, DescriptorSetHandle set) noexcept
    {
        BindDescriptorSet(setIndex, ::gte::Native(set));
    }

    // Push graphics constants immediately.
    void PushConstants(const void* data, std::uint32_t size, ShaderStageMask stages) noexcept;

    template <typename T>
    void PushConstants(const T& data, ShaderStageMask stages) noexcept
    {
        PushConstants(&data, static_cast<std::uint32_t>(sizeof(T)), stages);
    }

    // Set viewport and scissor to full dimensions.
    void SetViewportAndScissor(std::uint32_t width, std::uint32_t height) noexcept;

    void BindVertexBuffer(const Buffer& buffer, std::uint32_t binding = 0) noexcept;
    void BindIndexBuffer(const Buffer& buffer) noexcept; // Uses VK_INDEX_TYPE_UINT32.

    void DrawIndexed(std::uint32_t indexCount, std::uint32_t firstIndex = 0, std::int32_t vertexOffset = 0) noexcept;
    void Draw(std::uint32_t vertexCount, std::uint32_t firstVertex = 0) noexcept;

    // --- Compute Recording ---

    // Stage push constants for the next dispatch.
    void SetPushConstants(const void* data, std::uint32_t size) noexcept;

    template <typename T>
    void SetPushConstants(const T& data) noexcept
    {
        SetPushConstants(&data, static_cast<std::uint32_t>(sizeof(T)));
    }

    // Dispatch using workgroup counts.
    void Dispatch(std::uint32_t groupX, std::uint32_t groupY = 1, std::uint32_t groupZ = 1);

    // Dispatch using total element size (automatically calculates group count).
    void DispatchOverSize(std::uint32_t width, std::uint32_t height, std::uint32_t depth = 1);

    // Convenience helper: binds pipeline, set, constants, and dispatches in one call.
    template <typename PushConstants>
    void DispatchCompute(const ComputePipeline& pipeline, DescriptorSetHandle set, const PushConstants& pushConstants,
        std::uint32_t groupX, std::uint32_t groupY = 1, std::uint32_t groupZ = 1)
    {
        BindComputePipeline(pipeline);
        BindDescriptorSet(set);
        SetPushConstants(pushConstants);
        Dispatch(groupX, groupY, groupZ);
    }

    // Submits a mesh draw. The descriptor sets are engine handles, not raw VkDescriptorSet.
    void Draw(const Pipeline& pipeline, const Mesh& mesh, const Mat4& modelMatrix = Mat4::Identity(),
        const Mat4& viewProjMatrix = Mat4::Identity(),
        DescriptorSetHandle materialDescriptorSet = DescriptorSetHandle::Invalid,
        DescriptorSetHandle sceneServicesSet = DescriptorSetHandle::Invalid);

private:
    std::function<void(bool, std::uint32_t, std::uint32_t)> MakeRecordDrawStatsCallback() const;

    VkCommandBuffer m_cmd = VK_NULL_HANDLE;
    Renderer* m_renderer = nullptr;
    DrawStats* m_passDrawStats = nullptr;
    WriteTargetInfo m_writeTarget;
    FrameDebuggerEventSink* m_eventSink = nullptr;

    const ComputePipeline* m_boundComputePipeline = nullptr;
    VkDescriptorSet m_boundDescriptorSet = VK_NULL_HANDLE;
    const void* m_pushConstantData = nullptr;
    std::uint32_t m_pushConstantSize = 0;

    VkPipelineLayout m_boundGraphicsLayout = VK_NULL_HANDLE;
};

} // namespace gte::rg
