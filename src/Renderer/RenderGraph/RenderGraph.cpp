#include "RenderGraph.h"

#include "../Renderer.h"
#include "../../Editor/Logger.h" // PHASE1 (render-pass-6 campaign, item 2.4) - GTE_LOG_WARNING for slot-budget overflow.

#include <cassert>
#include <cstring>
#include <stdexcept>

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

    const TextureImportInfo& importInfo = input.textures[index].importInfo;
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
            m_resourcePool.AcquireTexture(input.textures[index].desc, input.textures[index].name);
        tex.isImported = false;
        tex.target = renderTexture.Target();
        tex.sampler = renderTexture.Sampler();
        tex.hasDepth = input.textures[index].desc.hasDepth;
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

    // GPU Vertex Skinning campaign, Phase 3
    // (GPU_SKINNING_PHASE3_RENDERGRAPH_SYNCHRONIZATION_STRATEGY_v2.md) -
    // closes the gap this comment used to describe: RenderGraphBuilder now
    // has a real ImportBuffer() counterpart to ImportTexture() (see
    // RenderGraphBuilder.h) - a declared BufferHandle may be either
    // transient/pooled (the original, only behavior) OR an already-live,
    // externally-owned buffer (e.g. a future per-model GPU skinning output
    // buffer).
    const BufferImportInfo& importInfo = input.buffers[index].importInfo;
    if (importInfo.isImported) {
        // Already a real, externally-owned resource - never allocated/
        // freed by this graph, mirroring EnsureTextureResolved()'s own
        // import branch above. Always seeded at a fresh, "never touched
        // before" ResourceState{} - a buffer has no image-layout concept
        // requiring a true cross-frame carry the way an imported TEXTURE's
        // currentLayout does; this engine's existing whole-frame fence/
        // semaphore synchronization is what makes this safe (see
        // RenderGraphBuilder::ImportBuffer()'s own comment).
        buf.buffer = importInfo.externalBuffer;
        buf.size = importInfo.size;
        buf.state = ResourceState{};
    } else {
        Buffer& buffer = m_resourcePool.AcquireBuffer(input.buffers[index].desc, input.buffers[index].name);
        buf.buffer = buffer.Native();
        buf.size = buffer.Size();
        buf.state = ResourceState{};
    }
    buf.resolved = true;
}

// Atmosphere Scattering campaign, Phase 2
// (ATMOSPHERE_PHASE2_VOLUME_TEXTURE_RENDERGRAPH_SUPPORT_v1.md). Every
// VolumeTextureHandle today is imported exclusively via
// RenderGraphBuilder::ImportVolumeTexture() - there is deliberately no
// CreateVolumeTexture() (pooled/transient) counterpart yet, so
// `importInfo.isImported` is always true in practice; the `else` branch
// below is defensive-only, mirroring a transient resource's own "never
// touched before" seed for when that gap is eventually closed.
void RenderGraph::EnsureVolumeTextureResolved(std::uint32_t index, const CompiledGraphInput& input,
    std::vector<PhysicalVolumeTexture>& physicalVolumeTextures)
{
    PhysicalVolumeTexture& vol = physicalVolumeTextures[index];
    if (vol.resolved) {
        return;
    }

    const VolumeTextureImportInfo& importInfo = input.volumeTextures[index].importInfo;
    if (importInfo.isImported) {
        vol.isImported = true;
        vol.target = importInfo.externalTarget;
        vol.state = ResourceState{ importInfo.currentLayout, VK_PIPELINE_STAGE_2_TOP_OF_PIPE_BIT, VK_ACCESS_2_NONE };
    } else {
        vol.isImported = false;
        vol.target = VolumeTarget{};
        vol.state = ResourceState{};
    }
    vol.resolved = true;
}

