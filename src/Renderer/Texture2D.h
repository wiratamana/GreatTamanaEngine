#pragma once

#include "Texture.h"

namespace gte {

// Immutable RGBA8 2D texture (imported image data). Created via Renderer::CreateTexture2D(), which uploads the pixels.
class Texture2D : public Texture {
public:
    Texture2D(VmaAllocator allocator, std::shared_ptr<GpuMemoryTracker> tracker, VkDevice device, int width,
        int height, const char* debugName = nullptr, bool allowStorageImageAccess = false)
        : Texture(allocator, std::move(tracker), device,
              TextureCreateInfo{ .width = width,
                  .height = height,
                  .format = PixelFormat::RGBA8_UNorm,
                  .storage = allowStorageImageAccess,
                  .transferDst = true,
                  .debugName = debugName })
    {
    }
};

} // namespace gte
