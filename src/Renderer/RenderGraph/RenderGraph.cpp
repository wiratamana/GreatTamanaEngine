#include "RenderGraph.h"

#include "../Renderer.h"
#include "../GraphicsHandleRegistry.h"
#include "../VulkanFormatInfo.h"
#include "RenderPassToggleRegistry.h"
#include "../../Core/Logging.h"

#include <algorithm>
#include <cassert>
#include <cstring>
#include <stdexcept>

namespace gte {

Renderer::VulkanContextInfo QueryVulkanContextInfoForRenderGraph(Renderer& renderer)
{
    return renderer.GetVulkanContextInfo();
}

} // namespace gte

namespace gte::rg {

RenderGraph::RenderGraph(Renderer& renderer)
    : m_resourcePool(renderer)
    , m_persistentResourceCache(renderer)
    , m_computeBindingCache(renderer)
    , m_timestampPool(QueryVulkanContextInfoForRenderGraph(renderer).device, renderer,
          QueryVulkanContextInfoForRenderGraph(renderer).graphicsQueueFamily,
          QueryVulkanContextInfoForRenderGraph(renderer).timestampCapability, kSynchronousTimingSlotBudget,
          kPipelinedTimingSlotBudget, kGpuTimingFramesInFlight)
    , m_eventSnapshotPool(renderer.GetVmaAllocator(), renderer.GetMemoryTracker(),
          QueryVulkanContextInfoForRenderGraph(renderer).device, kEventSnapshotSlotBudget, kGpuTimingFramesInFlight)
{
    m_renderer = &renderer;
}

// Call once per engine frame, outside graph execution. Binding sets are freed only after
// kDefaultStaleFrames of disuse, so no in-flight command buffer can still reference one.
void RenderGraph::BeginPersistentResourceFrame() noexcept
{
    ++m_persistentResourceFrameCounter;
    m_persistentResourceCache.BeginFrame(
        m_persistentResourceFrameCounter, kPersistentResourceStaleThresholdFrames);
    m_computeBindingCache.BeginFrame(m_persistentResourceFrameCounter);
    for (const VkDescriptorSetLayout layout : m_renderer->TakeQueuedBindingReleases()) {
        m_computeBindingCache.Release(layout);
    }
}

void RenderGraph::EnsureTextureResolved(
    std::uint32_t index, const CompiledGraphInput& input, std::vector<PhysicalTexture>& physicalTextures)
{
    PhysicalTexture& tex = physicalTextures[index];
    if (tex.resolved) {
        return;
    }

    const TextureSlot& slot = input.textures[index];
    const TextureImportInfo& importInfo = slot.importInfo;
    tex.dimension = slot.desc.dimension;
    tex.isImported = importInfo.isImported;
    const ResourceState importedState{
        ToVkImageLayout(importInfo.currentLayout), VK_PIPELINE_STAGE_2_TOP_OF_PIPE_BIT, VK_ACCESS_2_NONE };

    switch (tex.dimension) {
    case TextureDimension::Tex2D:
        if (importInfo.isImported) {
            // Externally owned resource with caller-provided initial layout.
            tex.target = importInfo.externalTarget;
            tex.sampler = Native(importInfo.colorSampler);
            tex.depthSampler = Native(importInfo.depthSampler);
            tex.hasDepth = importInfo.externalTarget.depthImage != VK_NULL_HANDLE;
            tex.colorState = importedState;
            tex.depthState = ResourceState{};
        } else {
            // Transient texture from the internal resource pool.
            RenderTexture& renderTexture = m_resourcePool.AcquireRenderTexture(slot.desc, slot.name);
            tex.target = renderTexture.Target();
            tex.sampler = renderTexture.Sampler();
            tex.depthSampler = renderTexture.DepthSampler();
            tex.hasDepth = slot.desc.hasDepth;
            tex.colorState = ResourceState{};
            tex.depthState = ResourceState{};
        }
        break;

    case TextureDimension::Tex3D:
        // 3D volumes are import-only; a non-imported one stays an empty target.
        if (importInfo.isImported) {
            const VolumeTarget& volume = importInfo.externalVolumeTarget;
            tex.target.image = volume.image;
            tex.target.imageView = volume.imageView;
            tex.sampler = Native(importInfo.colorSampler);
            tex.target.extent = VkExtent2D{ volume.extent.width, volume.extent.height };
            tex.target.format = volume.format;
            tex.depthSlices = volume.extent.depth;
            tex.colorState = importedState;
        } else {
            tex.colorState = ResourceState{};
        }
        break;

    case TextureDimension::Array2D:
    case TextureDimension::Cube:
        if (importInfo.isImported) {
            tex.arrayTarget = importInfo.externalArrayTarget;
            tex.layerStates.assign(tex.arrayTarget.arrayLayers, importedState);
        } else {
            TextureArray2D& textureArray = m_resourcePool.AcquireTextureArray(slot.desc, slot.name);
            tex.arrayTarget = textureArray.Target();
            tex.layerStates.assign(tex.arrayTarget.arrayLayers, ResourceState{});
        }
        tex.target.image = tex.arrayTarget.image;
        tex.target.imageView = tex.arrayTarget.imageView;
        tex.target.extent = tex.arrayTarget.extent;
        tex.target.format = tex.arrayTarget.format;
        tex.sampler = tex.arrayTarget.sampler;
        break;
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

    const BufferImportInfo& importInfo = input.buffers[index].importInfo;
    if (importInfo.isImported) {
        // Resolve externally owned buffer.
        buf.buffer = Native(importInfo.externalBuffer);
        buf.size = importInfo.size;
        buf.state = ResourceState{};
    } else {
        // Acquire transient buffer from the resource pool.
        Buffer& buffer = m_resourcePool.AcquireBuffer(input.buffers[index].desc, input.buffers[index].name);
        buf.buffer = buffer.Native();
        buf.size = buffer.Size();
        buf.state = ResourceState{};
    }
    buf.resolved = true;
}

void RenderGraph::ApplyUsageBarrierIfNeeded(VkCommandBuffer cmd, const ResourceUsage& usage,
    const CompiledGraphInput& input, std::vector<PhysicalTexture>& physicalTextures,
    std::vector<PhysicalBuffer>& physicalBuffers, AppliedBarrierInfo* outBarrierInfo)
{
    DispatchByKind(usage,
        [&](TextureHandle textureHandle) {
            EnsureTextureResolved(textureHandle.index, input, physicalTextures);
            PhysicalTexture& tex = physicalTextures[textureHandle.index];

            switch (tex.dimension) {
            case TextureDimension::Tex2D: {
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
                    if (outBarrierInfo != nullptr) {
                        *outBarrierInfo = AppliedBarrierInfo{ true, state.layout, next.layout };
                    }
                }
                state = next;
                break;
            }

            case TextureDimension::Tex3D: {
                const ResourceState next = RequiredStateFor(usage.access, false);
                if (RequiresBarrier(tex.colorState, next)) {
                    const VkImageSubresourceRange range{ VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 };
                    EmitImageBarrier(cmd, tex.target.image, range, tex.colorState, next);
                    if (outBarrierInfo != nullptr) {
                        *outBarrierInfo = AppliedBarrierInfo{ true, tex.colorState.layout, next.layout };
                    }
                }
                tex.colorState = next;
                break;
            }

            case TextureDimension::Array2D:
            case TextureDimension::Cube: {
                const TextureArrayTarget& arr = tex.arrayTarget;
                std::vector<ResourceState>& layerStates = tex.layerStates;

                const VkImageAspectFlags aspect = arr.hasDepth
                    ? static_cast<VkImageAspectFlags>(VK_IMAGE_ASPECT_DEPTH_BIT)
                    : static_cast<VkImageAspectFlags>(VK_IMAGE_ASPECT_COLOR_BIT);
                const ResourceState next = RequiredStateFor(usage.access, arr.hasDepth);

                if (usage.arrayLayerIndex.has_value()) {
                    // Per-layer usage transition.
                    const std::uint32_t layer = *usage.arrayLayerIndex;
                    assert(layer < layerStates.size() &&
                        "RenderGraph::ApplyUsageBarrierIfNeeded: arrayLayerIndex out of range for this handle's arrayLayers.");
                    const TextureArraySubresourceDecision decision =
                        DecideTextureArrayLayerTransition(layerStates[layer], aspect, layer, next);
                    if (decision.requiresBarrier) {
                        EmitImageBarrier(cmd, arr.image, decision.range, layerStates[layer], next);
                        if (outBarrierInfo != nullptr) {
                            *outBarrierInfo = AppliedBarrierInfo{ true, layerStates[layer].layout, next.layout };
                        }
                    }
                    layerStates[layer] = next;
                } else {
                    // Whole-image transition: one barrier if all layers match, individual barriers otherwise.
                    bool allLayersMatchFirst = true;
                    for (const ResourceState& layerState : layerStates) {
                        if (!(layerState == layerStates[0])) {
                            allLayersMatchFirst = false;
                            break;
                        }
                    }

                    if (allLayersMatchFirst) {
                        if (RequiresBarrier(layerStates[0], next)) {
                            const VkImageSubresourceRange range{ aspect, 0, 1, 0, arr.arrayLayers };
                            EmitImageBarrier(cmd, arr.image, range, layerStates[0], next);
                        }
                    } else {
                        for (std::uint32_t layer = 0; layer < arr.arrayLayers; ++layer) {
                            const TextureArraySubresourceDecision decision =
                                DecideTextureArrayLayerTransition(layerStates[layer], aspect, layer, next);
                            if (decision.requiresBarrier) {
                                EmitImageBarrier(cmd, arr.image, decision.range, layerStates[layer], next);
                            }
                        }
                    }

                    for (ResourceState& layerState : layerStates) {
                        layerState = next;
                    }
                }
                break;
            }
            }
        },
        [&](BufferHandle bufferHandle) {
            EnsureBufferResolved(bufferHandle.index, input, physicalBuffers);
            PhysicalBuffer& buf = physicalBuffers[bufferHandle.index];
            const ResourceState next = RequiredStateFor(usage.access, false);
            if (RequiresBarrier(buf.state, next)) {
                EmitBufferBarrier(cmd, buf.buffer, 0, buf.size, buf.state, next);
            }
            buf.state = next;
        });
}

void RenderGraph::ReportSkippedOnce(
    const PassRecord& pass, std::optional<std::uint32_t> binding, const char* reason)
{
    const std::string passName = pass.name != nullptr ? pass.name : "<unnamed>";
    const std::string key = passName + (binding ? "#" + std::to_string(*binding) : std::string("#pass"));
    switch (m_reportedSkippedBindings.Note(key)) {
    case BoundedOnceSet<kMaxReportedSkippedBindings>::Result::First:
        GTE_LOG_ERROR("RenderGraph",
            "Pass \"" + passName + "\" skipped" + (binding ? " binding " + std::to_string(*binding) : std::string())
                + ": " + reason);
        break;
    case BoundedOnceSet<kMaxReportedSkippedBindings>::Result::Overflow:
        GTE_LOG_ERROR("RenderGraph", "Too many distinct skipped bindings; further reports are suppressed.");
        break;
    default:
        break;
    }
}

// Resolves a pass's Bind*() declarations into descriptor writes. All-or-nothing: one unresolved binding
// invalidates the pass, so a stale view from an earlier frame can never stay in the set.
bool RenderGraph::BuildDescriptorWrites(const PassRecord& pass, const std::vector<PhysicalTexture>& physicalTextures,
    const std::vector<PhysicalBuffer>& physicalBuffers, std::vector<ComputeDescriptorWrite>& writes)
{
    writes.reserve(pass.bindings.size());
    bool complete = true;
    const auto skip = [&](const PendingBinding& binding, const char* reason) {
        ReportSkippedOnce(pass, binding.binding, reason);
        complete = false;
    };

    const auto resolvedTexture = [&](TextureHandle handle) -> const PhysicalTexture* {
        if (!handle.IsValid() || handle.index >= physicalTextures.size() || !physicalTextures[handle.index].resolved) {
            return nullptr;
        }
        return &physicalTextures[handle.index];
    };

    for (const PendingBinding& binding : pass.bindings) {
        switch (binding.kind) {
        case BindingKind::StorageBuffer: {
            VkBuffer buffer = VK_NULL_HANDLE;
            if (binding.bufferHandle.IsValid() && binding.bufferHandle.index < physicalBuffers.size()
                && physicalBuffers[binding.bufferHandle.index].resolved) {
                buffer = physicalBuffers[binding.bufferHandle.index].buffer;
            } else if (binding.bufferObjectHandle != BufferObjectHandle::Invalid) {
                buffer = Native(binding.bufferObjectHandle);
            }
            if (buffer == VK_NULL_HANDLE) {
                skip(binding, "storage buffer handle is invalid or unresolved.");
                break;
            }
            writes.push_back(
                ComputeDescriptorWrite::StorageBuffer(binding.binding, buffer, binding.bufferOffset, binding.bufferRange));
            break;
        }
        case BindingKind::UniformBuffer: {
            const VkBuffer buffer = binding.bufferObjectHandle != BufferObjectHandle::Invalid
                ? Native(binding.bufferObjectHandle)
                : VK_NULL_HANDLE;
            if (buffer == VK_NULL_HANDLE) {
                skip(binding, "uniform buffer handle is invalid.");
                break;
            }
            writes.push_back(
                ComputeDescriptorWrite::UniformBuffer(binding.binding, buffer, binding.bufferOffset, binding.bufferRange));
            break;
        }
        case BindingKind::CombinedImageSampler: {
            const PhysicalTexture* tex = resolvedTexture(binding.textureHandle);
            if (tex == nullptr) {
                skip(binding, "sampled texture handle is invalid or unresolved.");
                break;
            }
            if (binding.textureAspect == TextureAspect::Depth) {
                if (!tex->hasDepth || tex->target.depthImageView == VK_NULL_HANDLE) {
                    skip(binding, "texture has no depth view.");
                    break;
                }
                if (tex->depthSampler == VK_NULL_HANDLE) {
                    skip(binding, "texture has no depth sampler.");
                    break;
                }
                writes.push_back(ComputeDescriptorWrite::CombinedImageSampler(
                    binding.binding, tex->target.depthImageView, tex->depthSampler));
                break;
            }
            // A volume has no sampler of its own, so its binding must name one explicitly.
            const VkSampler sampler =
                (binding.explicitSampler != SamplerHandle::Invalid) ? Native(binding.explicitSampler) : tex->sampler;
            if (sampler == VK_NULL_HANDLE) {
                skip(binding, "texture has no sampler and none was given.");
                break;
            }
            writes.push_back(
                ComputeDescriptorWrite::CombinedImageSampler(binding.binding, tex->target.imageView, sampler));
            break;
        }
        case BindingKind::CombinedImageViewSampler: {
            if (binding.explicitImageView == ImageViewHandle::Invalid
                || binding.explicitSampler == SamplerHandle::Invalid) {
                skip(binding, "image view or sampler handle is invalid.");
                break;
            }
            writes.push_back(ComputeDescriptorWrite::CombinedImageSampler(
                binding.binding, Native(binding.explicitImageView), Native(binding.explicitSampler)));
            break;
        }
        case BindingKind::StorageImage: {
            const PhysicalTexture* tex = resolvedTexture(binding.textureHandle);
            if (tex == nullptr) {
                skip(binding, "storage image texture handle is invalid or unresolved.");
                break;
            }
            if (tex->target.imageView == VK_NULL_HANDLE) {
                skip(binding, "storage image has no view.");
                break;
            }
            writes.push_back(ComputeDescriptorWrite::StorageImage(binding.binding, tex->target.imageView));
            break;
        }
        }
    }

    return complete;
}

DescriptorSetHandle RenderGraph::ResolveComputeBindingDescriptorSet(const PassRecord& pass,
    const std::vector<PhysicalTexture>& physicalTextures, const std::vector<PhysicalBuffer>& physicalBuffers)
{
    if (pass.bindings.empty()) {
        return DescriptorSetHandle::Invalid;
    }
    const VkDescriptorSetLayout layout = Native(pass.computeBindingLayout);
    if (layout == VK_NULL_HANDLE) {
        ReportSkippedOnce(pass, std::nullopt, "Bind*() used without BindDescriptorSetLayout().");
        return DescriptorSetHandle::Invalid;
    }
    std::vector<ComputeDescriptorWrite> writes;
    if (!BuildDescriptorWrites(pass, physicalTextures, physicalBuffers, writes)) {
        return DescriptorSetHandle::Invalid;
    }

    const DescriptorSetHandle set =
        m_computeBindingCache.Resolve(pass.name, pass.viewScope, layout, writes, pass.bindingInstance);
    if (set == DescriptorSetHandle::Invalid) {
        ReportSkippedOnce(pass, std::nullopt, "descriptor set could not be allocated.");
    }
    return set;
}

// Constructs PassContext provided to the pass execute callback.
PassContext RenderGraph::BuildPassContext(VkCommandBuffer cmd, std::vector<PhysicalTexture>& physicalTextures,
    std::vector<PhysicalBuffer>& physicalBuffers, DrawStats& passDrawStats)
{
    PassContext ctx;
    ctx.m_cmd = cmd;
    ctx.textures = &physicalTextures;
    ctx.buffers = &physicalBuffers;
    ctx.renderer = m_renderer;
    ctx.eventSink = m_frameDebuggerEventSink;
    ctx.onceLog = &m_onceLog;
    ctx.recordDraw.drawStats = &passDrawStats;
    ctx.recordIndirectDraw.drawStats = &passDrawStats;
    return ctx;
}

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

PassContext::ResolvedImage PassContext::resolveColorImage(TextureHandle handle) const noexcept
{
    if (textures != nullptr && handle.index < textures->size() && (*textures)[handle.index].resolved
        && (*textures)[handle.index].dimension == TextureDimension::Tex2D) {
        const RenderGraph::PhysicalTexture& tex = (*textures)[handle.index];
        return ResolvedImage{ tex.target.image, tex.target.extent };
    }
    return ResolvedImage{};
}

PassContext::ResolvedVolumeTexture PassContext::resolveVolumeTexture(TextureHandle handle) const noexcept
{
    if (textures != nullptr && handle.index < textures->size() && (*textures)[handle.index].resolved
        && (*textures)[handle.index].dimension == TextureDimension::Tex3D) {
        return ResolvedVolumeTexture{ (*textures)[handle.index].target.imageView };
    }
    return ResolvedVolumeTexture{};
}

PassContext::ResolvedTextureArray PassContext::resolveTextureArray(TextureHandle handle) const noexcept
{
    if (textures != nullptr && handle.index < textures->size() && (*textures)[handle.index].resolved
        && (*textures)[handle.index].dimension != TextureDimension::Tex2D
        && (*textures)[handle.index].dimension != TextureDimension::Tex3D) {
        return ResolvedTextureArray{ (*textures)[handle.index].arrayTarget.imageView };
    }
    return ResolvedTextureArray{};
}

PassContext::ResolvedTextureArrayLayer PassContext::resolveArrayLayer(
    TextureHandle handle, std::uint32_t layerIndex) const noexcept
{
    if (textures != nullptr && handle.index < textures->size() && (*textures)[handle.index].resolved
        && (*textures)[handle.index].dimension != TextureDimension::Tex2D
        && (*textures)[handle.index].dimension != TextureDimension::Tex3D) {
        const TextureArrayTarget& arr = (*textures)[handle.index].arrayTarget;
        return ResolvedTextureArrayLayer{ arr.LayerView(layerIndex), arr.image };
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

// Builds VkRenderingAttachmentInfo descriptors for all color attachments.
std::vector<VkRenderingAttachmentInfo> RenderGraph::BuildColorAttachmentInfos(const PassRecord& pass,
    const std::vector<PhysicalTexture>& physicalTextures, std::vector<VkExtent2D>& outResolvedExtents) const
{
    std::vector<VkRenderingAttachmentInfo> colorAttachmentInfos;
    colorAttachmentInfos.reserve(pass.colorAttachments.size());
    outResolvedExtents.reserve(pass.colorAttachments.size());

    for (const ColorAttachmentDesc& desc : pass.colorAttachments) {
        assert(desc.handle.index < physicalTextures.size() &&
            physicalTextures[desc.handle.index].resolved &&
            "RenderGraph::ExecuteCompiledGraph: color attachment was never resolved.");
        const PhysicalTexture& colorTex = physicalTextures[desc.handle.index];
        outResolvedExtents.push_back(colorTex.target.extent);

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

    // Append color array layer attachment if present.
    if (pass.arrayLayerAttachment.has_value()) {
        const ArrayLayerAttachmentDesc& arrDesc = *pass.arrayLayerAttachment;
        assert(arrDesc.handle.index < physicalTextures.size() &&
            physicalTextures[arrDesc.handle.index].resolved &&
            "RenderGraph::BuildColorAttachmentInfos: arrayLayerAttachment was never resolved.");
        const TextureArrayTarget& arr = physicalTextures[arrDesc.handle.index].arrayTarget;
        if (!arr.hasDepth) {
            outResolvedExtents.push_back(arr.extent);

            VkRenderingAttachmentInfo colorAttachment{};
            colorAttachment.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
            colorAttachment.imageView = arr.LayerView(arrDesc.layerIndex);
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

// Builds VkRenderingAttachmentInfo for depth attachment (standard or array layer).
RenderGraph::DepthAttachmentResult RenderGraph::BuildDepthAttachmentInfo(
    const PassRecord& pass, const std::vector<PhysicalTexture>& physicalTextures, TextureHandle depthHandle) const
{
    DepthAttachmentResult result;

    assert((!depthHandle.IsValid() || !pass.arrayLayerAttachment.has_value() ||
               pass.arrayLayerAttachment->handle.index >= physicalTextures.size() ||
               !physicalTextures[pass.arrayLayerAttachment->handle.index].arrayTarget.hasDepth) &&
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
        assert(arrDesc.handle.index < physicalTextures.size() &&
            physicalTextures[arrDesc.handle.index].resolved &&
            "RenderGraph::BuildDepthAttachmentInfo: pass.arrayLayerAttachment was never resolved.");
        const TextureArrayTarget& arr = physicalTextures[arrDesc.handle.index].arrayTarget;
        if (arr.hasDepth) {
            result.extent = arr.extent;

            VkRenderingAttachmentInfo depthAttachment{};
            depthAttachment.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
            depthAttachment.imageView = arr.LayerView(arrDesc.layerIndex);
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

void RenderGraph::UpsertDebugTextureSnapshot(
    ExecuteTimingMode timingMode, const char* name, const PhysicalTexture& tex)
{
    DebugTextureSnapshot snapshot;
    snapshot.name = name;
    snapshot.regime = timingMode;
    snapshot.target = tex.target;
    snapshot.hasDepth = tex.hasDepth;
    snapshot.sampler = tex.sampler;
    snapshot.depthSampler = tex.depthSampler;
    snapshot.colorState = tex.colorState;
    snapshot.depthState = tex.depthState;
    snapshot.lastUpdatedFrameCounter = m_debugTextureFrameCounter;
    m_debugTextures.Upsert(snapshot);
}

// Registers resolved 2D textures with the debug inspector.
void RenderGraph::RegisterDebugTextureSnapshots(ExecuteTimingMode timingMode, const CompiledGraphInput& input,
    const std::vector<PhysicalTexture>& physicalTextures)
{
    for (std::size_t i = 0; i < physicalTextures.size(); ++i) {
        const PhysicalTexture& tex = physicalTextures[i];
        if (!tex.resolved || tex.dimension != TextureDimension::Tex2D) {
            continue;
        }
        const char* name = input.textures[i].name;
        if (name == nullptr || name[0] == '\0') {
            continue;
        }
        UpsertDebugTextureSnapshot(timingMode, name, tex);
    }
}

// Registers resolved 3D volume textures with the debug inspector.
void RenderGraph::RegisterDebugVolumeTextureSnapshots(ExecuteTimingMode timingMode, const CompiledGraphInput& input,
    const std::vector<PhysicalTexture>& physicalTextures)
{
    for (std::size_t i = 0; i < physicalTextures.size(); ++i) {
        const PhysicalTexture& vol = physicalTextures[i];
        if (!vol.resolved || vol.dimension != TextureDimension::Tex3D) {
            continue;
        }
        const char* name = input.textures[i].name;
        if (name == nullptr || name[0] == '\0') {
            continue;
        }

        DebugVolumeTextureSnapshot snapshot;
        snapshot.name = name;
        snapshot.regime = timingMode;
        snapshot.target.image = vol.target.image;
        snapshot.target.imageView = vol.target.imageView;
        snapshot.target.extent = VkExtent3D{ vol.target.extent.width, vol.target.extent.height, vol.depthSlices };
        snapshot.target.format = vol.target.format;
        snapshot.state = vol.colorState;
        snapshot.lastUpdatedFrameCounter = m_debugTextureFrameCounter;
        m_debugVolumeTextures.Upsert(snapshot);
    }
}

void RenderGraph::ExecuteCompiledGraph(VkCommandBuffer cmd, ExecuteTimingMode timingMode, CompiledGraphInput input,
    const std::vector<TextureHandle>& finalOutputs)
{
    const bool isPipelined = (timingMode == ExecuteTimingMode::PipelinedDeferredReadback);

    // A synchronous execution starts a new frame: unclaim pool entries and free the stale ones.
    if (!isPipelined) {
        m_resourcePool.BeginFrame(m_persistentResourceFrameCounter);
        const std::vector<VkImage> evicted = m_resourcePool.TakeEvictedImages();
        if (!evicted.empty()) {
            m_debugTextures.RemoveIf([&evicted](const DebugTextureSnapshot& snapshot) {
                return std::find(evicted.begin(), evicted.end(), snapshot.target.image) != evicted.end();
            });
        }
        ++m_debugTextureFrameCounter;
    }

    // Read back pipelined GPU timing data from completed frames.
    std::uint32_t pipelinedBufferIndex = 0;
    if (isPipelined) {
        pipelinedBufferIndex = m_pipelinedFrameCounter % kGpuTimingFramesInFlight;
        for (std::uint32_t s = 0; s < m_pipelinedTimingSlots.AssignedCount(); ++s) {
            if (!m_pipelinedHasWritten[s][pipelinedBufferIndex]) {
                continue;
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

    // Begin per-pass write target snapshot capture.
    m_eventSnapshotPool.BeginFrame(isPipelined, pipelinedBufferIndex);
    m_eventSnapshotsThisFrame.clear();

    const CompiledGraph compiled = Compile(input, std::span<const TextureHandle>(finalOutputs));

    std::vector<PhysicalTexture> physicalTextures(input.textures.size());
    std::vector<PhysicalBuffer> physicalBuffers(input.buffers.size());

    RenderGraphNameSlotTable& timingSlots = isPipelined ? m_pipelinedTimingSlots : m_synchronousTimingSlots;

    for (const PassHandle& passHandle : compiled.executionOrder) {
        PassRecord& pass = input.passes[passHandle.index];

        // Apply resource barriers for reads before writes.
        for (const ResourceUsage& usage : pass.reads) {
            ApplyUsageBarrierIfNeeded(cmd, usage, input, physicalTextures, physicalBuffers);
        }
        for (const ResourceUsage& usage : pass.writes) {
            AppliedBarrierInfo barrierInfo;
            ApplyUsageBarrierIfNeeded(cmd, usage, input, physicalTextures, physicalBuffers, &barrierInfo);
            if (barrierInfo.applied && m_debugMetadataSink != nullptr) {
                m_debugMetadataSink->OnResourceBarrierApplied(passHandle.index, ResourceUsageName(usage, input),
                    barrierInfo.oldLayout, barrierInfo.newLayout);
            }
        }

        const std::int32_t timingSlot = timingSlots.AssignOrGetSlot(pass.name);

        // Warn once if the fixed timing slot budget is exhausted.
        if (timingSlot == kNoNameSlot && timingSlots.JustOverflowed()) {
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

        // Record start timestamp after barriers execute.
        m_timestampPool.WriteBegin(cmd, isPipelined, pipelinedBufferIndex, timingSlot);

        // Find depth write texture handle, if any. Per-layer array writes and non-2D textures are attached
        // through pass.arrayLayerAttachment instead.
        TextureHandle depthHandle;
        for (const ResourceUsage& usage : pass.writes) {
            if (usage.kind != ResourceKind::Texture || usage.arrayLayerIndex.has_value()) {
                continue;
            }
            if (TargetsDepthState(usage.access)
                && physicalTextures[usage.texture.index].dimension == TextureDimension::Tex2D) {
                depthHandle = usage.texture;
            }
        }

        const bool hasAnyAttachment =
            !pass.colorAttachments.empty() || depthHandle.IsValid() || pass.arrayLayerAttachment.has_value();

        DrawStats passDrawStats;
        PassContext ctx = BuildPassContext(cmd, physicalTextures, physicalBuffers, passDrawStats);

        ctx.computeDescriptorSet = ResolveComputeBindingDescriptorSet(pass, physicalTextures, physicalBuffers);

        // A refused pass or one with an unresolved descriptor set must not record: barriers already ran.
        bool skipExecute = pass.skipExecute;
        if (pass.skipExecute) {
            ReportSkippedOnce(pass, std::nullopt, "duplicate (pass name, view) binding identity in one frame.");
        } else if (!pass.bindings.empty() && ctx.computeDescriptorSet == DescriptorSetHandle::Invalid) {
            skipExecute = true;
        }

        bool didBeginRendering = false;
        if (hasAnyAttachment) {
            std::vector<VkExtent2D> resolvedExtents;
            const std::vector<VkRenderingAttachmentInfo> colorAttachmentInfos =
                BuildColorAttachmentInfos(pass, physicalTextures, resolvedExtents);

            // Verify that all color attachments match in resolution.
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

            const DepthAttachmentResult depthResult = BuildDepthAttachmentInfo(pass, physicalTextures, depthHandle);

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

            if (!pass.colorAttachments.empty()) {
                const PhysicalTexture& colorTex = physicalTextures[pass.colorAttachments.front().handle.index];
                ctx.writeTargetColorImage = colorTex.target.image;
                ctx.writeTargetColorFormat = colorTex.target.format;
            }
            if (depthResult.info.has_value() && depthHandle.IsValid()) {
                const PhysicalTexture& depthTex = physicalTextures[depthHandle.index];
                ctx.writeTargetDepthImage = depthTex.target.depthImage;
                ctx.writeTargetDepthFormat = depthTex.target.depthFormat;
            }
            didBeginRendering = true;
        }

        if (pass.execute) {
            if (!skipExecute) {
                pass.execute(ctx);
            }
        } else if (pass.kind == PassKind::Blit && pass.blitCommand.has_value()) {
            // Execute image copy/blit pass.
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

            assert(((!spec.srcIsDepth && !spec.dstIsDepth) || m_renderer->SupportsDepthBlit())
                && "RenderGraph::ExecuteCompiledGraph: device does not support depth blitting.");

            const VkFilter effectiveFilter = ToVkFilter(ResolveEffectiveBlitFilter(spec));

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

        // Record end timestamp.
        m_timestampPool.WriteEnd(cmd, isPipelined, pipelinedBufferIndex, timingSlot);
        if (isPipelined && timingSlot != kNoNameSlot) {
            m_pipelinedHasWritten[static_cast<std::size_t>(timingSlot)][pipelinedBufferIndex] = true;
        }

        // Capture write targets for debugging when enabled.
        const bool eventSnapshotKillSwitchEnabled = m_renderPassToggleRegistry == nullptr
            || m_renderPassToggleRegistry->IsEnabled("FrameDebuggerEventSnapshot");
        if (m_eventSnapshotPool.IsCaptureEnabled() && eventSnapshotKillSwitchEnabled) {
            for (const ResourceUsage& usage : pass.writes) {
                if (usage.kind != ResourceKind::Texture) {
                    continue;
                }
                PhysicalTexture& tex = physicalTextures[usage.texture.index];
                if (tex.dimension != TextureDimension::Tex2D) {
                    continue; // Only plain 2D textures are captured per event.
                }
                const bool isDepth = TargetsDepthState(usage.access);
                const VkImage image = isDepth ? tex.target.depthImage : tex.target.image;
                if (image == VK_NULL_HANDLE) {
                    continue;
                }
                const VkFormat format = isDepth ? tex.target.depthFormat : tex.target.format;
                const VkImageAspectFlags aspect = isDepth
                    ? (VK_IMAGE_ASPECT_DEPTH_BIT | (tex.target.depthHasStencil ? VK_IMAGE_ASPECT_STENCIL_BIT : 0))
                    : static_cast<VkImageAspectFlags>(VK_IMAGE_ASPECT_COLOR_BIT);
                ResourceState& state = isDepth ? tex.depthState : tex.colorState;

                const std::int32_t eventSlot =
                    m_eventSnapshotPool.CaptureAfterPass(cmd, image, tex.target.extent, format, aspect, state);
                m_eventSnapshotsThisFrame.push_back({ pass.name, usage.texture, eventSlot,
                    m_eventSnapshotPool.RegionFor(isPipelined, pipelinedBufferIndex) });
            }
        }

        if (m_frameDebuggerEventSink != nullptr) {
            m_frameDebuggerEventSink->FlushPendingCapture(RecordingAccess::Wrap(cmd, m_renderer));
        }

        UpdateDrawStatsFor(pass.name, passDrawStats);
    }

    if (isPipelined) {
        ++m_pipelinedFrameCounter;
    }

    RegisterDebugTextureSnapshots(timingMode, input, physicalTextures);
    RegisterDebugVolumeTextureSnapshots(timingMode, input, physicalTextures);

    // Sync final image layout back to external owning RenderTextures.
    for (std::size_t i = 0; i < input.textures.size(); ++i) {
        if (!physicalTextures[i].resolved) {
            continue;
        }
        const TextureImportInfo& importInfo = input.textures[i].importInfo;
        if (importInfo.isImported && importInfo.trackedOwner != nullptr) {
            importInfo.trackedOwner->SetCurrentState(physicalTextures[i].colorState);
        }
    }

    // Record layouts of persistent cached textures and flush pending resizes.
    for (const PersistentTextureUse& use : input.persistentCacheTextures) {
        if (use.handle.index >= physicalTextures.size() || !physicalTextures[use.handle.index].resolved) {
            continue;
        }
        m_persistentResourceCache.RecordFinalLayout(use.slot, physicalTextures[use.handle.index].colorState.layout);
    }
    if (!isPipelined) {
        const std::vector<PersistentSlotRef> resized = m_persistentResourceCache.FlushPendingResizes();
        // The resize replaced the image behind the snapshot registered above: re-register it.
        for (const PersistentSlotRef& slot : resized) {
            for (const PersistentTextureUse& use : input.persistentCacheTextures) {
                if (!(use.slot == slot) || use.handle.index >= physicalTextures.size()) {
                    continue;
                }
                PhysicalTexture& tex = physicalTextures[use.handle.index];
                const RenderTexture* renderTexture = m_persistentResourceCache.TextureFor(slot);
                const char* name = input.textures[use.handle.index].name;
                if (!tex.resolved || renderTexture == nullptr || name == nullptr || name[0] == '\0') {
                    continue;
                }
                tex.target = renderTexture->Target();
                tex.sampler = renderTexture->Sampler();
                tex.colorState = ResourceState{ VK_IMAGE_LAYOUT_UNDEFINED, VK_PIPELINE_STAGE_2_TOP_OF_PIPE_BIT,
                    VK_ACCESS_2_NONE };
                UpsertDebugTextureSnapshot(timingMode, name, tex);
            }
        }
    }

    // Build the debug snapshot for the editor frame inspector.
    const bool timingSlotBudgetExhausted = timingSlots.AssignedCount() >= timingSlots.SlotBudget();

    std::function<bool(std::size_t, PassDebugMetadata&)> metadataLookup;
    if (m_debugMetadataProvider != nullptr) {
        IPassDebugMetadataProvider* provider = m_debugMetadataProvider;
        metadataLookup = [provider](std::size_t declarationIndex, PassDebugMetadata& outMetadata) {
            return provider->QueryPassDebugMetadata(declarationIndex, outMetadata);
        };
    }

    std::function<bool(std::size_t, const std::string&, std::string&)> barrierLabelLookup;
    if (m_debugMetadataProvider != nullptr) {
        IPassDebugMetadataProvider* provider = m_debugMetadataProvider;
        barrierLabelLookup =
            [provider](std::size_t declarationIndex, const std::string& resourceName, std::string& outLabel) {
                return provider->QueryBarrierTransitionLabel(declarationIndex, resourceName, outLabel);
            };
    }

    RenderGraphSnapshot snapshot = BuildRenderGraphSnapshot(compiled, input,
        [this](const char* name) { return LastKnownStatsFor(name); }, timingSlotBudgetExhausted, metadataLookup,
        barrierLabelLookup);
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

std::optional<DebugTextureSnapshot> RenderGraph::DebugTextureSnapshotFor(const std::string& name) const
{
    return m_debugTextures.FindByName(name);
}

std::vector<DebugTextureSnapshot> RenderGraph::ListDebugTextures() const
{
    return m_debugTextures.ListAll();
}

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