void RenderGraph::ApplyUsageBarrierIfNeeded(VkCommandBuffer cmd, const ResourceUsage& usage,
    const CompiledGraphInput& input, std::vector<PhysicalTexture>& physicalTextures,
    std::vector<PhysicalBuffer>& physicalBuffers, std::vector<PhysicalVolumeTexture>& physicalVolumeTextures)
{
    // Atmosphere Scattering campaign, Phase 2 precheck
    // (ATMOSPHERE_PHASE2_VOLUME_TEXTURE_RENDERGRAPH_SUPPORT_v1.md, Step
    // 2/3.2): this used to be a plain `if (usage.kind == ResourceKind::Texture)
    // {...} else {...}` two-way branch that would have silently routed a
    // VolumeTexture usage into the Buffer path - `EnsureBufferResolved()`
    // has NO bounds check on its own `index` parameter, so that would have
    // been a real, reachable out-of-bounds `physicalBuffers` access the
    // instant a volume-texture usage was ever declared. Converted to a
    // real, exhaustive, `default:`-less three-way `switch (usage.kind)`
    // BEFORE ResourceKind::VolumeTexture was ever added to the enum, and
    // then (render-pass-6 campaign, PHASE6, item 2.2) to a `void`-returning
    // DispatchByKind() call (RenderGraphTypes.h) - the three branches
    // genuinely cannot be unified into one same-return-type dispatch
    // (Texture legitimately does meaningfully more work than Buffer/
    // VolumeTexture), so `void` is what every lambda returns; each case's
    // existing body was moved verbatim into its own lambda, using the
    // lambda's own handle parameter instead of usage.texture/usage.buffer/
    // usage.volumeTexture.
    DispatchByKind(usage,
        [&](TextureHandle textureHandle) {
            EnsureTextureResolved(textureHandle.index, input, physicalTextures);
            PhysicalTexture& tex = physicalTextures[textureHandle.index];

            // Every ResourceAccess kind except DepthStencilAttachmentReadWrite
            // targets the COLOR image of this handle by default - Atmosphere
            // Scattering + Aerial Perspective campaign, Phase 7
            // (ATMOSPHERE_PHASE7_SKY_BACKGROUND_AND_COMPOSITE_PASSES_v1.md)
            // closes the MVP limitation this comment used to describe outright
            // ("there is no way today to declare... a distinct usage") by ALSO
            // consulting the usage's own explicit `isDepthResource` flag
            // (RenderGraphTypes.h's ResourceUsage/PassBuilder::ReadTexture()) -
            // this is what lets the Aerial Perspective Composite pass declare a
            // ShaderRead against the DEPTH half of the Game/Scene View's own
            // already-imported TextureHandle (which also carries a color
            // image), without needing a second, separately-imported handle for
            // the same physical depth image. TargetsDepthState() itself
            // (RenderGraphBarrierPlanner.h) is UNCHANGED by this addition - it
            // still only ever returns true for DepthStencilAttachmentReadWrite,
            // exactly as Phase 5 of the compute-shader campaign
            // (COMPUTE_PHASE5_SYNCHRONIZATION_STRATEGY_v2.md) confirmed for a
            // storage-image compute access (ComputeShaderRead/
            // ComputeShaderWrite), which is still correctly routed to the color
            // half either way (a compute access can never set isDepthResource
            // true today - no call site does).
            const bool isDepthAccess = TargetsDepthState(usage.access) || usage.isDepthResource;
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
        },
        [&](BufferHandle bufferHandle) {
            EnsureBufferResolved(bufferHandle.index, input, physicalBuffers);
            PhysicalBuffer& buf = physicalBuffers[bufferHandle.index];
            const ResourceState next = RequiredStateFor(usage.access, false);
            if (RequiresBarrier(buf.state, next)) {
                EmitBufferBarrier(cmd, buf.buffer, 0, buf.size, buf.state, next);
            }
            buf.state = next;
        },
        [&](VolumeTextureHandle volumeHandle) {
            // Atmosphere Scattering campaign, Phase 2
            // (ATMOSPHERE_PHASE2_VOLUME_TEXTURE_RENDERGRAPH_SUPPORT_v1.md) - a
            // volume texture has no depth-companion concept at all (unlike a
            // 2D PhysicalTexture's colorState/depthState split), so this is a
            // single ResourceState, always targeting the COLOR aspect of the
            // image (VK_IMAGE_ASPECT_COLOR_BIT - the only aspect a color
            // VK_FORMAT_R16G16B16A16_SFLOAT-style volume image ever has).
            EnsureVolumeTextureResolved(volumeHandle.index, input, physicalVolumeTextures);
            PhysicalVolumeTexture& vol = physicalVolumeTextures[volumeHandle.index];
            const ResourceState next = RequiredStateFor(usage.access, false);
            if (RequiresBarrier(vol.state, next)) {
                const VkImageSubresourceRange range{ VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 };
                EmitImageBarrier(cmd, vol.target.image, range, vol.state, next);
            }
            vol.state = next;
        });
}

