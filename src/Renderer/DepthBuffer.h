#pragma once

#include "Texture.h"

namespace gte {

// Depth attachment image. Optionally sampled (nearest filter, depth must never be blended).
class DepthBuffer : public Texture {
public:
    DepthBuffer(VmaAllocator allocator, std::shared_ptr<GpuMemoryTracker> tracker, VkDevice device, int width,
        int height, PixelFormat format, const char* debugName = nullptr, bool allowSampledAccess = false)
        : Texture(allocator, std::move(tracker), device,
              TextureCreateInfo{ .width = width,
                  .height = height,
                  .format = format,
                  .renderTarget = true,
                  .sampled = allowSampledAccess,
                  .filter = allowSampledAccess ? TextureFilter::Nearest : TextureFilter::None,
                  .debugName = debugName })
    {
    }
};

} // namespace gte
