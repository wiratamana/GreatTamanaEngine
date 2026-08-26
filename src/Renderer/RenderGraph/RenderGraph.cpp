#include "RenderGraph.h"

#include "../Renderer.h"

#include <cstring>

namespace gte::rg {

namespace {

// B.1 (B1_REAL_GPU_TIMING_STRATEGY_v1.md) - the one place this class's
// constructor needs a Renderer::VulkanContextInfo, computed once and
// forwarded into every RenderGraphTimestampPool constructor argument that
// needs it, rather than calling Renderer::GetVulkanContextInfo() several
// times inline in the member-initializer list (harmless either way - it's
// a cheap, side-effect-free struct copy - but this reads more clearly).
Renderer::VulkanContextInfo QueryVulkanContextInfo(Renderer& renderer)
{
    return renderer.GetVulkanContextInfo();
}

} // namespace

RenderGraph::RenderGraph(Renderer& renderer)
    : m_resourcePool(renderer)
    , m_timestampPool(QueryVulkanContextInfo(renderer).device, QueryVulkanContextInfo(renderer).graphicsQueue,
          QueryVulkanContextInfo(renderer).graphicsQueueFamily, QueryVulkanContextInfo(renderer).timestampCapability,
          kSynchronousTimingSlotBudget, kPipelinedTimingSlotBudget, kGpuTimingFramesInFlight)
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
    const bool isPipelined = (timingMode == ExecuteTimingMode::PipelinedDeferredReadback);

    // See this class's own header comment: the SynchronousImmediateReadback
    // call is, BY CONVENTION, the first of this frame's two Execute() calls
    // - it alone resets every pooled entry's "claimed this frame" flag, so
    // a resource claimed here stays correctly marked through the SECOND
    // (PipelinedDeferredReadback) call too.
    if (!isPipelined) {
        m_resourcePool.BeginFrame();
    }

    // B.1 (B1_REAL_GPU_TIMING_STRATEGY_v1.md), Step 3.7 - pipelined-regime
    // GPU timing readback PREAMBLE: reads back whatever was written into
    // THIS bufferIndex kGpuTimingFramesInFlight frames ago. Provably safe
    // to do here, with no extra synchronization of its own, because
    // FramePresenter::PresentViaRenderGraph() already waited on this exact
    // frame-in-flight slot's fence BEFORE ever calling Execute() at all -
    // see RenderGraphTimestampPool::WriteBegin()'s own doc comment for the
    // full reasoning. `m_pipelinedFrameCounter` only ever advances once per
    // REAL Execute() call in this mode (incremented at the very bottom of
    // this function, in the `isPipelined` branch only), so it always stays
    // in lockstep with FramePresenter's own m_currentFrame cadence.
    std::uint32_t pipelinedBufferIndex = 0;
    if (isPipelined) {
        pipelinedBufferIndex = m_pipelinedFrameCounter % kGpuTimingFramesInFlight;
        for (std::uint32_t s = 0; s < m_pipelinedTimingSlots.AssignedCount(); ++s) {
            if (!m_pipelinedHasWritten[s][pipelinedBufferIndex]) {
                continue; // This exact slice has never been written yet - first kGpuTimingFramesInFlight frames, or capture was off.
            }
            const char* name = m_pipelinedTimingSlots.NameAtSlot(static_cast<std::int32_t>(s));
            if (name == nullptr) {
                continue;
            }
            const RenderGraphTimestampPool::RawTicks raw =
                m_timestampPool.ReadBack(/*pipelined=*/true, pipelinedBufferIndex, static_cast<std::int32_t>(s));
            UpdateTimingFor(name, ResolveAndConvertTiming(raw));
        }
    }

    // Deliberately NOT wrapped in try/catch here - see this class's own
    // Execute() doc comment / RENDERGRAPH_PHASE6_EXECUTION_ENGINE_STRATEGY_v2.md's
    // Step 3.5 for why a dependency-cycle exception must propagate to the
    // caller, never be silently swallowed here.
    const CompiledGraph compiled = Compile(input, std::span<const TextureHandle>(finalOutputs));

    std::vector<PhysicalTexture> physicalTextures(input.textureDescs.size());
    std::vector<PhysicalBuffer> physicalBuffers(input.bufferDescs.size());

    RenderGraphNameSlotTable& timingSlots = isPipelined ? m_pipelinedTimingSlots : m_synchronousTimingSlots;

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

        const std::int32_t timingSlot = timingSlots.AssignOrGetSlot(pass.name);

        // B.1 - the BEGIN timestamp is written AFTER this pass's own
        // barriers have already been recorded above, so any GPU stall
        // caused by waiting on THIS pass's own dependency transitions is
        // attributed to THIS pass, never misleadingly folded into whatever
        // pass happens to run immediately before it. A safe no-op (no
        // Vulkan call at all) whenever timingSlot == kNoNameSlot (this
        // regime's fixed slot budget is already fully assigned to other
        // pass names) or GPU timing is unsupported/capture-disabled - see
        // RenderGraphTimestampPool::WriteBegin()'s own doc comment.
        m_timestampPool.WriteBegin(cmd, isPipelined, pipelinedBufferIndex, timingSlot);

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