// render-pass-6 campaign, PHASE2 (item 2.6), updated by PHASE3 (item 2.7) -
// extracted out of ExecuteCompiledGraph() for readability (PHASE2), then
// simplified from six freshly-constructed std::function closures down to a
// small, obviously-correct handful of pointer assignments (PHASE3) - see
// PHASE2_EXECUTE_COMPILED_GRAPH_EXTRACTION.md /
// PHASE3_PASSCONTEXT_PLAIN_RESOLVERS.md. `physicalTextures`/`physicalBuffers`/
// `physicalVolumeTextures`/`passDrawStats` all outlive the entire per-pass
// loop iteration that hands the returned PassContext to `pass.execute()` -
// see PassContext's own doc comment (RenderGraph.h) for the full pointer-
// lifetime reasoning.
PassContext RenderGraph::BuildPassContext(VkCommandBuffer cmd, std::vector<PhysicalTexture>& physicalTextures,
    std::vector<PhysicalBuffer>& physicalBuffers, std::vector<PhysicalVolumeTexture>& physicalVolumeTextures,
    DrawStats& passDrawStats)
{
    PassContext ctx;
    ctx.cmd = cmd;
    ctx.textures = &physicalTextures;
    ctx.buffers = &physicalBuffers;
    ctx.volumeTextures = &physicalVolumeTextures;
    ctx.recordDraw.drawStats = &passDrawStats;
    ctx.recordIndirectDraw.drawStats = &passDrawStats;
    return ctx;
}

// render-pass-6 campaign, PHASE3 (item 2.7) - PassContext's own resolve/
// record member-function bodies, mirroring the exact logic the six
// std::function closures above used to construct (see this campaign's
// PHASE2/PHASE3 strategy documents for the full history).
PassContext::ResolvedTexture PassContext::resolveReadTexture(TextureHandle handle) const noexcept
{
    if (textures != nullptr && handle.index < textures->size() && (*textures)[handle.index].resolved) {
        const RenderGraph::PhysicalTexture& tex = (*textures)[handle.index];
        return ResolvedTexture{ tex.target.imageView, tex.sampler };
    }
    return ResolvedTexture{};
}

VkBuffer PassContext::resolveBuffer(BufferHandle handle) const noexcept
{
    if (buffers != nullptr && handle.index < buffers->size() && (*buffers)[handle.index].resolved) {
        return (*buffers)[handle.index].buffer;
    }
    return VK_NULL_HANDLE;
}

PassContext::ResolvedVolumeTexture PassContext::resolveVolumeTexture(VolumeTextureHandle handle) const noexcept
{
    if (volumeTextures != nullptr && handle.index < volumeTextures->size()
        && (*volumeTextures)[handle.index].resolved) {
        return ResolvedVolumeTexture{ (*volumeTextures)[handle.index].target.imageView };
    }
    return ResolvedVolumeTexture{};
}

void PassContext::RecordDrawFn::operator()(
    bool hasIndexBuffer, std::uint32_t vertexCount, std::uint32_t indexCount) const
{
    if (drawStats != nullptr) {
        AccumulateDrawStats(*drawStats, hasIndexBuffer, vertexCount, indexCount);
    }
}

void PassContext::RecordIndirectDrawFn::operator()() const
{
    if (drawStats != nullptr) {
        AccumulateIndirectDrawStats(*drawStats);
    }
}

