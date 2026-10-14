#pragma once

#include "RenderGraphTypes.h"
#include "../Buffer.h"
#include "../RenderTexture.h"
#include "../Texture.h"
#include "../TextureArray2D.h"

#include <deque>
#include <memory>

namespace gte {
class Renderer;
}

namespace gte::rg {

// Owns every physical texture/buffer the render graph has created, and reuses one whenever a request has an
// identical desc. Entries are never evicted.
//
// Matching key: (desc, claimed-this-frame == false). The first unclaimed entry whose desc equals the request
// is claimed; otherwise a new entry is created. A claimed entry is excluded until BeginFrame(), so a pooled
// resource is used by at most one virtual resource per frame (two live ping/pong textures never alias).
//
// Imported resources never go through this pool. Entries live in a std::deque so a returned reference stays
// valid when a later call appends a new entry.
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

    // Call once per frame before realizing the graph - marks every entry as unclaimed.
    void BeginFrame() noexcept;

private:
    struct TextureEntry {
        TextureDesc desc;
        std::unique_ptr<Texture> texture;
        bool claimedThisFrame = false;
    };

    struct BufferEntry {
        BufferDesc desc;
        Buffer buffer;
        bool claimedThisFrame = false;
    };

    Renderer* m_renderer = nullptr;
    std::deque<TextureEntry> m_textureEntries;
    std::deque<BufferEntry> m_bufferEntries;
};

} // namespace gte::rg
