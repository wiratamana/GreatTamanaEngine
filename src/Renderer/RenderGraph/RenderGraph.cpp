#include "RenderGraph.h"

#include "../Renderer.h"
#include "../../Core/Logging.h" // PHASE1 (render-pass-6 campaign, item 2.4) - GTE_LOG_WARNING for slot-budget overflow. Moved from Editor/Logger.h to Core/Logging.h by editor-core-separation-1's own PHASE3 (PHASE3_LOGGING_GLOBAL_LOGSINK_EXTRACTION.md) - gte_core must never include anything under src/Editor/.

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
    , m_persistentResourceCache(renderer)
    , m_timestampPool(QueryVulkanContextInfo(renderer).device, QueryVulkanContextInfo(renderer).graphicsQueue,
          QueryVulkanContextInfo(renderer).graphicsQueueFamily, QueryVulkanContextInfo(renderer).timestampCapability,
          kSynchronousTimingSlotBudget, kPipelinedTimingSlotBudget, kGpuTimingFramesInFlight)
{
    // editor-core-separation-26 campaign, PHASE6 - see RenderGraph.h's own
    // m_renderer doc comment.
    m_renderer = &renderer;
}

// editor-core-separation-27 campaign, PHASE7 - see RenderGraph.h's own doc
// comment. Must be called EXACTLY once per real engine frame, by
// Core::BuildFrame(), before either ExecuteTimingMode regime's Execute()
// call runs that frame.
void RenderGraph::BeginPersistentResourceFrame() noexcept
{
    ++m_persistentResourceFrameCounter;
    m_persistentResourceCache.BeginFrame(
        m_persistentResourceFrameCounter, kPersistentResourceStaleThresholdFrames);
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
        tex.sampler = importInfo.colorSampler; // the real, already-live sampler the caller supplied at import time.
        tex.depthSampler = importInfo.depthSampler;
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
        tex.depthSampler = renderTexture.DepthSampler();
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

// better-render-pass-3 campaign, BLOCK5, Phase 3 - mirrors
// EnsureVolumeTextureResolved() above for the IMPORT branch, AND
// EnsureTextureResolved()'s own RenderGraphResourcePool::AcquireTexture()
// call for the POOLED branch - TextureArray needs BOTH branches, unlike
// VolumeTexture (import-only in practice).
void RenderGraph::EnsureTextureArrayResolved(std::uint32_t index, const CompiledGraphInput& input,
    std::vector<PhysicalTextureArray>& physicalTextureArrays)
{
    PhysicalTextureArray& arr = physicalTextureArrays[index];
    if (arr.resolved) {
        return;
    }

    const TextureArrayImportInfo& importInfo = input.textureArrays[index].importInfo;
    if (importInfo.isImported) {
        arr.isImported = true;
        arr.target = importInfo.externalTarget;
        arr.layerStates.assign(arr.target.arrayLayers,
            ResourceState{ importInfo.currentLayout, VK_PIPELINE_STAGE_2_TOP_OF_PIPE_BIT, VK_ACCESS_2_NONE });
    } else {
        arr.isImported = false;
        TextureArray2D& textureArray =
            m_resourcePool.AcquireTextureArray(input.textureArrays[index].desc, input.textureArrays[index].name);
        arr.target = textureArray.Target();
        arr.layerStates.assign(arr.target.arrayLayers, ResourceState{});
    }
    arr.resolved = true;
}

void RenderGraph::ApplyUsageBarrierIfNeeded(VkCommandBuffer cmd, const ResourceUsage& usage,
    const CompiledGraphInput& input, std::vector<PhysicalTexture>& physicalTextures,
    std::vector<PhysicalBuffer>& physicalBuffers, std::vector<PhysicalVolumeTexture>& physicalVolumeTextures,
    std::vector<PhysicalTextureArray>& physicalTextureArrays)
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
    // usage.volumeTexture. better-render-pass-3 campaign, BLOCK5, Phase 3 -
    // grew a 4th lambda for TextureArrayHandle.
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
        },
        [&](TextureArrayHandle textureArrayHandle) {
            // State is tracked PER LAYER (arr.layerStates) - a per-layer
            // usage (usage.arrayLayerIndex has a value) reads/writes
            // exactly one entry; a whole-array usage (ReadTextureArray()/
            // WriteTextureArray()) must check/update every entry, since
            // two per-layer writes against different layers in the same
            // frame must never silently share one tracked state.
            EnsureTextureArrayResolved(textureArrayHandle.index, input, physicalTextureArrays);
            PhysicalTextureArray& arr = physicalTextureArrays[textureArrayHandle.index];

            const VkImageAspectFlags aspect = arr.target.hasDepth
                ? static_cast<VkImageAspectFlags>(VK_IMAGE_ASPECT_DEPTH_BIT)
                : static_cast<VkImageAspectFlags>(VK_IMAGE_ASPECT_COLOR_BIT);
            const ResourceState next = RequiredStateFor(usage.access, arr.target.hasDepth);

            if (usage.arrayLayerIndex.has_value()) {
                const std::uint32_t layer = *usage.arrayLayerIndex;
                assert(layer < arr.layerStates.size() &&
                    "RenderGraph::ApplyUsageBarrierIfNeeded: arrayLayerIndex out of range for this handle's arrayLayers.");
                const TextureArraySubresourceDecision decision =
                    DecideTextureArrayLayerTransition(arr.layerStates[layer], aspect, layer, next);
                if (decision.requiresBarrier) {
                    EmitImageBarrier(cmd, arr.target.image, decision.range, arr.layerStates[layer], next);
                }
                arr.layerStates[layer] = next;
            } else {
                // Whole-array usage - re-synchronizes every layer. A
                // single barrier covering the full range is only correct
                // when every layer currently differing from `next` shares
                // the exact same previous state (layerStates[0] as the
                // "representative" previous state is only safe then) - so
                // first check, with a flat per-index pass, whether that
                // actually holds right now.
                bool allLayersMatchFirst = true;
                for (const ResourceState& layerState : arr.layerStates) {
                    if (!(layerState == arr.layerStates[0])) {
                        allLayersMatchFirst = false;
                        break;
                    }
                }

                if (allLayersMatchFirst) {
                    // Common case (every layer already agrees) - exactly
                    // one barrier for the full range.
                    if (RequiresBarrier(arr.layerStates[0], next)) {
                        const VkImageSubresourceRange range{ aspect, 0, 1, 0, arr.target.arrayLayers };
                        EmitImageBarrier(cmd, arr.target.image, range, arr.layerStates[0], next);
                    }
                } else {
                    // Layers have diverged (e.g. a whole-array re-sync
                    // after only some layers were individually rewritten
                    // since the last whole-array usage) - a single barrier
                    // using any one layer's state as "the" previous state
                    // would be wrong for every other layer that disagrees
                    // with it. Fall back to one barrier per layer that
                    // actually needs one, reusing
                    // DecideTextureArrayLayerTransition() verbatim - still
                    // a flat, per-index comparison, never a cross-pass
                    // dependency graph.
                    for (std::uint32_t layer = 0; layer < arr.target.arrayLayers; ++layer) {
                        const TextureArraySubresourceDecision decision =
                            DecideTextureArrayLayerTransition(arr.layerStates[layer], aspect, layer, next);
                        if (decision.requiresBarrier) {
                            EmitImageBarrier(cmd, arr.target.image, decision.range, arr.layerStates[layer], next);
                        }
                    }
                }

                for (ResourceState& layerState : arr.layerStates) {
                    layerState = next;
                }
            }
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
    std::vector<PhysicalTextureArray>& physicalTextureArrays, DrawStats& passDrawStats)
{
    PassContext ctx;
    ctx.cmd = cmd;
    ctx.textures = &physicalTextures;
    ctx.buffers = &physicalBuffers;
    ctx.volumeTextures = &physicalVolumeTextures;
    // better-render-pass-3 campaign, BLOCK5, Phase 3 - mirrors
    // ctx.volumeTextures above exactly.
    ctx.textureArrays = &physicalTextureArrays;
    // task_manager/better-render-pass-1 campaign, PHASE3 - forwards this
    // RenderGraph's own non-owning m_renderer member into the PassContext,
    // so a pass's `execute` callback can call ctx.Cmd() to obtain a
    // CommandBuffer (CommandBuffer.h).
    ctx.renderer = m_renderer;
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
        ResolvedTexture result{ tex.target.imageView, tex.sampler };
        if (IsResolvedViewMissingItsSampler(result.view, result.sampler)) {
            assert(false && "PassContext::resolveReadTexture() - non-null view with a null sampler");
            GTE_LOG_ERROR("RenderGraph",
                "resolveReadTexture() - handle index " + std::to_string(handle.index)
                    + " resolved a non-null view with a null sampler.");
        }
        return result;
    }
    return ResolvedTexture{};
}

