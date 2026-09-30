#include "RenderGraphBuilder.h"

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

TextureHandle RenderGraphBuilder::ImportTexture(const char* name, const RenderTarget& externalTarget, VkImageLayout currentLayout)
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

    m_textures.push_back(TextureSlot{ desc, name, importInfo });
    return TextureHandle{ index, 1 };
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

// editor-core-separation-26 campaign, PHASE5
// (PHASE5_ADDBLITPASS_BUILDER_ENTRYPOINT.md) - see AddBlitPass()'s own
// declaration (RenderGraphBuilder.h) for the full reasoning. This is NOT
// built on top of AddPass()/AddComputePass() - it constructs its own
// PassRecord directly, mirroring AddPass()'s own internal shape.
void RenderGraphBuilder::AddBlitPass(const char* name, const BlitSpec& spec, RenderPassEvent renderPassEvent,
    ViewScope viewScope, RenderPassCategory category, RenderPassTagMask tags)
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

    if (m_debugMetadataSink != nullptr) {
        m_debugMetadataSink->OnPassDeclared(m_passes.size() - 1, category, RenderPassDrawKind::Blit, tags);
    }
}

CompiledGraphInput RenderGraphBuilder::Finish()
{
    CompiledGraphInput input;
    input.passes = std::move(m_passes);
    input.textures = std::move(m_textures);
    input.buffers = std::move(m_buffers);
    input.volumeTextures = std::move(m_volumeTextures);
    input.finalVolumeTextureOutputs = std::move(m_finalVolumeTextureOutputs);
    input.finalBufferOutputs = std::move(m_finalBufferOutputs);
    return input;
}

} // namespace gte::rg
