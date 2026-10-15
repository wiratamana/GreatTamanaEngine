#pragma once

#include "RenderGraphTypes.h"
#include "../Buffer.h"
#include "../RenderTexture.h"
#include "../Texture.h"
#include "../TextureArray2D.h"

#include <cstddef>
#include <cstdint>
#include <list>
#include <memory>
#include <utility>
#include <vector>

namespace gte {
class Renderer;
}

namespace gte::rg {

// Frames an unclaimed pool entry survives before it is freed; it must exceed the frames in flight.
inline constexpr std::uint64_t kPoolStaleFrames = 120;

// True when an entry is unclaimed and has been idle for more than `maxIdleFrames` frames.
constexpr bool IsStalePoolEntry(bool claimed, std::uint64_t lastClaimedFrame, std::uint64_t currentFrame,
    std::uint64_t maxIdleFrames = kPoolStaleFrames) noexcept
{
    return !claimed && currentFrame > lastClaimedFrame && (currentFrame - lastClaimedFrame) > maxIdleFrames;
}

// Owns every physical texture/buffer the graph creates and reuses an unclaimed entry with an equal desc.
// A claimed entry is excluded until BeginFrame(), so no two live resources alias; entries idle for
// kPoolStaleFrames are freed there. Imported resources bypass it.
class RenderGraphResourcePool {
public:
    explicit RenderGraphResourcePool(Renderer& renderer) noexcept;

    // Returns a pooled Texture for `desc`: a RenderTexture for Tex2D, a TextureArray2D for Array2D/Cube.
    // Tex3D is import-only. `debugName` is only used when a new entry is created; it never takes part in matching.
    Texture& AcquireTexture(const TextureDesc& desc, const char* debugName);

    // Typed views of AcquireTexture(); `desc.dimension` must match.
    RenderTexture& AcquireRenderTexture(const TextureDesc& desc, const char* debugName);
    TextureArray2D& AcquireTextureArray(const TextureDesc& desc, const char* debugName);

    // New buffers are always BufferMemoryUsage::GpuOnly.
    Buffer& AcquireBuffer(const BufferDesc& desc, const char* debugName);

    // Call once per frame before realizing the graph: unclaims every entry, then frees the stale ones.
    // `currentFrame` is the graph's persistent frame counter.
    void BeginFrame(std::uint64_t currentFrame) noexcept;

    std::size_t TextureEntryCount() const noexcept { return m_textureEntries.size(); }
    std::size_t BufferEntryCount() const noexcept { return m_bufferEntries.size(); }
    std::uint64_t EvictedEntryCount() const noexcept { return m_evictedEntries; }

    // Color images freed by the last BeginFrame(); the graph drops their debug snapshots.
    std::vector<VkImage> TakeEvictedImages() noexcept { return std::move(m_evictedImages); }

private:
    struct TextureEntry {
        TextureDesc desc;
        std::unique_ptr<Texture> texture;
        bool claimedThisFrame = false;
        std::uint64_t lastClaimedFrame = 0;
    };

    struct BufferEntry {
        BufferDesc desc;
        Buffer buffer;
        bool claimedThisFrame = false;
        std::uint64_t lastClaimedFrame = 0;
    };

    Renderer* m_renderer = nullptr;
    // A list keeps returned references valid when another entry is evicted.
    std::list<TextureEntry> m_textureEntries;
    std::list<BufferEntry> m_bufferEntries;
    std::uint64_t m_currentFrame = 0;
    std::uint64_t m_evictedEntries = 0;
    std::vector<VkImage> m_evictedImages;
};

} // namespace gte::rg