// Depth sub-resource sibling of resolveReadTexture() above - same shape,
// gated additionally on tex.hasDepth so a texture with no depth sub-resource
// never returns a stale/zero depth view.
PassContext::ResolvedDepthTexture PassContext::resolveDepthTexture(TextureHandle handle) const noexcept
{
    if (textures != nullptr && handle.index < textures->size() && (*textures)[handle.index].resolved
        && (*textures)[handle.index].hasDepth) {
        const RenderGraph::PhysicalTexture& tex = (*textures)[handle.index];
        ResolvedDepthTexture result{ tex.target.depthImageView, tex.depthSampler };
        if (IsResolvedViewMissingItsSampler(result.view, result.sampler)) {
            assert(false && "PassContext::resolveDepthTexture() - non-null view with a null sampler");
            GTE_LOG_ERROR("RenderGraph",
                "resolveDepthTexture() - handle index " + std::to_string(handle.index)
                    + " resolved a non-null view with a null sampler.");
        }
        return result;
    }
    return ResolvedDepthTexture{};
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

// better-render-pass-3 campaign, BLOCK5, Phase 3 - mirrors
// resolveVolumeTexture() above exactly ("view only", no sampler).
PassContext::ResolvedTextureArray PassContext::resolveTextureArray(TextureArrayHandle handle) const noexcept
{
    if (textureArrays != nullptr && handle.index < textureArrays->size()
        && (*textureArrays)[handle.index].resolved) {
        return ResolvedTextureArray{ (*textureArrays)[handle.index].target.imageView };
    }
    return ResolvedTextureArray{};
}

// Per-layer sibling of resolveTextureArray() above - same "resolve whatever
// was already resolved" discipline, just returning the one layer's own view
// (via TextureArrayTarget::LayerView()) plus the array's whole image.
PassContext::ResolvedTextureArrayLayer PassContext::resolveArrayLayer(
    TextureArrayHandle handle, std::uint32_t layerIndex) const noexcept
{
    if (textureArrays != nullptr && handle.index < textureArrays->size()
        && (*textureArrays)[handle.index].resolved) {
        const RenderGraph::PhysicalTextureArray& arr = (*textureArrays)[handle.index];
        return ResolvedTextureArrayLayer{ arr.target.LayerView(layerIndex), arr.target.image };
    }
    return ResolvedTextureArrayLayer{};
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
    const std::vector<PhysicalTextureArray>& physicalTextureArrays, std::vector<VkExtent2D>& outResolvedExtents) const
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

    // A COLOR-kind array-layer attachment (WriteArrayLayer()) lands AFTER
    // every pass.colorAttachments entry above - shader-visible as
    // layout(location = pass.colorAttachments.size()). A DEPTH-kind one is
    // handled entirely by BuildDepthAttachmentInfo() instead; a pass never
    // contributes to both lists from the same arrayLayerAttachment.
    if (pass.arrayLayerAttachment.has_value()) {
        const ArrayLayerAttachmentDesc& arrDesc = *pass.arrayLayerAttachment;
        assert(arrDesc.handle.index < physicalTextureArrays.size() &&
            physicalTextureArrays[arrDesc.handle.index].resolved &&
            "RenderGraph::BuildColorAttachmentInfos: pass.arrayLayerAttachment was never "
            "resolved - WriteArrayLayer() must always also push a matching write onto "
            "pass.writes.");
        const PhysicalTextureArray& arr = physicalTextureArrays[arrDesc.handle.index];
        if (!arr.target.hasDepth) {
            outResolvedExtents.push_back(arr.target.extent);

            VkRenderingAttachmentInfo colorAttachment{};
            colorAttachment.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
            colorAttachment.imageView = arr.target.LayerView(arrDesc.layerIndex);
            colorAttachment.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
            colorAttachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
            if (arrDesc.clearColor.has_value()) {
                colorAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
                const std::array<float, 4>& c = *arrDesc.clearColor;
                colorAttachment.clearValue.color = { { c[0], c[1], c[2], c[3] } };
            } else {
                colorAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_LOAD;
            }
            colorAttachmentInfos.push_back(colorAttachment);
        }
    }

    return colorAttachmentInfos;
}

