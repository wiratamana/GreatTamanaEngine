#include "RenderGraphResourcePool.h"

#include "../Renderer.h"

namespace gte::rg {

RenderGraphResourcePool::RenderGraphResourcePool(Renderer& renderer) noexcept
    : m_renderer(&renderer)
{
}

RenderTexture& RenderGraphResourcePool::AcquireTexture(const TextureDesc& desc, const char* debugName)
{
    for (TextureEntry& entry : m_textureEntries) {
        if (!entry.claimedThisFrame && entry.desc == desc) {
            entry.claimedThisFrame = true;
            return entry.texture;
        }
    }

    // No matching, unclaimed entry - create a fresh one and append it.
    // desc.format == VK_FORMAT_UNDEFINED is passed straight through
    // (rather than pre-resolved here) - Renderer::CreateRenderTexture()
    // itself already treats VK_FORMAT_UNDEFINED as "match Renderer::
    // ColorFormat() exactly" (see AGENTS.md, "Render Target Format
    // Matching"), so every "default format" request still compares equal
    // to every other one via TextureDesc::operator==, exactly as intended.
    // better-render-pass-1 campaign, PHASE8
    // (PHASE8_TEXTUREDESC_USAGE_FIELD_AND_RESOURCE_POOL_AUDIT.md, R5) -
    // desc.usage's TextureUsage::Storage bit is threaded through into
    // allowStorageImageAccess here - the load-bearing fix this phase exists
    // for: before this, a CreateTexture()-declared (pooled/transient)
    // resource had no way to ever become a storage image at all, since this
    // call always passed the default (false). TransferSrc/TransferDst are
    // deliberately NOT threaded anywhere yet - see TextureUsage's own doc
    // comment (RenderGraphTypes.h) for why.
    TextureEntry& entry = m_textureEntries.emplace_back(TextureEntry{ desc,
        m_renderer->CreateRenderTexture(static_cast<int>(desc.width), static_cast<int>(desc.height), desc.format,
            debugName, nullptr, HasFlag(desc.usage, TextureUsage::Storage)),
        true });
    return entry.texture;
}

Buffer& RenderGraphResourcePool::AcquireBuffer(const BufferDesc& desc, const char* debugName)
{
    for (BufferEntry& entry : m_bufferEntries) {
        if (!entry.claimedThisFrame && entry.desc == desc) {
            entry.claimedThisFrame = true;
            return entry.buffer;
        }
    }

    // No matching, unclaimed entry - create a fresh one and append it. See
    // this class's own header comment for why BufferMemoryUsage::GpuOnly is
    // the fixed choice here (BufferDesc carries no memory-usage field of
    // its own, and no real pass exercises this path yet).
    BufferEntry& entry = m_bufferEntries.emplace_back(
        BufferEntry{ desc, m_renderer->CreateBuffer(desc.size, desc.usage, BufferMemoryUsage::GpuOnly, debugName), true });
    return entry.buffer;
}

// better-render-pass-3 campaign, BLOCK5 - mirrors AcquireTexture() EXACTLY
// (same desc-equality linear-scan-and-reuse shape), NOT AcquireVolumeTexture()
// (which does not exist).
TextureArray2D& RenderGraphResourcePool::AcquireTextureArray(const TextureArrayDesc& desc, const char* debugName)
{
    for (TextureArrayEntry& entry : m_textureArrayEntries) {
        if (!entry.claimedThisFrame && entry.desc == desc) {
            entry.claimedThisFrame = true;
            return entry.textureArray;
        }
    }

    // desc.format == VK_FORMAT_UNDEFINED is passed straight through - see
    // AcquireTexture()'s own identical comment for why this is correct
    // (Renderer::CreateTextureArray() itself resolves it per desc.hasDepth -
    // NOT GpuResourceFactory::CreateTextureArray(), which has no
    // ColorFormat()/DepthFormat() access at all - so every "default format"
    // request still compares equal via TextureArrayDesc::operator==
    // regardless).
    TextureArrayEntry& entry = m_textureArrayEntries.emplace_back(TextureArrayEntry{ desc,
        m_renderer->CreateTextureArray(static_cast<int>(desc.width), static_cast<int>(desc.height),
            static_cast<int>(desc.arrayLayers), desc.format, desc.hasDepth, desc.isCubemap,
            HasFlag(desc.usage, TextureUsage::Storage), debugName),
        true });
    return entry.textureArray;
}

void RenderGraphResourcePool::BeginFrame() noexcept
{
    for (TextureEntry& entry : m_textureEntries) {
        entry.claimedThisFrame = false;
    }
    for (BufferEntry& entry : m_bufferEntries) {
        entry.claimedThisFrame = false;
    }
    // better-render-pass-3 campaign, BLOCK5.
    for (TextureArrayEntry& entry : m_textureArrayEntries) {
        entry.claimedThisFrame = false;
    }
}

} // namespace gte::rg