// render-pass-6 campaign, PHASE2 (item 2.6) - extracted out of
// ExecuteCompiledGraph() for readability, zero behavior change - see
// PHASE2_EXECUTE_COMPILED_GRAPH_EXTRACTION.md.
std::vector<VkRenderingAttachmentInfo> RenderGraph::BuildColorAttachmentInfos(
    const PassRecord& pass, const std::vector<PhysicalTexture>& physicalTextures,
    std::vector<VkExtent2D>& outResolvedExtents) const
{
    // Multi-Render-Target (MRT) campaign (task_manager/mrt-1),
    // PHASE2 - one VkRenderingAttachmentInfo PER declared color
    // attachment, built in the EXACT order pass.colorAttachments
    // holds them (== shader layout(location = N) out). A plain
    // std::vector, sized once per pass, is consistent with this
    // function's own existing allocation profile (e.g.
    // physicalTextures/physicalBuffers above) - no fixed-capacity/
    // small_vector convention exists elsewhere in this codebase to
    // prefer instead.
    std::vector<VkRenderingAttachmentInfo> colorAttachmentInfos;
    colorAttachmentInfos.reserve(pass.colorAttachments.size());
    outResolvedExtents.reserve(pass.colorAttachments.size());

    for (const ColorAttachmentDesc& desc : pass.colorAttachments) {
        // This handle was ALREADY resolved above, by the exact same
        // generic `for (const ResourceUsage& usage : pass.writes)`
        // barrier loop every pass already goes through, completely
        // unchanged by this phase - WriteColorAttachment() (PHASE1)
        // always pushes a matching ColorAttachmentWrite usage onto
        // pass.writes in lockstep with pass.colorAttachments,
        // specifically so this holds (see this file's own Step 2
        // analysis in the PHASE2 strategy document). Asserted here
        // defensively (cheap, debug-only) so a future regression
        // that ever breaks that lockstep invariant fails LOUDLY,
        // right here, instead of silently building a
        // VkRenderingAttachmentInfo around a VK_NULL_HANDLE
        // imageView that would otherwise only surface as a
        // confusing validation-layer error deep inside
        // vkCmdBeginRendering.
        assert(desc.handle.index < physicalTextures.size() &&
            physicalTextures[desc.handle.index].resolved &&
            "RenderGraph::ExecuteCompiledGraph: a pass.colorAttachments entry was never "
            "resolved - WriteColorAttachment() must always also push a matching "
            "ColorAttachmentWrite onto pass.writes (see PHASE1)");
        const PhysicalTexture& colorTex = physicalTextures[desc.handle.index];
        outResolvedExtents.push_back(colorTex.target.extent);

        // Phase 7 (RENDERGRAPH_PHASE7_APPLICATION_MIGRATION_STRATEGY_v2.md)
        // - loadOp is CLEAR whenever THIS attachment declared its
        // own clear color (ColorAttachmentDesc::clearColor, set via
        // PassBuilder::WriteColorAttachment()'s own optional
        // parameter - see RenderGraphBuilder.h), LOAD otherwise
        // (Phase 6's original, only behavior - never silently
        // discards another pass's, or a previous frame's, contents
        // a pass author didn't ask to lose). storeOp = STORE
        // (always): this graph has no way to know yet whether a
        // later pass/import consumer needs this attachment's
        // contents, so nothing is ever discarded speculatively.
        VkRenderingAttachmentInfo colorAttachment{};
        colorAttachment.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
        colorAttachment.imageView = colorTex.target.imageView;
        colorAttachment.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
        colorAttachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
        if (desc.clearColor.has_value()) {
            colorAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
            const std::array<float, 4>& c = *desc.clearColor;
            colorAttachment.clearValue.color = { { c[0], c[1], c[2], c[3] } };
        } else {
            colorAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_LOAD;
        }
        colorAttachmentInfos.push_back(colorAttachment);
    }
    return colorAttachmentInfos;
}

// render-pass-6 campaign, PHASE2 (item 2.6) - extracted out of
// ExecuteCompiledGraph() for readability, zero behavior change - see
// PHASE2_EXECUTE_COMPILED_GRAPH_EXTRACTION.md. `depthHandle` alone signals
// "no depth write this call" via its own IsValid() (see this method's
// declaration in RenderGraph.h for the full reasoning) - identical logic/
// identical produced VkRenderingAttachmentInfo fields to what used to be
// written inline.
std::optional<VkRenderingAttachmentInfo> RenderGraph::BuildDepthAttachmentInfo(
    const PassRecord& pass, const std::vector<PhysicalTexture>& physicalTextures, TextureHandle depthHandle) const
{
    if (!depthHandle.IsValid()) {
        return std::nullopt;
    }

    const PhysicalTexture& depthTex = physicalTextures[depthHandle.index];
    VkRenderingAttachmentInfo depthAttachment{};
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
    return depthAttachment;
}