// render-pass-6 campaign, PHASE2 (item 2.6) - extracted out of
// ExecuteCompiledGraph() for readability, zero behavior change - see
// PHASE2_EXECUTE_COMPILED_GRAPH_EXTRACTION.md. `depthHandle` alone signals
// "no plain-Texture depth write this call" via its own IsValid() (see this
// method's declaration in RenderGraph.h for the full reasoning) - falls back
// to pass.arrayLayerAttachment (a DEPTH-kind array-layer attachment) when
// that isn't set, identical produced VkRenderingAttachmentInfo fields either
// way.
RenderGraph::DepthAttachmentResult RenderGraph::BuildDepthAttachmentInfo(const PassRecord& pass,
    const std::vector<PhysicalTexture>& physicalTextures,
    const std::vector<PhysicalTextureArray>& physicalTextureArrays, TextureHandle depthHandle) const
{
    DepthAttachmentResult result;

    // Defensive - these two depth-target sources must never both be
    // populated for one pass (catches an authoring mistake instead of
    // silently keeping the plain-Texture one and dropping the
    // array-layer one).
    assert((!depthHandle.IsValid() || !pass.arrayLayerAttachment.has_value() ||
               pass.arrayLayerAttachment->handle.index >= physicalTextureArrays.size() ||
               !physicalTextureArrays[pass.arrayLayerAttachment->handle.index].target.hasDepth) &&
           "RenderGraph::BuildDepthAttachmentInfo: pass declared BOTH a plain Texture depth "
           "write and a DEPTH array-layer attachment - at most one depth target per pass.");

    if (depthHandle.IsValid()) {
        const PhysicalTexture& depthTex = physicalTextures[depthHandle.index];
        result.extent = depthTex.target.extent;

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
        result.info = depthAttachment;
        return result;
    }

    if (pass.arrayLayerAttachment.has_value()) {
        const ArrayLayerAttachmentDesc& arrDesc = *pass.arrayLayerAttachment;
        assert(arrDesc.handle.index < physicalTextureArrays.size() &&
            physicalTextureArrays[arrDesc.handle.index].resolved &&
            "RenderGraph::BuildDepthAttachmentInfo: pass.arrayLayerAttachment was never resolved.");
        const PhysicalTextureArray& arr = physicalTextureArrays[arrDesc.handle.index];
        if (arr.target.hasDepth) {
            result.extent = arr.target.extent;

            VkRenderingAttachmentInfo depthAttachment{};
            depthAttachment.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
            depthAttachment.imageView = arr.target.LayerView(arrDesc.layerIndex);
            // TextureArray2D never allocates a stencil aspect - see
            // TextureArray2D.cpp's own constructor (always
            // VK_IMAGE_ASPECT_DEPTH_BIT alone when hasDepth == true).
            depthAttachment.imageLayout = VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL;
            depthAttachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
            if (arrDesc.clearDepth.has_value()) {
                depthAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
                depthAttachment.clearValue.depthStencil = { *arrDesc.clearDepth, 0 };
            } else {
                depthAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_LOAD;
            }
            result.info = depthAttachment;
        }
    }

    return result;
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
    // better-render-pass-3 campaign, BLOCK5, Phase 3.
    std::vector<PhysicalTextureArray> physicalTextureArrays(input.textureArrays.size());

    RenderGraphNameSlotTable& timingSlots = isPipelined ? m_pipelinedTimingSlots : m_synchronousTimingSlots;

    for (const PassHandle& passHandle : compiled.executionOrder) {
        PassRecord& pass = input.passes[passHandle.index];

        // Reads before writes - order between the two doesn't affect
        // correctness (each usage's own barrier is applied strictly before
        // this pass's `execute` callback runs either way), but reads-first
        // mirrors how a pass conceptually consumes its inputs before
        // producing its outputs.
        for (const ResourceUsage& usage : pass.reads) {
            ApplyUsageBarrierIfNeeded(
                cmd, usage, input, physicalTextures, physicalBuffers, physicalVolumeTextures, physicalTextureArrays);
        }
        for (const ResourceUsage& usage : pass.writes) {
            ApplyUsageBarrierIfNeeded(
                cmd, usage, input, physicalTextures, physicalBuffers, physicalVolumeTextures, physicalTextureArrays);
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
            // editor-core-separation-13 campaign, PHASE5 - `reported` is now
            // OWNED std::string entries (see this vector's own doc comment,
            // RenderGraph.h) - `pass.name` (this frame's own, still-valid
            // pointer) is compared/stored safely either way.
            std::vector<std::string>& reported =
                isPipelined ? m_reportedPipelinedOverflows : m_reportedSynchronousOverflows;
            bool alreadyReported = false;
            if (pass.name != nullptr) {
                for (const std::string& n : reported) {
                    if (n == pass.name) {
                        alreadyReported = true;
                        break;
                    }
                }
            }
            if (!alreadyReported) {
                reported.emplace_back(pass.name != nullptr ? pass.name : "<unnamed>");
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
        // campaign, see task_manager/mrt-1/PHASE0_MASTER_STRATEGY.md). Only a
        // pass with no color attachment, no plain-Texture depth write, AND
        // no array-layer attachment (pass.arrayLayerAttachment) at all gets
        // no vkCmdBeginRendering bracket - its `execute` callback is then
        // invoked with a zero-extent PassContext and is expected to record
        // whatever non-rendering Vulkan work it needs directly against `cmd`.
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
        // The bracket-opening condition is a three-way OR: a plain color
        // attachment, a plain-Texture depth write, or an array-layer
        // attachment (color or depth) - WriteColorAttachment() (PHASE1) is
        // still the only call site that ever constructs a
        // ColorAttachmentWrite usage, always in lockstep with
        // pass.colorAttachments; WriteArrayLayer() is the array-layer
        // sibling, always in lockstep with pass.arrayLayerAttachment.
        const bool hasAnyAttachment =
            !pass.colorAttachments.empty() || depthHandle.IsValid() || pass.arrayLayerAttachment.has_value();

        // render-pass-6 campaign, PHASE2 (item 2.6) - BuildPassContext()
        // extracted below (see this class's own header comment on that
        // method for the full reasoning). `passDrawStats` must live in
        // THIS function's own scope (not inside BuildPassContext() itself)
        // since UpdateDrawStatsFor(pass.name, passDrawStats) below still
        // needs to read it after pass.execute(ctx) returns.
        DrawStats passDrawStats;
        PassContext ctx = BuildPassContext(
            cmd, physicalTextures, physicalBuffers, physicalVolumeTextures, physicalTextureArrays, passDrawStats);

        bool didBeginRendering = false;
        if (hasAnyAttachment) {
            // render-pass-6 campaign, PHASE2 (item 2.6) - BuildColorAttachmentInfos()
            // extracted below; this function still owns the
            // FindMismatchedColorAttachmentExtent() check, the
            // vkCmdBeginRendering/vkCmdSetViewport/vkCmdSetScissor calls, and the
            // ctx.colorAttachmentExtent assignment - see
            // PHASE2_EXECUTE_COMPILED_GRAPH_EXTRACTION.md, Step 3.2.
            std::vector<VkExtent2D> resolvedExtents;
            const std::vector<VkRenderingAttachmentInfo> colorAttachmentInfos =
                BuildColorAttachmentInfos(pass, physicalTextures, physicalTextureArrays, resolvedExtents);

            // LOCKED (see task_manager/mrt-1/PHASE2_EXECUTE_LAYER_MRT_RECORDING.md's
            // own Step 3.2 "Decision 2"): a real, unconditional throw - never
            // a plain assert() a release/NDEBUG build would silently compile
            // away. Built on the pure, Tier-1-tested decision function above
            // (RenderGraphTypes.h/.cpp), so this exact check has real,
            // VkDevice-free test coverage (RenderGraphTypesTests.cpp).
            // FindMismatchedColorAttachmentExtent() already returns nullopt
            // vacuously for size() <= 1, so the size() > 1 guard below is a
            // cheap, purely-for-clarity short-circuit, not a required fix.
            if (resolvedExtents.size() > 1) {
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
            }

            const DepthAttachmentResult depthResult =
                BuildDepthAttachmentInfo(pass, physicalTextures, physicalTextureArrays, depthHandle);

            // A depth-only pass (array-layer OR plain Texture) has an EMPTY
            // resolvedExtents - the render-area/viewport size must then come
            // from the depth attachment itself. Never index resolvedExtents[0]
            // unconditionally once a depth-only pass can reach this code.
            VkExtent2D firstExtent{};
            if (!resolvedExtents.empty()) {
                firstExtent = resolvedExtents[0];
            } else if (depthResult.info.has_value()) {
                firstExtent = depthResult.extent;
            }

            VkRenderingInfo renderingInfo{};
            renderingInfo.sType = VK_STRUCTURE_TYPE_RENDERING_INFO;
            renderingInfo.renderArea = { { 0, 0 }, firstExtent };
            renderingInfo.layerCount = 1;
            renderingInfo.colorAttachmentCount = static_cast<std::uint32_t>(colorAttachmentInfos.size());
            renderingInfo.pColorAttachments = colorAttachmentInfos.empty() ? nullptr : colorAttachmentInfos.data();
            renderingInfo.pDepthAttachment = depthResult.info.has_value() ? &depthResult.info.value() : nullptr;

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
        } else if (pass.kind == PassKind::Blit && pass.blitCommand.has_value()) {
            // editor-core-separation-26 campaign, PHASE6 - the vkCmdBlitImage2
            // execution branch for a real, producible RenderGraphBuilder::
            // AddBlitPass() pass. The generic per-usage barrier loop
            // (ApplyUsageBarrierIfNeeded()) already ran, for THIS pass, above -
            // physicalTextures[spec.src.index]/[spec.dst.index] are ALREADY
            // resolved AND already correctly barriered (into
            // VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL/_DST_OPTIMAL) by this point -
            // this branch reads them, it never re-resolves or re-barriers
            // anything.
            const BlitSpec& spec = *pass.blitCommand;

            const PhysicalTexture& srcTex = physicalTextures[spec.src.index];
            const PhysicalTexture& dstTex = physicalTextures[spec.dst.index];

            const VkImage srcImage = spec.srcIsDepth ? srcTex.target.depthImage : srcTex.target.image;
            const VkImage dstImage = spec.dstIsDepth ? dstTex.target.depthImage : dstTex.target.image;
            const VkImageAspectFlags srcAspect = spec.srcIsDepth
                ? (VK_IMAGE_ASPECT_DEPTH_BIT | (srcTex.target.depthHasStencil ? VK_IMAGE_ASPECT_STENCIL_BIT : 0))
                : static_cast<VkImageAspectFlags>(VK_IMAGE_ASPECT_COLOR_BIT);
            const VkImageAspectFlags dstAspect = spec.dstIsDepth
                ? (VK_IMAGE_ASPECT_DEPTH_BIT | (dstTex.target.depthHasStencil ? VK_IMAGE_ASPECT_STENCIL_BIT : 0))
                : static_cast<VkImageAspectFlags>(VK_IMAGE_ASPECT_COLOR_BIT);

            const ResolvedBlitRegion srcRegion =
                ResolveBlitRegion(spec.srcRegionMin, spec.srcRegionMax, srcTex.target.extent);
            const ResolvedBlitRegion dstRegion =
                ResolveBlitRegion(spec.dstRegionMin, spec.dstRegionMax, dstTex.target.extent);
            assert(IsValidBlitRegion(srcRegion, srcTex.target.extent) &&
                "RenderGraph::ExecuteCompiledGraph: blit pass declared an invalid SRC region");
            assert(IsValidBlitRegion(dstRegion, dstTex.target.extent) &&
                "RenderGraph::ExecuteCompiledGraph: blit pass declared an invalid DST region");

            // Source document's own "CAUTION" bullet - a real, engine-checked
            // precondition (Locked Decision 4), never a documented-only trust.
            assert(((!spec.srcIsDepth && !spec.dstIsDepth) || m_renderer->SupportsDepthBlit())
                && "RenderGraph::ExecuteCompiledGraph: blit pass declared srcIsDepth/dstIsDepth but this "
                   "device does not report VK_FORMAT_FEATURE_BLIT_SRC_BIT/_DST_BIT support for the engine's "
                   "real depth format - see VulkanDevice::SupportsDepthBlit().");

            const VkFilter effectiveFilter = ResolveEffectiveBlitFilter(spec);

            VkImageBlit2 region{};
            region.sType = VK_STRUCTURE_TYPE_IMAGE_BLIT_2;
            region.srcSubresource = VkImageSubresourceLayers{ srcAspect, 0, 0, 1 };
            region.srcOffsets[0] = srcRegion.min;
            region.srcOffsets[1] = srcRegion.max;
            region.dstSubresource = VkImageSubresourceLayers{ dstAspect, 0, 0, 1 };
            region.dstOffsets[0] = dstRegion.min;
            region.dstOffsets[1] = dstRegion.max;

            VkBlitImageInfo2 blitInfo{};
            blitInfo.sType = VK_STRUCTURE_TYPE_BLIT_IMAGE_INFO_2;
            blitInfo.srcImage = srcImage;
            blitInfo.srcImageLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
            blitInfo.dstImage = dstImage;
            blitInfo.dstImageLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
            blitInfo.regionCount = 1;
            blitInfo.pRegions = &region;
            blitInfo.filter = effectiveFilter;

            vkCmdBlitImage2(cmd, &blitInfo);
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

    // editor-core-separation-27 campaign, PHASE8 (BIG_STEP_3, Section
    // 5.1/5.5) - honest layout recording, every regime, every call (a
    // persistent texture's real color image layout must survive frame-to-
    // frame HONESTLY, never guessed/hardcoded); batched resize flush,
    // SynchronousImmediateReadback only (mirrors m_resourcePool.BeginFrame()'s
    // own `if (!isPipelined) { ... }` gating a few lines above this
    // function's own pass-recording loop - a resize must never be triggered
    // from the pipelined/Present regime, which must never issue a
    // vkDeviceWaitIdle()).
    for (const TextureHandle& h : input.persistentCacheTextures) {
        if (h.index >= physicalTextures.size() || !physicalTextures[h.index].resolved) {
            continue; // defensive - mirrors RegisterDebugTextureSnapshots()'s own identical guard.
        }
        m_persistentResourceCache.RecordFinalLayout(
            input.textures[h.index].name, physicalTextures[h.index].colorState.layout);
    }
    if (!isPipelined) {
        m_persistentResourceCache.FlushPendingResizes();
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
    // PHASE1 (render-pass-6 campaign, item 2.4) - "is this table currently at
    // 100% capacity" at snapshot-build time is exactly the persistent
    // condition RenderGraphSnapshot::timingSlotBudgetExhausted is meant to
    // describe - simpler and equally correct than plumbing JustOverflowed()'s
    // one-shot state through.
    const bool timingSlotBudgetExhausted = timingSlots.AssignedCount() >= timingSlots.SlotBudget();

    // editor-core-separation-25 campaign - built fresh every call, exactly
    // like the statsLookup lambda immediately above it (which this
    // mirrors precisely) - std::function is cheap here, this runs once
    // per Execute() call, not once per pass. Left as a default-constructed
    // (empty) std::function when no provider is installed - BuildPassSnapshot()
    // (RenderGraphSnapshot.cpp) already treats an empty metadataLookup
    // exactly like "no entry found for this index" (PHASE3), so a headless
    // build takes this exact same code path with zero extra cost beyond
    // the one null-pointer check below.
    std::function<bool(std::size_t, PassDebugMetadata&)> metadataLookup;
    if (m_debugMetadataProvider != nullptr) {
        IPassDebugMetadataProvider* provider = m_debugMetadataProvider;
        metadataLookup = [provider](std::size_t declarationIndex, PassDebugMetadata& outMetadata) {
            return provider->QueryPassDebugMetadata(declarationIndex, outMetadata);
        };
    }

    RenderGraphSnapshot snapshot = BuildRenderGraphSnapshot(compiled, input,
        [this](const char* name) { return LastKnownStatsFor(name); }, timingSlotBudgetExhausted, metadataLookup);
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
    // editor-core-separation-13 campaign, PHASE5 - `entry.name` is now an
    // OWNED std::string (see NamedStats' own doc comment, RenderGraph.h) -
    // compares safely against the incoming, still-guaranteed-valid-this-
    // frame `name` via std::string::operator==(const char*), never
    // std::strcmp() against a possibly-already-FreeLibrary()'d pointer.
    for (NamedStats& entry : m_lastKnownStats) {
        if (entry.name == name) {
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
    // editor-core-separation-13 campaign, PHASE5 - see UpdateDrawStatsFor()'s
    // own identical comment immediately above.
    for (NamedStats& entry : m_lastKnownStats) {
        if (entry.name == name) {
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
        // editor-core-separation-13 campaign, PHASE5 - see
        // UpdateDrawStatsFor()'s own identical comment above.
        for (const NamedStats& entry : m_lastKnownStats) {
            if (entry.name == passName) {
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
