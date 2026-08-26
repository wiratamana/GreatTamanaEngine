#include "RenderGraphBuilder.h"

namespace gte::rg {

// --- RenderGraphBuilder::PassBuilder ---------------------------------------

void RenderGraphBuilder::PassBuilder::ReadTexture(TextureHandle handle, ResourceAccess access)
{
    m_pass.reads.push_back(ResourceUsage::ForTexture(handle, access));
}

void RenderGraphBuilder::PassBuilder::WriteColorAttachment(
    TextureHandle handle, const std::optional<std::array<float, 4>>& clearColor)
{
    m_pass.writes.push_back(ResourceUsage::ForTexture(handle, ResourceAccess::ColorAttachmentWrite));
    if (clearColor.has_value()) {
        m_pass.colorClearValue = clearColor;
    }
}

void RenderGraphBuilder::PassBuilder::WriteDepthStencilAttachment(TextureHandle handle, std::optional<float> clearDepth)
{
    m_pass.writes.push_back(ResourceUsage::ForTexture(handle, ResourceAccess::DepthStencilAttachmentReadWrite));
    if (clearDepth.has_value()) {
        m_pass.depthClearValue = clearDepth;
    }
}

void RenderGraphBuilder::PassBuilder::ReadBuffer(BufferHandle handle, ResourceAccess access)
{
    m_pass.reads.push_back(ResourceUsage::ForBuffer(handle, access));
}

void RenderGraphBuilder::PassBuilder::WriteBuffer(BufferHandle handle, ResourceAccess access)
{
    m_pass.writes.push_back(ResourceUsage::ForBuffer(handle, access));
}

// --- RenderGraphBuilder ------------------------------------------------

TextureHandle RenderGraphBuilder::CreateTexture(const char* name, const TextureDesc& desc)
{
    assert(name != nullptr && name[0] != '\0' &&
        "RenderGraphBuilder::CreateTexture requires a non-empty, static-storage-duration name");

    const std::uint32_t index = static_cast<std::uint32_t>(m_textureDescs.size());
    m_textureDescs.push_back(desc);
    m_textureNames.push_back(name);
    m_textureImportInfo.push_back(TextureImportInfo{});
    return TextureHandle{ index, 1 };
}

BufferHandle RenderGraphBuilder::CreateBuffer(const char* name, const BufferDesc& desc)
{
    assert(name != nullptr && name[0] != '\0' &&
        "RenderGraphBuilder::CreateBuffer requires a non-empty, static-storage-duration name");

    const std::uint32_t index = static_cast<std::uint32_t>(m_bufferDescs.size());
    m_bufferDescs.push_back(desc);
    m_bufferNames.push_back(name);
    return BufferHandle{ index, 1 };
}

TextureHandle RenderGraphBuilder::ImportTexture(const char* name, const RenderTarget& externalTarget, VkImageLayout currentLayout)
{
    assert(name != nullptr && name[0] != '\0' &&
        "RenderGraphBuilder::ImportTexture requires a non-empty, static-storage-duration name");

    const std::uint32_t index = static_cast<std::uint32_t>(m_textureDescs.size());

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
    m_textureDescs.push_back(desc);
    m_textureNames.push_back(name);

    TextureImportInfo importInfo;
    importInfo.isImported = true;
    importInfo.externalTarget = externalTarget;
    importInfo.currentLayout = currentLayout;
    m_textureImportInfo.push_back(importInfo);

    return TextureHandle{ index, 1 };
}

CompiledGraphInput RenderGraphBuilder::Finish()
{
    CompiledGraphInput input;
    input.passes = std::move(m_passes);
    input.textureDescs = std::move(m_textureDescs);
    input.textureNames = std::move(m_textureNames);
    input.textureImportInfo = std::move(m_textureImportInfo);
    input.bufferDescs = std::move(m_bufferDescs);
    input.bufferNames = std::move(m_bufferNames);
    return input;
}

} // namespace gte::rg
