#pragma once

#include "RenderGraph/RenderGraphBarrierPlanner.h"
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

    // Last known GPU barrier state of the image; written back by one-shot runs.
    rg::ResourceState CurrentState() const noexcept { return m_currentState; }
    void SetCurrentState(rg::ResourceState state) noexcept { m_currentState = state; }

private:
    rg::ResourceState m_currentState{};
};

} // namespace gte