// render-pass-6 campaign, PHASE2 (item 2.6) - extracted verbatim out of
// ExecuteCompiledGraph()'s own tail, zero behavior change - see
// PHASE2_EXECUTE_COMPILED_GRAPH_EXTRACTION.md.
//
// network-impl-4 campaign, Phase 2 - passive registration: every texture
// this call actually resolved becomes (or stays) queryable by name via
// DebugTextureSnapshotFor()/ListDebugTextures(), regardless of which
// ExecuteTimingMode this call was. An unresolved index this call is
// skipped, deliberately leaving any PREVIOUS entry for that name
// untouched - see PHASE2's own Step 2 analysis for why.
void RenderGraph::RegisterDebugTextureSnapshots(ExecuteTimingMode timingMode, const CompiledGraphInput& input,
    const std::vector<PhysicalTexture>& physicalTextures)
{
    for (std::size_t i = 0; i < physicalTextures.size(); ++i) {
        const PhysicalTexture& tex = physicalTextures[i];
        if (!tex.resolved) {
            continue;
        }
        const char* name = input.textures[i].name;
        if (name == nullptr || name[0] == '\0') {
            continue; // Defensive - every real call site always supplies a real name (RenderGraphBuilder::CreateTexture()/ImportTexture() both assert a non-null/non-empty name), but never trust that blindly here (an assert compiles out entirely in a release/NDEBUG build).
        }

        DebugTextureSnapshot snapshot;
        snapshot.name = name;
        snapshot.regime = timingMode; // ExecuteCompiledGraph()'s own parameter - confirmed live, exact spelling.
        snapshot.target = tex.target;
        snapshot.hasDepth = tex.hasDepth;
        snapshot.colorState = tex.colorState;
        snapshot.depthState = tex.depthState;
        snapshot.lastUpdatedFrameCounter = m_debugTextureFrameCounter;
        m_debugTextures.Upsert(snapshot);
    }
}

