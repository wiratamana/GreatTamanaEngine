#include "RenderGraphBuilder.h"

#include "RenderGraph.h"
#include "../../Core/Logging.h"
#include "../GraphicsHandleRegistry.h" // Native(SamplerHandle)/Native(BufferObjectHandle) - ResolveImportedTextureSamplers()/MintPersistentHandle() below.
#include "../VulkanFormatInfo.h"

#include <cstring>
#include <exception>
#include <string>

namespace gte::rg {

// --- RenderGraphBuilder::PassBuilder ---------------------------------------

void RenderGraphBuilder::PassBuilder::ReadTexture(TextureHandle handle, ResourceAccess access, bool isDepthResource)
{
    m_pass.reads.push_back(ResourceUsage::ForTexture(handle, access, isDepthResource));
}

void RenderGraphBuilder::PassBuilder::WriteColorAttachment(
    TextureHandle handle, const std::optional<std::array<float, 4>>& clearColor)
{
    assert(m_pass.colorAttachments.size() < kMaxColorAttachments &&
        "RenderGraphBuilder::PassBuilder::WriteColorAttachment: exceeded kMaxColorAttachments per pass");

    m_pass.writes.push_back(ResourceUsage::ForTexture(handle, ResourceAccess::ColorAttachmentWrite));

    if (clearColor.has_value()) {
        m_pass.colorClearValue = clearColor;
    }

    ColorAttachmentDesc desc;
    desc.handle = handle;
    desc.clearColor = clearColor;
    m_pass.colorAttachments.push_back(desc);
}

void RenderGraphBuilder::PassBuilder::WriteDepthStencilAttachment(TextureHandle handle, std::optional<float> clearDepth)
{
    m_pass.writes.push_back(ResourceUsage::ForTexture(handle, ResourceAccess::DepthStencilAttachmentReadWrite));
    if (clearDepth.has_value()) {
        m_pass.depthClearValue = clearDepth;
    }
}

void RenderGraphBuilder::PassBuilder::WriteTexture(TextureHandle handle, ResourceAccess access, bool isDepthResource)
{
    m_pass.writes.push_back(ResourceUsage::ForTexture(handle, access, isDepthResource));
}

void RenderGraphBuilder::PassBuilder::ReadBuffer(BufferHandle handle, ResourceAccess access)
{
    m_pass.reads.push_back(ResourceUsage::ForBuffer(handle, access));
}

void RenderGraphBuilder::PassBuilder::WriteBuffer(BufferHandle handle, ResourceAccess access)
{
    m_pass.writes.push_back(ResourceUsage::ForBuffer(handle, access));
}

void RenderGraphBuilder::PassBuilder::WriteArrayLayer(TextureHandle handle, std::uint32_t layerIndex,
    ResourceAccess access, std::optional<std::array<float, 4>> clearColor, std::optional<float> clearDepth)
{
    assert(!m_pass.arrayLayerAttachment.has_value() &&
        "RenderGraphBuilder::PassBuilder::WriteArrayLayer: a pass may declare at most one "
        "array-layer attachment - use a separate pass per layer.");

    m_pass.writes.push_back(ResourceUsage::ForTextureLayer(handle, layerIndex, access));

    ArrayLayerAttachmentDesc desc;
    desc.handle = handle;
    desc.layerIndex = layerIndex;
    desc.clearColor = clearColor;
    desc.clearDepth = clearDepth;
    m_pass.arrayLayerAttachment = desc;
}

// --- Compute-binding vocabulary ----------------------------------------

void RenderGraphBuilder::PassBuilder::BindDescriptorSetLayout(DescriptorSetLayoutHandle layout)
{
    m_pass.computeBindingLayout = layout;
}

void RenderGraphBuilder::PassBuilder::SetBindingInstance(std::uint64_t instance)
{
    m_pass.bindingInstance = instance;
}

void RenderGraphBuilder::PassBuilder::BindStorageBuffer(
    std::uint32_t binding, BufferHandle handle, uint64_t offset, uint64_t range)
{
    PendingBinding pending;
    pending.binding = binding;
    pending.kind = BindingKind::StorageBuffer;
    pending.bufferHandle = handle;
    pending.bufferOffset = offset;
    pending.bufferRange = range;
    m_pass.bindings.push_back(pending);
}

void RenderGraphBuilder::PassBuilder::BindStorageBuffer(
    std::uint32_t binding, BufferObjectHandle handle, uint64_t offset, uint64_t range)
{
    PendingBinding pending;
    pending.binding = binding;
    pending.kind = BindingKind::StorageBuffer;
    pending.bufferObjectHandle = handle;
    pending.bufferOffset = offset;
    pending.bufferRange = range;
    m_pass.bindings.push_back(pending);
}

void RenderGraphBuilder::PassBuilder::BindUniformBuffer(
    std::uint32_t binding, BufferObjectHandle handle, uint64_t offset, uint64_t range)
{
    PendingBinding pending;
    pending.binding = binding;
    pending.kind = BindingKind::UniformBuffer;
    pending.bufferObjectHandle = handle;
    pending.bufferOffset = offset;
    pending.bufferRange = range;
    m_pass.bindings.push_back(pending);
}

void RenderGraphBuilder::PassBuilder::BindCombinedImageSampler(
    std::uint32_t binding, TextureHandle handle, SamplerHandle sampler)
{
    PendingBinding pending;
    pending.binding = binding;
    pending.kind = BindingKind::CombinedImageSampler;
    pending.textureHandle = handle;
    pending.explicitSampler = sampler;
    m_pass.bindings.push_back(pending);
}

void RenderGraphBuilder::PassBuilder::BindCombinedImageSampler(
    std::uint32_t binding, TextureHandle handle, TextureAspect aspect)
{
    PendingBinding pending;
    pending.binding = binding;
    pending.kind = BindingKind::CombinedImageSampler;
    pending.textureHandle = handle;
    pending.textureAspect = aspect;
    m_pass.bindings.push_back(pending);
}

void RenderGraphBuilder::PassBuilder::BindCombinedImageSampler(
    std::uint32_t binding, ImageViewHandle view, SamplerHandle sampler)
{
    assert(sampler != SamplerHandle::Invalid &&
        "RenderGraphBuilder::PassBuilder::BindCombinedImageSampler: an ImageViewHandle binding requires an "
        "explicit sampler - there is no graph-tracked resource here to fall back to.");

    PendingBinding pending;
    pending.binding = binding;
    pending.kind = BindingKind::CombinedImageViewSampler;
    pending.explicitImageView = view;
    pending.explicitSampler = sampler;
    m_pass.bindings.push_back(pending);
}

void RenderGraphBuilder::PassBuilder::BindStorageImage(std::uint32_t binding, TextureHandle handle)
{
    PendingBinding pending;
    pending.binding = binding;
    pending.kind = BindingKind::StorageImage;
    pending.textureHandle = handle;
    m_pass.bindings.push_back(pending);
}

// --- Fused declarations ----------------------------------------------------

void RenderGraphBuilder::PassBuilder::SampleTexture(std::uint32_t binding, TextureHandle handle, SamplerHandle sampler)
{
    ReadTexture(handle, ResourceAccess::ShaderRead, /*isDepthResource=*/false);
    BindCombinedImageSampler(binding, handle, sampler);
}

void RenderGraphBuilder::PassBuilder::SampleDepth(std::uint32_t binding, TextureHandle handle)
{
    ReadTexture(handle, ResourceAccess::ShaderRead, /*isDepthResource=*/true);
    BindCombinedImageSampler(binding, handle, TextureAspect::Depth);
}

void RenderGraphBuilder::PassBuilder::WriteStorageImage(std::uint32_t binding, TextureHandle handle)
{
    WriteTexture(handle, ResourceAccess::ComputeShaderWrite);
    BindStorageImage(binding, handle);
}

void RenderGraphBuilder::PassBuilder::ReadStorageBuffer(std::uint32_t binding, BufferObjectHandle handle)
{
    BindStorageBuffer(binding, handle);
}

void RenderGraphBuilder::PassBuilder::ReadStorageBuffer(std::uint32_t binding, BufferHandle handle)
{
    ReadBuffer(handle, ResourceAccess::ComputeShaderRead);
    BindStorageBuffer(binding, handle);
}

void RenderGraphBuilder::PassBuilder::WriteStorageBuffer(std::uint32_t binding, BufferHandle handle)
{
    WriteBuffer(handle, ResourceAccess::ComputeShaderWrite);
    BindStorageBuffer(binding, handle);
}

// The read comes first: the executor applies read barriers before write barriers.
void RenderGraphBuilder::PassBuilder::ReadWriteStorageBuffer(std::uint32_t binding, BufferHandle handle)
{
    ReadBuffer(handle, ResourceAccess::ComputeShaderRead);
    WriteBuffer(handle, ResourceAccess::ComputeShaderWrite);
    BindStorageBuffer(binding, handle);
}

// --- RenderGraphBuilder ------------------------------------------------

void RenderGraphBuilder::ClaimBindingIdentity(std::size_t passIndex)
{
    PassRecord& pass = m_passes[passIndex];
    if (pass.computeBindingLayout == DescriptorSetLayoutHandle::Invalid) {
        return;
    }
    for (const std::size_t other : m_bindingPassIndices) {
        const PassRecord& owner = m_passes[other];
        if (owner.bindingInstance == pass.bindingInstance && owner.viewScope == pass.viewScope
            && std::strcmp(owner.name, pass.name) == 0) {
            pass.computeBindingLayout = DescriptorSetLayoutHandle::Invalid;
            pass.bindings.clear();
            pass.skipExecute = true;
            return;
        }
    }
    m_bindingPassIndices.push_back(passIndex);
}

TextureHandle RenderGraphBuilder::CreateTexture(const char* name, const TextureDesc& desc)
{
    assert(name != nullptr && name[0] != '\0' &&
        "RenderGraphBuilder::CreateTexture requires a non-empty, static-storage-duration name");

    const std::uint32_t index = static_cast<std::uint32_t>(m_textures.size());
    TextureDesc stored = desc;
    if (stored.dimension == TextureDimension::Array2D || stored.dimension == TextureDimension::Cube) {
        stored.hasColor = !stored.hasDepth; // An array is either a color or a depth image.
    }
    m_textures.push_back(TextureSlot{ stored, name, TextureImportInfo{} });
    return TextureHandle{ index, 1 };
}

BufferHandle RenderGraphBuilder::CreateBuffer(const char* name, const BufferDesc& desc)
{
    assert(name != nullptr && name[0] != '\0' &&
        "RenderGraphBuilder::CreateBuffer requires a non-empty, static-storage-duration name");

    const std::uint32_t index = static_cast<std::uint32_t>(m_buffers.size());
    m_buffers.push_back(BufferSlot{ desc, name, BufferImportInfo{} });
    return BufferHandle{ index, 1 };
}   

TextureHandle RenderGraphBuilder::ImportTexture(const char* name, const RenderTarget& externalTarget,
    ImageLayout currentLayout, SamplerHandle colorSampler, SamplerHandle depthSampler, RenderTexture* trackedOwner)
{
    assert(name != nullptr && name[0] != '\0' &&
        "RenderGraphBuilder::ImportTexture requires a non-empty, static-storage-duration name");

    const std::uint32_t index = static_cast<std::uint32_t>(m_textures.size());

    TextureDesc desc;
    desc.width = externalTarget.extent.width;
    desc.height = externalTarget.extent.height;
    desc.format = FromVkFormat(externalTarget.format);
    desc.hasDepth = externalTarget.depthImage != VK_NULL_HANDLE;
    desc.hasColor = externalTarget.image != VK_NULL_HANDLE;
    if (desc.hasDepth) {
        desc.depthFormat = FromVkFormat(externalTarget.depthFormat);
    }

    TextureImportInfo importInfo;
    importInfo.isImported = true;
    importInfo.externalTarget = externalTarget;
    importInfo.currentLayout = currentLayout;
    importInfo.colorSampler = colorSampler;
    importInfo.depthSampler = depthSampler;
    importInfo.trackedOwner = trackedOwner;

    m_textures.push_back(TextureSlot{ desc, name, importInfo });
    return TextureHandle{ index, 1 };
}

RenderGraphBuilder::ImportedTextureSamplers RenderGraphBuilder::ResolveImportedTextureSamplers(
    TextureHandle handle) const noexcept
{
    if (handle.index >= m_textures.size() || !m_textures[handle.index].importInfo.isImported) {
        return ImportedTextureSamplers{};
    }
    const TextureImportInfo& importInfo = m_textures[handle.index].importInfo;
    ImportedTextureSamplers resolved{
        Native(importInfo.colorSampler), importInfo.externalTarget.depthImageView, Native(importInfo.depthSampler)
    };
    if (IsResolvedViewMissingItsSampler(resolved.depthImageView, resolved.depthSampler)) {
        assert(false && "RenderGraphBuilder::ResolveImportedTextureSamplers() - non-null depth view with a null sampler");
        GTE_LOG_ERROR("RenderGraphBuilder",
            "ResolveImportedTextureSamplers() - handle index " + std::to_string(handle.index)
                + " resolved a non-null depth view with a null sampler.");
    }
    return resolved;
}

BufferHandle RenderGraphBuilder::Import(const GpuBuffer& buffer, const char* graphName)
{
    return ImportBuffer(graphName != nullptr ? graphName : buffer.Name(), buffer.Handle(), buffer.Size());
}

BufferHandle RenderGraphBuilder::ImportBuffer(const char* name, BufferObjectHandle externalBuffer, uint64_t size)
{
    assert(name != nullptr && name[0] != '\0' &&
        "RenderGraphBuilder::ImportBuffer requires a non-empty, static-storage-duration name");

    const std::uint32_t index = static_cast<std::uint32_t>(m_buffers.size());

    BufferDesc desc;
    desc.size = size;
    desc.usage = BufferUsage::None;

    BufferImportInfo importInfo;
    importInfo.isImported = true;
    importInfo.externalBuffer = externalBuffer;
    importInfo.size = size;

    m_buffers.push_back(BufferSlot{ desc, name, importInfo });
    return BufferHandle{ index, 1 };
}

TextureHandle RenderGraphBuilder::ImportVolumeTexture(
    const char* name, const VolumeTarget& externalVolumeTarget, ImageLayout currentLayout, SamplerHandle sampler)
{
    assert(name != nullptr && name[0] != '\0' &&
        "RenderGraphBuilder::ImportVolumeTexture requires a non-empty, static-storage-duration name");

    const std::uint32_t index = static_cast<std::uint32_t>(m_textures.size());

    TextureDesc desc;
    desc.dimension = TextureDimension::Tex3D;
    desc.width = externalVolumeTarget.extent.width;
    desc.height = externalVolumeTarget.extent.height;
    desc.depth = externalVolumeTarget.extent.depth;
    desc.format = FromVkFormat(externalVolumeTarget.format);

    TextureImportInfo importInfo;
    importInfo.isImported = true;
    importInfo.externalVolumeTarget = externalVolumeTarget;
    importInfo.currentLayout = currentLayout;
    importInfo.colorSampler = sampler;

    m_textures.push_back(TextureSlot{ desc, name, importInfo });
    return TextureHandle{ index, 1 };
}

TextureHandle RenderGraphBuilder::ImportTextureArray(
    const char* name, const TextureArrayTarget& externalTarget, ImageLayout currentLayout)
{
    assert(name != nullptr && name[0] != '\0' &&
        "RenderGraphBuilder::ImportTextureArray requires a non-empty, static-storage-duration name");
    const std::uint32_t index = static_cast<std::uint32_t>(m_textures.size());

    TextureDesc desc;
    desc.dimension = TextureDimension::Array2D;
    desc.width = externalTarget.extent.width;
    desc.height = externalTarget.extent.height;
    desc.arrayLayers = externalTarget.arrayLayers;
    desc.format = FromVkFormat(externalTarget.format);
    desc.hasDepth = externalTarget.hasDepth;
    desc.hasColor = !externalTarget.hasDepth;

    TextureImportInfo importInfo;
    importInfo.isImported = true;
    importInfo.externalArrayTarget = externalTarget;
    importInfo.currentLayout = currentLayout;

    m_textures.push_back(TextureSlot{ desc, name, importInfo });
    return TextureHandle{ index, 1 };
}

void RenderGraphBuilder::KeepTextureOutput(TextureHandle handle)
{
    m_finalTextureOutputs.push_back(handle);
}

void RenderGraphBuilder::KeepBufferOutput(BufferHandle handle)
{
    m_finalBufferOutputs.push_back(handle);
}

void RenderGraphBuilder::AddBlitPass(
    const char* name, const BlitSpec& spec, RenderPassEvent renderPassEvent, ViewScope viewScope, RenderPassCategory category)
{
    assert(name != nullptr && name[0] != '\0' &&
        "RenderGraphBuilder::AddBlitPass requires a non-empty, static-storage-duration pass name");

    m_passes.push_back(PassRecord{});
    PassRecord& pass = m_passes.back();
    pass.name = name;
    pass.kind = PassKind::Blit;
    pass.featureId = CurrentFeatureId();
    pass.viewScope = viewScope;
    pass.renderPassEvent = renderPassEvent;
    pass.blitCommand = spec;

    PassBuilder passBuilder(pass);
    passBuilder.ReadTexture(spec.src, ResourceAccess::TransferSrc, spec.srcIsDepth);
    passBuilder.WriteTexture(spec.dst, ResourceAccess::TransferDst, spec.dstIsDepth);

    const std::string_view owner = CurrentOwningFeatureName();
    if (m_debugMetadataSink != nullptr) {
        m_debugMetadataSink->OnPassDeclared(m_passes.size() - 1, category, RenderPassDrawKind::Blit, owner);
    }
    if (m_passToggleRegistry != nullptr) {
        m_passToggleRegistry->NoteDeclaredWithOwner(name, owner);
    }
}

TextureHandle RenderGraphBuilder::MintPersistentHandle(
    const RenderGraphPersistentResourceCache::ResolvedTexture& resolved)
{
    const TextureHandle handle = ImportTexture(resolved.combinedKey->c_str(), resolved.texture->Target(),
        FromVkImageLayout(resolved.lastKnownLayout), resolved.texture->SamplerObjectHandle());
    m_persistentCacheTextures.push_back(PersistentTextureUse{ handle, resolved.slot });
    return handle;
}

TextureHandle RenderGraphBuilder::GetOrCreatePersistentTexture(
    PersistentTextureCacheToken& token, const char* owner, const char* name, const TextureDesc& desc)
{
    assert(m_persistentCache != nullptr
        && "GetOrCreatePersistentTexture requires a builder obtained through RenderGraph::Execute()");
    if (m_persistentCache == nullptr) {
        GTE_LOG_ERROR("RenderGraphBuilder",
            "GetOrCreatePersistentTexture() called with no persistent cache installed - refusing.");
        return TextureHandle{};
    }

    if (m_persistentCache->IsTokenLive(token)) {
#ifndef NDEBUG
        assert(m_persistentCache->DebugTokenIdentityMatches(token.slot, owner, name)
            && "A persistent token must not be reused across different (owner, name) identities");
#endif
        const std::optional<RenderGraphPersistentResourceCache::ResolvedTexture> resolved =
            m_persistentCache->ResolveFast(token.slot, desc, m_persistentCacheCurrentFrame, m_persistentCacheTimingMode);
        if (!resolved.has_value()) {
            return TextureHandle{}; // Refused, already logged.
        }
        return MintPersistentHandle(*resolved);
    }

    // Slow path: resolve by name, then refresh the token. Creation failures must not reach the frame loop.
    std::optional<RenderGraphPersistentResourceCache::ResolvedTexture> resolved;
    try {
        resolved = m_persistentCache->Resolve(owner, name, desc, m_persistentCacheCurrentFrame, m_persistentCacheTimingMode);
    } catch (const std::exception& e) {
        if (!token.refusalLogged) {
            token.refusalLogged = true;
            GTE_LOG_ERROR("RenderGraphBuilder",
                std::string("Persistent texture \"") + (name != nullptr ? name : "") + "\" could not be created: " + e.what());
        }
        return TextureHandle{};
    }
    if (!resolved.has_value()) {
        return TextureHandle{};
    }
    token.slot = resolved->slot;
    return MintPersistentHandle(*resolved);
}

TextureHandle RenderGraphBuilder::GetOrCreatePersistentTexture(
    PersistentTextureCacheToken& token, const char* name, const TextureDesc& desc)
{
    const char* refusal = nullptr;
    if (!HasOwningFeatureScope()) {
        refusal = "no feature scope is open, so the texture has no owner";
    } else if (desc.format == PixelFormat::Undefined) {
        refusal = "the format is Undefined";
    } else if (desc.width == 0 || desc.height == 0) {
        refusal = "the extent is empty";
    }
    if (refusal != nullptr) {
        if (!token.refusalLogged) {
            token.refusalLogged = true;
            GTE_LOG_ERROR("RenderGraphBuilder",
                std::string("Persistent texture \"") + (name != nullptr ? name : "") + "\" refused: " + refusal + ".");
        }
        return TextureHandle{};
    }
    // std::string storage is NUL-terminated, so the owner is a valid C string for the call.
    return GetOrCreatePersistentTexture(token, m_owningFeatureScope.back().c_str(), name, desc);
}

bool RenderGraphBuilder::IsPersistentRegimePipelined() const noexcept
{
    return m_persistentCacheTimingMode == ExecuteTimingMode::PipelinedDeferredReadback;
}

std::optional<TextureDesc> RenderGraphBuilder::DescOf(TextureHandle handle) const
{
    if (!handle.IsValid() || handle.index >= m_textures.size()) {
        return std::nullopt;
    }
    return m_textures[handle.index].desc;
}

std::optional<BufferDesc> RenderGraphBuilder::DescOfBuffer(BufferHandle handle) const
{
    if (!handle.IsValid() || handle.index >= m_buffers.size()) {
        return std::nullopt;
    }
    return m_buffers[handle.index].desc;
}

CompiledGraphInput RenderGraphBuilder::Finish()
{
    CompiledGraphInput input;
    input.passes = std::move(m_passes);
    input.textures = std::move(m_textures);
    input.buffers = std::move(m_buffers);
    input.finalTextureOutputs = m_finalTextureOutputs;
    input.finalBufferOutputs = std::move(m_finalBufferOutputs);
    input.persistentCacheTextures = std::move(m_persistentCacheTextures);
    return input;
}

} // namespace gte::rg
