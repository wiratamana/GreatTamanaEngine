#include "RenderGraphBuilder.h"

#include "../../Core/Logging.h" // GTE_LOG_ERROR for GetOrCreatePersistentTexture()'s own
    // "no persistent cache installed" refusal branch.

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
    // Multi-Render-Target (MRT) campaign (task_manager/mrt-1), PHASE1 - the
    // 8-attachment cap (Locked Design Decision 2, PHASE0_MASTER_STRATEGY.md).
    assert(m_pass.colorAttachments.size() < kMaxColorAttachments &&
        "RenderGraphBuilder::PassBuilder::WriteColorAttachment: exceeded kMaxColorAttachments per pass");

    // ALWAYS ALSO push a ColorAttachmentWrite onto `writes` (Phase 6's
    // original behavior, kept byte-for-byte) - this is what keeps
    // RenderGraphCompiler's dependency-edge/culling computation and
    // RenderGraph::ApplyUsageBarrierIfNeeded()'s per-usage barrier loop
    // working per-target with ZERO changes to either - see
    // task_manager/mrt-1/PHASE0_MASTER_STRATEGY.md's Step 2.
    m_pass.writes.push_back(ResourceUsage::ForTexture(handle, ResourceAccess::ColorAttachmentWrite));

    // MRT campaign, PHASE1 - PassRecord::colorClearValue is kept, byte-for-
    // byte, exactly as before (RenderGraph::ExecuteCompiledGraph() still
    // reads it exclusively until PHASE2 lands, and 3 pre-existing tests in
    // RenderGraphBuilderTests.cpp assert on it directly for the
    // single-attachment case) - this is a deliberate deviation from a
    // literal reading of this phase's own strategy doc's illustrative code
    // sample, which omitted this line; see PHASE1_COMPLETION_REPORT.md's
    // own "Design decisions" section for the full reasoning. The NEW
    // ordered `colorAttachments` list (below) is what PHASE2 will read
    // instead, going forward - both are populated here so nothing regresses
    // in between.
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


// --- RenderGraphBuilder ------------------------------------------------