// render-pass-6 campaign, PHASE2 (item 2.6) - extracted verbatim out of
// ExecuteCompiledGraph()'s own tail, zero behavior change - see
// PHASE2_EXECUTE_COMPILED_GRAPH_EXTRACTION.md.
//
// network-impl-6 campaign, Phase 2
// (task_manager/network-impl-6/PHASE2_RENDERGRAPH_VOLUME_AUTO_REGISTRATION.md) -
// the volume-texture counterpart of RegisterDebugTextureSnapshots() above,
// sharing the exact same m_debugTextureFrameCounter stamp (see
// RenderGraph.h's own CurrentDebugTextureFrameCounter() doc comment for why
// there is deliberately no separate volume-only counter). input.volumeTextures[i].name
// and physicalVolumeTextures are both sized from
// input.volumeTextures.size() (see RenderGraphBuilder::
// ImportVolumeTexture(), which always pushes onto both in lockstep), so
// indexing them together by `i` is safe by construction, exactly like
// the 2D pair above.
void RenderGraph::RegisterDebugVolumeTextureSnapshots(ExecuteTimingMode timingMode, const CompiledGraphInput& input,
    const std::vector<PhysicalVolumeTexture>& physicalVolumeTextures)
{
    for (std::size_t i = 0; i < physicalVolumeTextures.size(); ++i) {
        const PhysicalVolumeTexture& vol = physicalVolumeTextures[i];
        if (!vol.resolved) {
            continue;
        }
        const char* name = input.volumeTextures[i].name;
        if (name == nullptr || name[0] == '\0') {
            continue; // Defensive - RenderGraphBuilder::ImportVolumeTexture() already asserts a non-null/non-empty name, but an assert compiles out entirely in a release/NDEBUG build.
        }

        DebugVolumeTextureSnapshot snapshot;
        snapshot.name = name;
        snapshot.regime = timingMode;
        snapshot.target = vol.target;
        snapshot.state = vol.state;
        snapshot.lastUpdatedFrameCounter = m_debugTextureFrameCounter;
        m_debugVolumeTextures.Upsert(snapshot);
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
        ++m_debugTextureFrameCounter; // network-impl-4, Phase 2 - see RenderGraph.h's own doc comment on this member.
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

    std::vector<PhysicalTexture> physicalTextures(input.textures.size());
    std::vector<PhysicalBuffer> physicalBuffers(input.buffers.size());
    // Atmosphere Scattering campaign, Phase 2.
    std::vector<PhysicalVolumeTexture> physicalVolumeTextures(input.volumeTextures.size());

    RenderGraphNameSlotTable& timingSlots = isPipelined ? m_pipelinedTimingSlots : m_synchronousTimingSlots;

    for (const PassHandle& passHandle : compiled.executionOrder) {
        PassRecord& pass = input.passes[passHandle.index];

        // Reads before writes - order between the two doesn't affect
        // correctness (each usage's own barrier is applied strictly before
        // this pass's `execute` callback runs either way), but reads-first
        // mirrors how a pass conceptually consumes its inputs before
        // producing its outputs.
        for (const ResourceUsage& usage : pass.reads) {
            ApplyUsageBarrierIfNeeded(cmd, usage, input, physicalTextures, physicalBuffers, physicalVolumeTextures);
        }
        for (const ResourceUsage& usage : pass.writes) {
            ApplyUsageBarrierIfNeeded(cmd, usage, input, physicalTextures, physicalBuffers, physicalVolumeTextures);
        }

        const std::int32_t timingSlot = timingSlots.AssignOrGetSlot(pass.name);

        // PHASE1 (render-pass-6 campaign, item 2.4) - the FIRST time a name
        // is denied a slot because this regime's fixed timing-slot budget is
        // already fully assigned to other names, report it exactly once
        // (never once per frame forever) via the engine's own logging
        // facility - see RenderGraphNameSlotTable::JustOverflowed()'s own
        // doc comment for why this is distinguishable from every other
        // kNoNameSlot-returning case.
        if (timingSlot == kNoNameSlot && timingSlots.JustOverflowed()) {
            std::vector<const char*>& reported =
                isPipelined ? m_reportedPipelinedOverflows : m_reportedSynchronousOverflows;
            bool alreadyReported = false;
            for (const char* n : reported) {
                if (n == pass.name || (pass.name != nullptr && n != nullptr && std::strcmp(n, pass.name) == 0)) {
                    alreadyReported = true;
                    break;
                }
            }
            if (!alreadyReported) {
                reported.push_back(pass.name);
                GTE_LOG_WARNING("RenderGraph",
                    "Pass \"" + std::string(pass.name != nullptr ? pass.name : "<unnamed>")
                        + "\" could not be assigned a GPU-timing slot - the "
                        + std::string(isPipelined ? "pipelined" : "synchronous")
                        + " regime's fixed timing-slot budget (" + std::to_string(timingSlots.SlotBudget())
                        + ") is already fully assigned to other pass names. This pass's GPU timing will read as "
                          "Absent until this budget is increased.");
            }
        }

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

        // Multi-Render-Target (MRT) campaign (task_manager/mrt-1), PHASE2 -
        // replaces the old "scan pass.writes, keep only the LAST
        // ColorAttachmentWrite" logic with a direct, ordered read of
        // pass.colorAttachments (PHASE1) - attachment index in that vector is
        // the shader layout(location = N) contract (see RenderGraphTypes.h's
        // own ColorAttachmentDesc doc comment). Depth is UNCHANGED - still
        // found via the exact same pass.writes scan as before (a pass has at
        // most one depth/stencil attachment - out of scope for this
        // campaign, see task_manager/mrt-1/PHASE0_MASTER_STRATEGY.md). A
        // pass with no color attachments at all (e.g. a transfer-only/
        // compute-only pass) gets no vkCmdBeginRendering bracket at all -
        // its `execute` callback is invoked with a zero-extent PassContext
        // and is expected to record whatever non-rendering Vulkan work it
        // needs directly against `cmd`.
        // render-pass-6 campaign, PHASE2 (item 2.6) - `hasDepthWrite` (the
        // original standalone bool this loop used to also maintain) was
        // dropped: depthHandle.IsValid() is exactly that same signal now
        // that BuildDepthAttachmentInfo() derives "was a depth write found"
        // purely from depthHandle itself (see that method's own doc
        // comment) - never a separate bool parameter.
        TextureHandle depthHandle;
        for (const ResourceUsage& usage : pass.writes) {
            if (usage.kind != ResourceKind::Texture) {
                continue;
            }
            if (TargetsDepthState(usage.access)) {
                depthHandle = usage.texture;
            }
        }
        // This equivalence (hasColorWrite == !pass.colorAttachments.empty())
        // holds because WriteColorAttachment() (PHASE1) is the ONLY call
        // site anywhere in this codebase that ever constructs a
        // ColorAttachmentWrite usage, and it ALWAYS pushes onto both
        // pass.writes AND pass.colorAttachments in lockstep - see this
        // campaign's own PHASE2 strategy document, Step 2, for the full
        // "load-bearing fact" analysis.
        const bool hasColorWrite = !pass.colorAttachments.empty();

        // render-pass-6 campaign, PHASE2 (item 2.6) - BuildPassContext()
        // extracted below (see this class's own header comment on that
        // method for the full reasoning). `passDrawStats` must live in
        // THIS function's own scope (not inside BuildPassContext() itself)
        // since UpdateDrawStatsFor(pass.name, passDrawStats) below still
        // needs to read it after pass.execute(ctx) returns.
        DrawStats passDrawStats;
        PassContext ctx = BuildPassContext(cmd, physicalTextures, physicalBuffers, physicalVolumeTextures, passDrawStats);

        bool didBeginRendering = false;
        if (hasColorWrite) {
            // render-pass-6 campaign, PHASE2 (item 2.6) - BuildColorAttachmentInfos()
            // extracted below; this function still owns the
            // FindMismatchedColorAttachmentExtent() check, the
            // vkCmdBeginRendering/vkCmdSetViewport/vkCmdSetScissor calls, and the
            // ctx.colorAttachmentExtent assignment - see
            // PHASE2_EXECUTE_COMPILED_GRAPH_EXTRACTION.md, Step 3.2.
            std::vector<VkExtent2D> resolvedExtents;
            const std::vector<VkRenderingAttachmentInfo> colorAttachmentInfos =
                BuildColorAttachmentInfos(pass, physicalTextures, resolvedExtents);

            // LOCKED (see task_manager/mrt-1/PHASE2_EXECUTE_LAYER_MRT_RECORDING.md's
            // own Step 3.2 "Decision 2"): a real, unconditional throw - never
            // a plain assert() a release/NDEBUG build would silently compile
            // away. Built on the pure, Tier-1-tested decision function above
            // (RenderGraphTypes.h/.cpp), so this exact check has real,
            // VkDevice-free test coverage (RenderGraphTypesTests.cpp).
            if (const std::optional<std::size_t> mismatchIndex =
                    FindMismatchedColorAttachmentExtent(resolvedExtents)) {
                const VkExtent2D& first = resolvedExtents[0];
                const VkExtent2D& bad = resolvedExtents[*mismatchIndex];
                throw std::runtime_error(
                    "RenderGraph::ExecuteCompiledGraph: pass \"" +
                    std::string(pass.name != nullptr ? pass.name : "<unnamed>") +
                    "\" declared color attachments with mismatched extents - attachment 0 is " +
                    std::to_string(first.width) + "x" + std::to_string(first.height) + ", attachment " +
                    std::to_string(*mismatchIndex) + " is " + std::to_string(bad.width) + "x" +
                    std::to_string(bad.height) +
                    " - every color attachment on one pass must share the same extent (G-buffer-style "
                    "targets are always rendered at the same resolution).");
            }
            const VkExtent2D firstExtent = resolvedExtents[0];

            const std::optional<VkRenderingAttachmentInfo> depthAttachmentInfo =
                BuildDepthAttachmentInfo(pass, physicalTextures, depthHandle);

            VkRenderingInfo renderingInfo{};
            renderingInfo.sType = VK_STRUCTURE_TYPE_RENDERING_INFO;
            renderingInfo.renderArea = { { 0, 0 }, firstExtent };
            renderingInfo.layerCount = 1;
            renderingInfo.colorAttachmentCount = static_cast<std::uint32_t>(colorAttachmentInfos.size());
            renderingInfo.pColorAttachments = colorAttachmentInfos.data();
            renderingInfo.pDepthAttachment = depthAttachmentInfo.has_value() ? &depthAttachmentInfo.value() : nullptr;

            vkCmdBeginRendering(cmd, &renderingInfo);

            // Phase 5's own header comment on this file's future consumer:
            // viewport/scissor setup is RenderGraph's responsibility, sized
            // to this pass's own resolved color attachment(s) - mirrors
            // FrameRecorder::RecordFrame()'s existing behavior exactly.
            // Multi-Render-Target (MRT) campaign, PHASE2 - sized from
            // attachment 0 (firstExtent), which every OTHER declared color
            // attachment on this pass is now guaranteed (by the throw above)
            // to share exactly - identical to today's single-attachment
            // behavior for every pre-existing pass in the engine.
            VkViewport viewport{};
            viewport.x = 0.0f;
            viewport.y = 0.0f;
            viewport.width = static_cast<float>(firstExtent.width);
            viewport.height = static_cast<float>(firstExtent.height);
            viewport.minDepth = 0.0f;
            viewport.maxDepth = 1.0f;
            vkCmdSetViewport(cmd, 0, 1, &viewport);

            VkRect2D scissor{};
            scissor.offset = { 0, 0 };
            scissor.extent = firstExtent;
            vkCmdSetScissor(cmd, 0, 1, &scissor);

            ctx.colorAttachmentExtent = firstExtent;
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

    // render-pass-6 campaign, PHASE2 (item 2.6) - the two passive-
    // registration loops that used to run inline here, extracted below -
    // see PHASE2_EXECUTE_COMPILED_GRAPH_EXTRACTION.md.
    RegisterDebugTextureSnapshots(timingMode, input, physicalTextures);
    RegisterDebugVolumeTextureSnapshots(timingMode, input, physicalVolumeTextures);

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
    // PHASE1 (render-pass-6 campaign, item 2.4) - "is this table currently at
    // 100% capacity" at snapshot-build time is exactly the persistent
    // condition RenderGraphSnapshot::timingSlotBudgetExhausted is meant to
    // describe - simpler and equally correct than plumbing JustOverflowed()'s
    // one-shot state through.
    const bool timingSlotBudgetExhausted = timingSlots.AssignedCount() >= timingSlots.SlotBudget();
    RenderGraphSnapshot snapshot = BuildRenderGraphSnapshot(
        compiled, input, [this](const char* name) { return LastKnownStatsFor(name); }, timingSlotBudgetExhausted);
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

// network-impl-4 campaign, Phase 2 - thin forwarders onto m_debugTextures
// (see RenderGraph.h's own doc comments on each of these).
std::optional<DebugTextureSnapshot> RenderGraph::DebugTextureSnapshotFor(const std::string& name) const
{
    return m_debugTextures.FindByName(name);
}

std::vector<DebugTextureSnapshot> RenderGraph::ListDebugTextures() const
{
    return m_debugTextures.ListAll();
}

// network-impl-6 campaign, Phase 2 - thin forwarders onto
// m_debugVolumeTextures (see RenderGraph.h's own doc comments on each of
// these).
std::optional<DebugVolumeTextureSnapshot> RenderGraph::DebugVolumeTextureSnapshotFor(const std::string& name) const
{
    return m_debugVolumeTextures.FindByName(name);
}

std::vector<DebugVolumeTextureSnapshot> RenderGraph::ListDebugVolumeTextures() const
{
    return m_debugVolumeTextures.ListAll();
}

void RenderGraph::NotifyDebugTextureStateOverride(const std::string& name, const ResourceState& newColorState)
{
    m_debugTextures.ApplyColorStateOverride(name, newColorState);
}

std::uint64_t RenderGraph::CurrentDebugTextureFrameCounter() const noexcept
{
    return m_debugTextureFrameCounter;
}

} // namespace gte::rg
