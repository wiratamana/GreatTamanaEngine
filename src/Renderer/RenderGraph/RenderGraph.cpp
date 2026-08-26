#include "RenderGraph.h"

#include <cstring>

namespace gte::rg {

RenderGraph::RenderGraph(Renderer& renderer)
    : m_resourcePool(renderer)
{
}

void RenderGraph::EnsureTextureResolved(
    std::uint32_t index, const CompiledGraphInput& input, std::vector<PhysicalTexture>& physicalTextures)
{
    PhysicalTexture& tex = physicalTextures[index];
    if (tex.resolved) {
        return;
    }

    const TextureImportInfo& importInfo = input.textureImportInfo[index];
    if (importInfo.isImported) {
        // Already a real, externally-owned resource (the swapchain image,
        // or the Editor's own persistent Game/Scene RenderTexture) - never
        // allocated/freed by this graph. Seeded from the caller-supplied
        // `currentLayout` exactly as RenderGraphBuilder::ImportTexture()'s
        // own doc comment requires; stage/access are conservatively seeded
        // as TOP_OF_PIPE/NONE (the same simplification
        // FrameRecorder::RecordFrame() already makes for every resource it
        // transitions today - its own barriers always use
        // srcStageMask = TOP_OF_PIPE_BIT/srcAccessMask = NONE regardless of
        // a resource's real prior usage, relying on the fence/semaphore
        // sync that already orders frames elsewhere).
        tex.isImported = true;
        tex.target = importInfo.externalTarget;
        tex.sampler = VK_NULL_HANDLE; // TextureImportInfo carries no sampler of its own.
        tex.hasDepth = importInfo.externalTarget.depthImage != VK_NULL_HANDLE;
        tex.colorState =
            ResourceState{ importInfo.currentLayout, VK_PIPELINE_STAGE_2_TOP_OF_PIPE_BIT, VK_ACCESS_2_NONE };
        // Phase 2's ImportTexture() only records ONE currentLayout (the
        // color image's) - an imported resource's companion depth image
        // (if any) has no equivalent caller-supplied layout, so it is
        // conservatively seeded at the same synthetic "never touched
        // before" state a transient resource starts at. No real Phases
        // 1-8 pass imports a depth-carrying external target, so this is
        // untested territory in practice, documented here for whoever
        // first does.
        tex.depthState = ResourceState{};
    } else {
        RenderTexture& renderTexture =
            m_resourcePool.AcquireTexture(input.textureDescs[index], input.textureNames[index]);
        tex.isImported = false;
        tex.target = renderTexture.Target();
        tex.sampler = renderTexture.Sampler();
        tex.hasDepth = input.textureDescs[index].hasDepth;
        // A freshly-claimed pooled entry (whether brand-new or reused from
        // a previous frame) always starts this call's tracking at the
        // synthetic "never touched before" state - RenderGraphResourcePool
        // guarantees at most one virtual resource claims a given pool entry
        // per frame (see its own class comment), so there is no real
        // cross-frame state to inherit here the way an IMPORTED resource's
        // `currentLayout` carries one.
        tex.colorState = ResourceState{};
        tex.depthState = ResourceState{};
    }
    tex.resolved = true;
}

void RenderGraph::EnsureBufferResolved(
    std::uint32_t index, const CompiledGraphInput& input, std::vector<PhysicalBuffer>& physicalBuffers)
{
    PhysicalBuffer& buf = physicalBuffers[index];
    if (buf.resolved) {
        return;
    }

    // Phase 2 has no ImportBuffer() counterpart to ImportTexture() - every
    // declared BufferHandle is necessarily transient/pooled.
    Buffer& buffer = m_resourcePool.AcquireBuffer(input.bufferDescs[index], input.bufferNames[index]);
    buf.buffer = buffer.Native();
    buf.size = buffer.Size();
    buf.state = ResourceState{};
    buf.resolved = true;
}

void RenderGraph::ApplyUsageBarrierIfNeeded(VkCommandBuffer cmd, const ResourceUsage& usage,
    const CompiledGraphInput& input, std::vector<PhysicalTexture>& physicalTextures,
    std::vector<PhysicalBuffer>& physicalBuffers)
{
    if (usage.kind == ResourceKind::Texture) {
        EnsureTextureResolved(usage.texture.index, input, physicalTextures);
        PhysicalTexture& tex = physicalTextures[usage.texture.index];

        // Every ResourceAccess kind except DepthStencilAttachmentReadWrite
        // targets the COLOR image of this handle - MVP limitation: there is
        // no way today to declare "I want to ShaderRead the DEPTH half of a
        // texture that also has a color image" as a distinct usage (e.g. a
        // shadow map sampled by a later pass) - see
        // RENDERGRAPH_PHASE5_COMPLETION_REPORT.md's own MRT/attachment-count
        // scope notes for the sibling limitation this mirrors.
        const bool isDepthAccess = (usage.access == ResourceAccess::DepthStencilAttachmentReadWrite);
        ResourceState& state = isDepthAccess ? tex.depthState : tex.colorState;
        const ResourceState next = RequiredStateFor(usage.access, isDepthAccess);

        if (RequiresBarrier(state, next)) {
            const VkImage image = isDepthAccess ? tex.target.depthImage : tex.target.image;
            const VkImageAspectFlags aspect = isDepthAccess
                ? (VK_IMAGE_ASPECT_DEPTH_BIT | (tex.target.depthHasStencil ? VK_IMAGE_ASPECT_STENCIL_BIT : 0))
                : static_cast<VkImageAspectFlags>(VK_IMAGE_ASPECT_COLOR_BIT);
            const VkImageSubresourceRange range{ aspect, 0, 1, 0, 1 };
            EmitImageBarrier(cmd, image, range, state, next);
        }
        state = next;
    } else {
        EnsureBufferResolved(usage.buffer.index, input, physicalBuffers);
        PhysicalBuffer& buf = physicalBuffers[usage.buffer.index];
        const ResourceState next = RequiredStateFor(usage.access, false);
        if (RequiresBarrier(buf.state, next)) {
            EmitBufferBarrier(cmd, buf.buffer, 0, buf.size, buf.state, next);
        }
        buf.state = next;
    }
}

void RenderGraph::ExecuteCompiledGraph(VkCommandBuffer cmd, ExecuteTimingMode timingMode, CompiledGraphInput input,
    const std::vector<TextureHandle>& finalOutputs)
{
    // See this class's own header comment: the SynchronousImmediateReadback
    // call is, BY CONVENTION, the first of this frame's two Execute() calls
    // - it alone resets every pooled entry's "claimed this frame" flag, so
    // a resource claimed here stays correctly marked through the SECOND
    // (PipelinedDeferredReadback) call too.
    if (timingMode == ExecuteTimingMode::SynchronousImmediateReadback) {
        m_resourcePool.BeginFrame();
    }

    // Deliberately NOT wrapped in try/catch here - see this class's own
    // Execute() doc comment / RENDERGRAPH_PHASE6_EXECUTION_ENGINE_STRATEGY_v2.md's
    // Step 3.5 for why a dependency-cycle exception must propagate to the
    // caller, never be silently swallowed here.
    const CompiledGraph compiled = Compile(input, std::span<const TextureHandle>(finalOutputs));

    std::vector<PhysicalTexture> physicalTextures(input.textureDescs.size());
    std::vector<PhysicalBuffer> physicalBuffers(input.bufferDescs.size());

    RenderGraphNameSlotTable& timingSlots = (timingMode == ExecuteTimingMode::SynchronousImmediateReadback)
        ? m_synchronousTimingSlots
        : m_pipelinedTimingSlots;

    for (const PassHandle& passHandle : compiled.executionOrder) {
        PassRecord& pass = input.passes[passHandle.index];

        // Reads before writes - order between the two doesn't affect
        // correctness (each usage's own barrier is applied strictly before
        // this pass's `execute` callback runs either way), but reads-first
        // mirrors how a pass conceptually consumes its inputs before
        // producing its outputs.
        for (const ResourceUsage& usage : pass.reads) {
            ApplyUsageBarrierIfNeeded(cmd, usage, input, physicalTextures, physicalBuffers);
        }
        for (const ResourceUsage& usage : pass.writes) {
            ApplyUsageBarrierIfNeeded(cmd, usage, input, physicalTextures, physicalBuffers);
        }

        // Reserved for Phase 7's real per-pass timestamp query wiring (see
        // this class's own "GPU TIMING NOTE") - exercising the name-slot
        // assignment/reuse logic on every real Execute() call from day one,
        // even though nothing consumes the resulting slot index yet.
        (void)timingSlots.AssignOrGetSlot(pass.name);

        // MVP scope (RENDERGRAPH_PHASE5_BARRIER_SYNTHESIS_STRATEGY_v2.md,
        // carried into this phase): a SINGLE color attachment plus an
        // optional depth attachment per pass. A pass with no
        // ColorAttachmentWrite write (e.g. a future transfer-only/
        // compute-only pass) gets no vkCmdBeginRendering bracket at all -
        // its `execute` callback is invoked with a zero-extent
        // PassContext and is expected to record whatever non-rendering
        // Vulkan work it needs directly against `cmd`.
        bool hasColorWrite = false;
        TextureHandle colorHandle;
        bool hasDepthWrite = false;
        TextureHandle depthHandle;
        for (const ResourceUsage& usage : pass.writes) {
            if (usage.kind != ResourceKind::Texture) {
                continue;
            }
            if (usage.access == ResourceAccess::ColorAttachmentWrite) {
                colorHandle = usage.texture;
                hasColorWrite = true;
            } else if (usage.access == ResourceAccess::DepthStencilAttachmentReadWrite) {
                depthHandle = usage.texture;
                hasDepthWrite = true;
            }
        }

        PassContext ctx;
        ctx.cmd = cmd;
        ctx.resolveReadTexture = [&physicalTextures](TextureHandle handle) -> PassContext::ResolvedTexture {
            if (handle.index < physicalTextures.size() && physicalTextures[handle.index].resolved) {
                const PhysicalTexture& tex = physicalTextures[handle.index];
                return PassContext::ResolvedTexture{ tex.target.imageView, tex.sampler };
            }
            return PassContext::ResolvedTexture{};
        };

        DrawStats passDrawStats;
        ctx.recordDraw = [&passDrawStats](bool hasIndexBuffer, std::uint32_t vertexCount, std::uint32_t indexCount) {
            AccumulateDrawStats(passDrawStats, hasIndexBuffer, vertexCount, indexCount);
        };

        bool didBeginRendering = false;
        if (hasColorWrite) {
            const PhysicalTexture& colorTex = physicalTextures[colorHandle.index];

            // Phase 7 (RENDERGRAPH_PHASE7_APPLICATION_MIGRATION_STRATEGY_v2.md)
            // - loadOp is CLEAR whenever this pass declared a color clear
            // value (PassRecord::colorClearValue, set via
            // PassBuilder::WriteColorAttachment()'s own optional parameter -
            // see RenderGraphBuilder.h), LOAD otherwise (Phase 6's original,
            // only behavior - never silently discards another pass's, or a
            // previous frame's, contents a pass author didn't ask to lose).
            // storeOp = STORE (always): this graph has no way to know yet
            // whether a later pass/import consumer needs this attachment's
            // contents, so nothing is ever discarded speculatively.
            VkRenderingAttachmentInfo colorAttachment{};
            colorAttachment.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
            colorAttachment.imageView = colorTex.target.imageView;
            colorAttachment.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
            colorAttachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
            if (pass.colorClearValue.has_value()) {
                colorAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
                const std::array<float, 4>& c = *pass.colorClearValue;
                colorAttachment.clearValue.color = { { c[0], c[1], c[2], c[3] } };
            } else {
                colorAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_LOAD;
            }

            VkRenderingAttachmentInfo depthAttachment{};
            bool hasDepthAttachment = false;
            if (hasDepthWrite) {
                const PhysicalTexture& depthTex = physicalTextures[depthHandle.index];
                depthAttachment.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
                depthAttachment.imageView = depthTex.target.depthImageView;
                depthAttachment.imageLayout = depthTex.target.depthHasStencil
                    ? VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL
                    : VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL;
                depthAttachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
                if (pass.depthClearValue.has_value()) {
                    depthAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
                    depthAttachment.clearValue.depthStencil = { *pass.depthClearValue, 0 };
                } else {
                    depthAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_LOAD;
                }
                hasDepthAttachment = true;
            }

            VkRenderingInfo renderingInfo{};
            renderingInfo.sType = VK_STRUCTURE_TYPE_RENDERING_INFO;
            renderingInfo.renderArea = { { 0, 0 }, colorTex.target.extent };
            renderingInfo.layerCount = 1;
            renderingInfo.colorAttachmentCount = 1;
            renderingInfo.pColorAttachments = &colorAttachment;
            renderingInfo.pDepthAttachment = hasDepthAttachment ? &depthAttachment : nullptr;

            vkCmdBeginRendering(cmd, &renderingInfo);

            // Phase 5's own header comment on this file's future consumer:
            // viewport/scissor setup is RenderGraph's responsibility, sized
            // to this pass's own resolved color attachment - mirrors
            // FrameRecorder::RecordFrame()'s existing behavior exactly.
            VkViewport viewport{};
            viewport.x = 0.0f;
            viewport.y = 0.0f;
            viewport.width = static_cast<float>(colorTex.target.extent.width);
            viewport.height = static_cast<float>(colorTex.target.extent.height);
            viewport.minDepth = 0.0f;
            viewport.maxDepth = 1.0f;
            vkCmdSetViewport(cmd, 0, 1, &viewport);

            VkRect2D scissor{};
            scissor.offset = { 0, 0 };
            scissor.extent = colorTex.target.extent;
            vkCmdSetScissor(cmd, 0, 1, &scissor);

            ctx.colorAttachmentExtent = colorTex.target.extent;
            didBeginRendering = true;
        }

        if (pass.execute) {
            pass.execute(ctx);
        }

        if (didBeginRendering) {
            vkCmdEndRendering(cmd);
        }

        // Absent GpuTimingSample - see this class's own "GPU TIMING NOTE".
        RecordStatsFor(pass.name, PassGpuStats{ passDrawStats, GpuTimingSample{} });
    }
}

void RenderGraph::RecordStatsFor(const char* name, const PassGpuStats& stats)
{
    if (name == nullptr) {
        return;
    }
    for (NamedStats& entry : m_lastKnownStats) {
        if (entry.name == name || std::strcmp(entry.name, name) == 0) {
            entry.stats = stats;
            return;
        }
    }
    m_lastKnownStats.push_back(NamedStats{ name, stats });
}

PassGpuStats RenderGraph::LastKnownStatsFor(const char* passName) const
{
    if (passName != nullptr) {
        for (const NamedStats& entry : m_lastKnownStats) {
            if (entry.name == passName || std::strcmp(entry.name, passName) == 0) {
                return entry.stats;
            }
        }
    }
    return PassGpuStats{};
}

} // namespace gte::rg