// render-pass-6 campaign, PHASE5 (item 2.1) - every Create*/Import* method
// below now pushes exactly ONE entry onto its own single slot vector
// (m_textures/m_buffers/m_volumeTextures) instead of three separate,
// hand-kept-in-lockstep vectors - see TextureSlot/BufferSlot/
// VolumeTextureSlot (RenderGraphBuilder.h) for the full reasoning. Every
// method's real, observable behavior (what desc/name/importInfo ends up
// stored, what handle is returned) is byte-for-byte unchanged.

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
    VkImageLayout currentLayout, VkSampler colorSampler, VkSampler depthSampler, RenderTexture* trackedOwner)
{
    assert(name != nullptr && name[0] != '\0' &&
        "RenderGraphBuilder::ImportTexture requires a non-empty, static-storage-duration name");

    const std::uint32_t index = static_cast<std::uint32_t>(m_textures.size());

    // Mirror the external target's own real shape into a TextureDesc
    // purely for informational/debug-display purposes (Phase 8) - Phase 4
    // never pool-matches against an imported resource's desc, since an
    // imported resource is never allocated/freed by the graph in the
    // first place.
    TextureDesc desc;
    desc.width = externalTarget.extent.width;
    desc.height = externalTarget.extent.height;
    desc.format = externalTarget.format;
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

// Declare-time sibling of PassContext::resolveReadTexture()/resolveDepthTexture()
// - see this method's own doc comment (RenderGraphBuilder.h). Built only from
// TextureImportInfo, already stored synchronously the instant ImportTexture()
// was called - no compile/execute/resolve step needed for an imported handle.
RenderGraphBuilder::ImportedTextureSamplers RenderGraphBuilder::ResolveImportedTextureSamplers(
    TextureHandle handle) const noexcept
{
    if (handle.index >= m_textures.size() || !m_textures[handle.index].importInfo.isImported) {
        return ImportedTextureSamplers{};
    }
    const TextureImportInfo& importInfo = m_textures[handle.index].importInfo;
    ImportedTextureSamplers resolved{
        importInfo.colorSampler, importInfo.externalTarget.depthImageView, importInfo.depthSampler
    };
    if (IsResolvedViewMissingItsSampler(resolved.depthImageView, resolved.depthSampler)) {
        assert(false && "RenderGraphBuilder::ResolveImportedTextureSamplers() - non-null depth view with a null sampler");
        GTE_LOG_ERROR("RenderGraphBuilder",
            "ResolveImportedTextureSamplers() - handle index " + std::to_string(handle.index)
                + " resolved a non-null depth view with a null sampler.");
    }
    return resolved;
}

BufferHandle RenderGraphBuilder::ImportBuffer(const char* name, VkBuffer externalBuffer, VkDeviceSize size)
{
    assert(name != nullptr && name[0] != '\0' &&
        "RenderGraphBuilder::ImportBuffer requires a non-empty, static-storage-duration name");

    const std::uint32_t index = static_cast<std::uint32_t>(m_buffers.size());

    // Mirror the external buffer's own real size into a BufferDesc purely
    // for informational/debug-display purposes (Phase 8's snapshot) -
    // RenderGraphResourcePool never pool-matches against an imported
    // resource's desc, since an imported resource is never allocated/freed
    // by the graph in the first place (see ImportTexture()'s own identical
    // comment above). `usage` has no equivalent to mirror (a live VkBuffer
    // does not expose its own creation-time usage flags back) - left at 0,
    // which is harmless since an imported entry's desc is never read for
    // pool-matching purposes.
    BufferDesc desc;
    desc.size = size;
    desc.usage = 0;

    BufferImportInfo importInfo;
    importInfo.isImported = true;
    importInfo.externalBuffer = externalBuffer;
    importInfo.size = size;

    m_buffers.push_back(BufferSlot{ desc, name, importInfo });
    return BufferHandle{ index, 1 };
}

VolumeTextureHandle RenderGraphBuilder::ImportVolumeTexture(
    const char* name, const VolumeTarget& externalVolumeTarget, VkImageLayout currentLayout)
{
    assert(name != nullptr && name[0] != '\0' &&
        "RenderGraphBuilder::ImportVolumeTexture requires a non-empty, static-storage-duration name");

    const std::uint32_t index = static_cast<std::uint32_t>(m_volumeTextures.size());

    // Mirror the external target's own real shape into a VolumeTextureDesc
    // purely for informational/debug-display purposes - mirrors
    // ImportTexture()'s own identical comment above.
    VolumeTextureDesc desc;
    desc.width = externalVolumeTarget.extent.width;
    desc.height = externalVolumeTarget.extent.height;
    desc.depth = externalVolumeTarget.extent.depth;
    desc.format = externalVolumeTarget.format;

    VolumeTextureImportInfo importInfo;
    importInfo.isImported = true;
    importInfo.externalTarget = externalVolumeTarget;
    importInfo.currentLayout = currentLayout;

    m_volumeTextures.push_back(VolumeTextureSlot{ desc, name, importInfo });
    return VolumeTextureHandle{ index, 1 };
}

// better-render-pass-3 campaign, BLOCK5 - mirrors CreateTexture()'s exact
// assert-then-push-then-return-handle shape.
TextureArrayHandle RenderGraphBuilder::CreateTextureArray(const char* name, const TextureArrayDesc& desc)
{
    assert(name != nullptr && name[0] != '\0' &&
        "RenderGraphBuilder::CreateTextureArray requires a non-empty, static-storage-duration name");
    const std::uint32_t index = static_cast<std::uint32_t>(m_textureArrays.size());
    m_textureArrays.push_back(TextureArraySlot{ desc, name, TextureArrayImportInfo{} });
    return TextureArrayHandle{ index, 1 };
}

// better-render-pass-3 campaign, BLOCK5 - mirrors ImportVolumeTexture()'s
// exact import-info-population shape immediately above.
TextureArrayHandle RenderGraphBuilder::ImportTextureArray(
    const char* name, const TextureArrayTarget& externalTarget, VkImageLayout currentLayout)
{
    assert(name != nullptr && name[0] != '\0' &&
        "RenderGraphBuilder::ImportTextureArray requires a non-empty, static-storage-duration name");
    const std::uint32_t index = static_cast<std::uint32_t>(m_textureArrays.size());

    TextureArrayDesc desc;
    desc.width = externalTarget.extent.width;
    desc.height = externalTarget.extent.height;
    desc.arrayLayers = externalTarget.arrayLayers;
    desc.format = externalTarget.format;
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

// editor-core-separation-26 campaign, PHASE1 - see
// RenderGraphBuilder::KeepBufferOutput()'s own declaration
// (RenderGraphBuilder.h) for the full reasoning; mirrors
// KeepVolumeTextureOutput() immediately above verbatim.
void RenderGraphBuilder::KeepBufferOutput(BufferHandle handle)
{
    m_finalBufferOutputs.push_back(handle);
}

// better-render-pass-3 campaign, BLOCK5 - see
// RenderGraphBuilder::KeepTextureArrayOutput()'s own declaration
// (RenderGraphBuilder.h) for the full reasoning; mirrors
// KeepVolumeTextureOutput()/KeepBufferOutput() above verbatim.
void RenderGraphBuilder::KeepTextureArrayOutput(TextureArrayHandle handle)
{
    m_finalTextureArrayOutputs.push_back(handle);
}

// The real, official pass-declaration entry point for a raw image blit/copy
// - see AddBlitPass()'s own declaration (RenderGraphBuilder.h). NOT built on
// top of AddPass()/AddComputePass() - constructs its own PassRecord
// directly, mirroring AddPass()'s own internal shape, then derives ownership
// and registers with the toggle registry exactly like AddRenderPass() does.
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

// editor-core-separation-27 campaign, PHASE8 (BIG_STEP_3, Section 5.1/5.2) -
// the ONE place either GetOrCreatePersistentTexture() overload mints this
// frame's real TextureHandle from a successful Resolve()/ResolveFast()
// result - see this method's own declaration (RenderGraphBuilder.h) for the
// full reasoning.
TextureHandle RenderGraphBuilder::MintPersistentHandle(
    const RenderGraphPersistentResourceCache::ResolvedTexture& resolved)
{
    // Persistent cache entries are always color-only (Section 7) - no depth sampler to pass.
    const TextureHandle handle = ImportTexture(
        resolved.combinedKey->c_str(), resolved.texture->Target(), resolved.lastKnownLayout, resolved.texture->Sampler());
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
        return TextureHandle{}; // Resolve() already logged the specific reason - never a second log here.
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
    input.textureArrays = std::move(m_textureArrays); // better-render-pass-3 campaign, BLOCK5
    input.finalVolumeTextureOutputs = std::move(m_finalVolumeTextureOutputs);
    input.finalBufferOutputs = std::move(m_finalBufferOutputs);
    input.finalTextureArrayOutputs = std::move(m_finalTextureArrayOutputs); // better-render-pass-3 campaign, BLOCK5
    input.persistentCacheTextures = std::move(m_persistentCacheTextures);
    return input;
}

} // namespace gte::rg
