#include "RenderGraphBuilder.h"

#include "../../Core/Logging.h"
#include "../GraphicsHandleRegistry.h" // Native(SamplerHandle)/Native(BufferObjectHandle) - ResolveImportedTextureSamplers()/MintPersistentHandle() below.
#include "../VulkanFormatInfo.h"

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

void RenderGraphBuilder::PassBuilder::ReadVolumeTexture(VolumeTextureHandle handle, ResourceAccess access)
{
    m_pass.reads.push_back(ResourceUsage::ForVolumeTexture(handle, access));
}

void RenderGraphBuilder::PassBuilder::WriteVolumeTexture(VolumeTextureHandle handle, ResourceAccess access)
{
    m_pass.writes.push_back(ResourceUsage::ForVolumeTexture(handle, access));
}

void RenderGraphBuilder::PassBuilder::ReadTextureArray(TextureArrayHandle handle, ResourceAccess access)
{
    m_pass.reads.push_back(ResourceUsage::ForTextureArray(handle, access));
}

void RenderGraphBuilder::PassBuilder::WriteTextureArray(TextureArrayHandle handle, ResourceAccess access)
{
    m_pass.writes.push_back(ResourceUsage::ForTextureArray(handle, access));
}

void RenderGraphBuilder::PassBuilder::WriteArrayLayer(TextureArrayHandle handle, std::uint32_t layerIndex,
    ResourceAccess access, std::optional<std::array<float, 4>> clearColor, std::optional<float> clearDepth)
{
    assert(!m_pass.arrayLayerAttachment.has_value() &&
        "RenderGraphBuilder::PassBuilder::WriteArrayLayer: a pass may declare at most one "
        "array-layer attachment - use a separate pass per layer.");

    m_pass.writes.push_back(ResourceUsage::ForTextureArrayLayer(handle, layerIndex, access));

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
    std::uint32_t binding, VolumeTextureHandle handle, SamplerHandle sampler)
{
    assert(sampler != SamplerHandle::Invalid &&
        "RenderGraphBuilder::PassBuilder::BindCombinedImageSampler: a volume texture binding requires an "
        "explicit sampler - the render graph tracks no implicit sampler for a VolumeTextureHandle.");

    PendingBinding pending;
    pending.binding = binding;
    pending.kind = BindingKind::CombinedVolumeImageSampler;
    pending.volumeTextureHandle = handle;
    pending.explicitSampler = sampler;
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

void RenderGraphBuilder::PassBuilder::BindStorageImage(std::uint32_t binding, VolumeTextureHandle handle)
{
    PendingBinding pending;
    pending.binding = binding;
    pending.kind = BindingKind::StorageImage;
    pending.volumeTextureHandle = handle;
    m_pass.bindings.push_back(pending);
}


// --- RenderGraphBuilder ------------------------------------------------

TextureHandle RenderGraphBuilder::CreateTexture(const char* name, const TextureDesc& desc)
{
    assert(name != nullptr && name[0] != '\0' &&
        "RenderGraphBuilder::CreateTexture requires a non-empty, static-storage-duration name");

    const std::uint32_t index = static_cast<std::uint32_t>(m_textures.size());
    m_textures.push_back(TextureSlot{ desc, name, TextureImportInfo{} });
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

VolumeTextureHandle RenderGraphBuilder::ImportVolumeTexture(
    const char* name, const VolumeTarget& externalVolumeTarget, ImageLayout currentLayout)
{
    assert(name != nullptr && name[0] != '\0' &&
        "RenderGraphBuilder::ImportVolumeTexture requires a non-empty, static-storage-duration name");

    const std::uint32_t index = static_cast<std::uint32_t>(m_volumeTextures.size());

    VolumeTextureDesc desc;
    desc.width = externalVolumeTarget.extent.width;
    desc.height = externalVolumeTarget.extent.height;
    desc.depth = externalVolumeTarget.extent.depth;
    desc.format = FromVkFormat(externalVolumeTarget.format);

    VolumeTextureImportInfo importInfo;
    importInfo.isImported = true;
    importInfo.externalTarget = externalVolumeTarget;
    importInfo.currentLayout = currentLayout;

    m_volumeTextures.push_back(VolumeTextureSlot{ desc, name, importInfo });
    return VolumeTextureHandle{ index, 1 };
}

TextureArrayHandle RenderGraphBuilder::CreateTextureArray(const char* name, const TextureArrayDesc& desc)
{
    assert(name != nullptr && name[0] != '\0' &&
        "RenderGraphBuilder::CreateTextureArray requires a non-empty, static-storage-duration name");
    const std::uint32_t index = static_cast<std::uint32_t>(m_textureArrays.size());
    m_textureArrays.push_back(TextureArraySlot{ desc, name, TextureArrayImportInfo{} });
    return TextureArrayHandle{ index, 1 };
}

TextureArrayHandle RenderGraphBuilder::ImportTextureArray(
    const char* name, const TextureArrayTarget& externalTarget, ImageLayout currentLayout)
{
    assert(name != nullptr && name[0] != '\0' &&
        "RenderGraphBuilder::ImportTextureArray requires a non-empty, static-storage-duration name");
    const std::uint32_t index = static_cast<std::uint32_t>(m_textureArrays.size());

    TextureArrayDesc desc;
    desc.width = externalTarget.extent.width;
    desc.height = externalTarget.extent.height;
    desc.arrayLayers = externalTarget.arrayLayers;
    desc.format = FromVkFormat(externalTarget.format);
    desc.hasDepth = externalTarget.hasDepth;

    TextureArrayImportInfo importInfo;
    importInfo.isImported = true;
    importInfo.externalTarget = externalTarget;
    importInfo.currentLayout = currentLayout;

    m_textureArrays.push_back(TextureArraySlot{ desc, name, importInfo });
    return TextureArrayHandle{ index, 1 };
}

void RenderGraphBuilder::KeepTextureOutput(TextureHandle handle)
{
    m_finalTextureOutputs.push_back(handle);
}

void RenderGraphBuilder::KeepVolumeTextureOutput(VolumeTextureHandle handle)
{
    m_finalVolumeTextureOutputs.push_back(handle);
}

void RenderGraphBuilder::KeepBufferOutput(BufferHandle handle)
{
    m_finalBufferOutputs.push_back(handle);
}

void RenderGraphBuilder::KeepTextureArrayOutput(TextureArrayHandle handle)
{
    m_finalTextureArrayOutputs.push_back(handle);
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
    m_persistentCacheTextures.push_back(handle);
    return handle;
}

TextureHandle RenderGraphBuilder::GetOrCreatePersistentTexture(
    const char* owner, const char* name, const TextureDesc& desc)
{
    assert(m_persistentCache != nullptr
        && "RenderGraphBuilder::GetOrCreatePersistentTexture requires a RenderGraphBuilder obtained through "
           "RenderGraph::Execute() (a bare, default-constructed RenderGraphBuilder has no persistent cache)");
    if (m_persistentCache == nullptr) {
        GTE_LOG_ERROR("RenderGraphBuilder",
            "GetOrCreatePersistentTexture() called with no persistent cache installed - refusing.");
        return TextureHandle{};
    }
    const std::optional<RenderGraphPersistentResourceCache::ResolvedTexture> resolved = m_persistentCache->Resolve(
        owner, name, desc, m_persistentCacheCurrentFrame, m_persistentCacheTimingMode);
    if (!resolved.has_value()) {
        return TextureHandle{};
    }
    return MintPersistentHandle(*resolved);
}

TextureHandle RenderGraphBuilder::GetOrCreatePersistentTexture(
    PersistentTextureCacheToken& token, const char* owner, const char* name, const TextureDesc& desc)
{
    assert(m_persistentCache != nullptr
        && "RenderGraphBuilder::GetOrCreatePersistentTexture requires a RenderGraphBuilder obtained through "
           "RenderGraph::Execute()");
    if (m_persistentCache == nullptr) {
        GTE_LOG_ERROR("RenderGraphBuilder",
            "GetOrCreatePersistentTexture() called with no persistent cache installed - refusing.");
        return TextureHandle{};
    }

    if (m_persistentCache->IsTokenLive(token.entry, token.entryEpoch)) {
#ifndef NDEBUG
        assert(m_persistentCache->DebugTokenIdentityMatches(token.entry, owner, name)
            && "RenderGraphBuilder::GetOrCreatePersistentTexture: this token was already resolved against a "
               "DIFFERENT (owner, name) identity - a token must never be reused across two unrelated identities");
#endif
        const std::optional<RenderGraphPersistentResourceCache::ResolvedTexture> resolved =
            m_persistentCache->ResolveFast(
                token.entry, desc, m_persistentCacheCurrentFrame, m_persistentCacheTimingMode);
        if (!resolved.has_value()) {
            return TextureHandle{}; // e.g. a same-frame double-request - already logged inside Resolve*().
        }
        return MintPersistentHandle(*resolved);
    }

    // Slow path - identical to the no-token overload, then refresh `token`
    // for every subsequent call this session.
    const std::optional<RenderGraphPersistentResourceCache::ResolvedTexture> resolved = m_persistentCache->Resolve(
        owner, name, desc, m_persistentCacheCurrentFrame, m_persistentCacheTimingMode);
    if (!resolved.has_value()) {
        return TextureHandle{};
    }
    token.entry = resolved->entry;
    token.entryEpoch = resolved->entryEpoch;
    return MintPersistentHandle(*resolved);
}

CompiledGraphInput RenderGraphBuilder::Finish()
{
    CompiledGraphInput input;
    input.passes = std::move(m_passes);
    input.textures = std::move(m_textures);
    input.buffers = std::move(m_buffers);
    input.volumeTextures = std::move(m_volumeTextures);
    input.textureArrays = std::move(m_textureArrays);
    input.finalVolumeTextureOutputs = std::move(m_finalVolumeTextureOutputs);
    input.finalBufferOutputs = std::move(m_finalBufferOutputs);
    input.finalTextureArrayOutputs = std::move(m_finalTextureArrayOutputs);
    input.persistentCacheTextures = std::move(m_persistentCacheTextures);
    return input;
}

} // namespace gte::rg