        // B.1 - the END timestamp, bracketing this pass's whole recorded
        // body (both the dynamic-rendering bracket, if any, AND
        // pass.execute() itself) - same guard as WriteBegin() above. For
        // the pipelined regime, immediately mark this exact
        // (slot, bufferIndex) slice as "genuinely written this call" so a
        // FUTURE call (kGpuTimingFramesInFlight frames from now) knows it's
        // safe to read back - mirrors GpuTimingService::
        // RecordPresentPassEnd()/MarkPresentSlotWritten()'s own pairing.
        m_timestampPool.WriteEnd(cmd, isPipelined, pipelinedBufferIndex, timingSlot);
        if (isPipelined && timingSlot != kNoNameSlot) {
            m_pipelinedHasWritten[static_cast<std::size_t>(timingSlot)][pipelinedBufferIndex] = true;
        }

        // B.1 - drawStats only; timing is populated separately (see
        // FinalizeSynchronousGpuTiming()/the pipelined preamble above) -
        // never let one clobber the other's already-correct data with a
        // stale default (see UpdateDrawStatsFor()'s own doc comment).
        UpdateDrawStatsFor(pass.name, passDrawStats);
    }

    if (isPipelined) {
        ++m_pipelinedFrameCounter;
    }

    // Phase 8 (RENDERGRAPH_PHASE8_EDITOR_DEBUG_TOOLING_STRATEGY_v1.md) - built
    // AFTER the whole pass loop above has run, so `statsLookup` (backed by
    // LastKnownStatsFor(), already updated by UpdateDrawStatsFor()/
    // UpdateTimingFor() above/inside that loop) sees this call's own
    // freshly-recorded stats for every surviving pass - see
    // BuildRenderGraphSnapshot()'s own doc comment (RenderGraphSnapshot.h)
    // for why a culled pass's stats are left at their default instead. Note
    // that for the SYNCHRONOUS regime, this snapshot's `timing` still
    // reflects whatever was known BEFORE this call's own
    // FinalizeSynchronousGpuTiming() runs (that happens after this
    // function returns, from Application::Run()) - i.e. one frame stale,
    // same one-frame-of-lag every other Editor Game/Scene-view-sized field
    // already tolerates (see ImGuiEditorLayer.cpp's own class comment).
    RenderGraphSnapshot snapshot = BuildRenderGraphSnapshot(
        compiled, input, [this](const char* name) { return LastKnownStatsFor(name); });
    if (!isPipelined) {
        m_synchronousSnapshot = std::move(snapshot);
    } else {
        m_pipelinedSnapshot = std::move(snapshot);
    }
}

void RenderGraph::FinalizeSynchronousGpuTiming()
{
    for (std::uint32_t s = 0; s < m_synchronousTimingSlots.AssignedCount(); ++s) {
        const char* name = m_synchronousTimingSlots.NameAtSlot(static_cast<std::int32_t>(s));
        if (name == nullptr) {
            continue;
        }
        const RenderGraphTimestampPool::RawTicks raw =
            m_timestampPool.ReadBack(/*pipelined=*/false, /*bufferIndex=*/0, static_cast<std::int32_t>(s));
        UpdateTimingFor(name, ResolveAndConvertTiming(raw));
    }
}

GpuTimingSample RenderGraph::ResolveAndConvertTiming(const RenderGraphTimestampPool::RawTicks& raw) const
{
    const GpuTimingSample::Status status =
        ResolveGpuTimingStatus(m_timestampPool.IsSupported(), m_timestampPool.IsCaptureEnabled(), /*hasWrittenData=*/true);
    if (status != GpuTimingSample::Status::Present) {
        return GpuTimingSample{ status, 0.0 };
    }
    const GpuTimestampCapability& capability = m_timestampPool.Capability();
    const double milliseconds =
        ConvertTimestampDeltaToMilliseconds(raw.begin, raw.end, capability.timestampPeriodNs, capability.validBits);
    return GpuTimingSample{ GpuTimingSample::Status::Present, milliseconds };
}

void RenderGraph::UpdateDrawStatsFor(const char* name, const DrawStats& drawStats)
{
    if (name == nullptr) {
        return;
    }
    for (NamedStats& entry : m_lastKnownStats) {
        if (entry.name == name || std::strcmp(entry.name, name) == 0) {
            entry.stats.drawStats = drawStats;
            return;
        }
    }
    PassGpuStats stats;
    stats.drawStats = drawStats;
    m_lastKnownStats.push_back(NamedStats{ name, stats });
}

void RenderGraph::UpdateTimingFor(const char* name, const GpuTimingSample& timing)
{
    if (name == nullptr) {
        return;
    }
    for (NamedStats& entry : m_lastKnownStats) {
        if (entry.name == name || std::strcmp(entry.name, name) == 0) {
            entry.stats.timing = timing;
            return;
        }
    }
    PassGpuStats stats;
    stats.timing = timing;
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

const RenderGraphSnapshot& RenderGraph::LastSnapshot(ExecuteTimingMode mode) const noexcept
{
    return (mode == ExecuteTimingMode::SynchronousImmediateReadback) ? m_synchronousSnapshot : m_pipelinedSnapshot;
}

} // namespace gte::rg
