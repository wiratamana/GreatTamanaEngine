#include "RenderGraphResourcePool.h"

#include "../Renderer.h"

#include <cassert>
#include <stdexcept>
#include <type_traits>

namespace gte::rg {

RenderGraphResourcePool::RenderGraphResourcePool(Renderer& renderer) noexcept
    : m_renderer(&renderer)
{
}

Texture& RenderGraphResourcePool::AcquireTexture(const TextureDesc& desc, const char* debugName)
{
    for (TextureEntry& entry : m_textureEntries) {
        if (!entry.claimedThisFrame && entry.desc == desc) {
            entry.claimedThisFrame = true;
            entry.lastClaimedFrame = m_currentFrame;
            return *entry.texture;
        }
    }

    // PixelFormat::Undefined is passed through: the Renderer resolves it (color or depth format), so every
    // "default format" request still compares equal. Storage and transfer usage bits are wired through (2D only).
    std::unique_ptr<Texture> texture;
    switch (desc.dimension) {
    case TextureDimension::Tex2D:
        texture = std::make_unique<RenderTexture>(m_renderer->CreateRenderTexture(static_cast<int>(desc.width),
            static_cast<int>(desc.height), desc.format, debugName, nullptr, HasFlag(desc.usage, TextureUsage::Storage),
            /*allowDepthSampledAccess=*/false, /*createDepthCompanion=*/true, /*createColorImage=*/true,
            HasFlag(desc.usage, TextureUsage::TransferSrc), HasFlag(desc.usage, TextureUsage::TransferDst)));
        break;
    case TextureDimension::Array2D:
    case TextureDimension::Cube:
        texture = std::make_unique<TextureArray2D>(m_renderer->CreateTextureArray(static_cast<int>(desc.width),
            static_cast<int>(desc.height), static_cast<int>(desc.arrayLayers), desc.format, desc.hasDepth,
            desc.dimension == TextureDimension::Cube, HasFlag(desc.usage, TextureUsage::Storage), debugName));
        break;
    case TextureDimension::Tex3D:
        throw std::logic_error("RenderGraphResourcePool::AcquireTexture: a 3D texture cannot be pooled (import-only).");
    }

    TextureEntry& entry = m_textureEntries.emplace_back(TextureEntry{ desc, std::move(texture), true, m_currentFrame });
    return *entry.texture;
}

RenderTexture& RenderGraphResourcePool::AcquireRenderTexture(const TextureDesc& desc, const char* debugName)
{
    assert(desc.dimension == TextureDimension::Tex2D && "AcquireRenderTexture requires a Tex2D desc");
    return static_cast<RenderTexture&>(AcquireTexture(desc, debugName));
}

TextureArray2D& RenderGraphResourcePool::AcquireTextureArray(const TextureDesc& desc, const char* debugName)
{
    assert((desc.dimension == TextureDimension::Array2D || desc.dimension == TextureDimension::Cube)
        && "AcquireTextureArray requires an Array2D/Cube desc");
    return static_cast<TextureArray2D&>(AcquireTexture(desc, debugName));
}

Buffer& RenderGraphResourcePool::AcquireBuffer(const BufferDesc& desc, const char* debugName)
{
    for (BufferEntry& entry : m_bufferEntries) {
        if (!entry.claimedThisFrame && entry.desc == desc) {
            entry.claimedThisFrame = true;
            entry.lastClaimedFrame = m_currentFrame;
            return entry.buffer;
        }
    }

    BufferEntry& entry = m_bufferEntries.emplace_back(
        BufferEntry{ desc, m_renderer->CreateBuffer(desc.size, desc.usage, BufferMemoryUsage::GpuOnly, debugName), true,
            m_currentFrame });
    return entry.buffer;
}

void RenderGraphResourcePool::BeginFrame(std::uint64_t currentFrame) noexcept
{
    m_currentFrame = currentFrame;
    const auto evict = [&](auto& entries) {
        for (auto it = entries.begin(); it != entries.end();) {
            it->claimedThisFrame = false;
            if (IsStalePoolEntry(false, it->lastClaimedFrame, currentFrame)) {
                if constexpr (std::is_same_v<std::remove_reference_t<decltype(*it)>, TextureEntry>) {
                    m_evictedImages.push_back(it->texture->Image());
                }
                it = entries.erase(it);
                ++m_evictedEntries;
            } else {
                ++it;
            }
        }
    };
    evict(m_textureEntries);
    evict(m_bufferEntries);
}

} // namespace gte::rg
