#pragma once

// Renderer-internal bridge between the Vulkan-free recording types and a raw VkCommandBuffer.
// Nothing outside src/Renderer may include this header.

#include <volk.h>

#include "CommandBuffer.h"
#include "RecordingContext.h"

#include <cstdint>

namespace gte::rg {

struct RecordingAccess {
    static RecordingContext Wrap(
        VkCommandBuffer cmd, Renderer* renderer = nullptr, const PassContext* pass = nullptr) noexcept
    {
        return RecordingContext(cmd, renderer, pass);
    }

    static VkCommandBuffer Native(const RecordingContext& ctx) noexcept
    {
        return static_cast<VkCommandBuffer>(ctx.m_native);
    }
    static VkCommandBuffer Native(const CommandBuffer& cmd) noexcept { return cmd.m_cmd; }
    static VkCommandBuffer Native(const PassContext& ctx) noexcept; // Defined in RenderGraph.h.

    static CommandBuffer Adopt(VkCommandBuffer cmd, Renderer* renderer = nullptr, DrawStats* passDrawStats = nullptr,
        CommandBufferWriteTargetInfo writeTarget = {}, FrameDebuggerEventSink* eventSink = nullptr) noexcept
    {
        return CommandBuffer(cmd, renderer, passDrawStats, writeTarget, eventSink);
    }

    static NativeImageToken ImageToken(VkImage image) noexcept
    {
        return static_cast<NativeImageToken>(reinterpret_cast<std::uintptr_t>(image));
    }
    static VkImage Image(NativeImageToken token) noexcept
    {
        return reinterpret_cast<VkImage>(static_cast<std::uintptr_t>(token));
    }
};

} // namespace gte::rg
