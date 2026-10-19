#pragma once

#include "RenderGraph/RenderGraphBarrierPlanner.h"
#include "Texture.h"
#include "Texture2DUploadDesc.h"

namespace gte {

// Immutable 2D texture (imported image data). Created via GpuResourceFactory::CreateTexture2D(), which uploads the pixels.
class Texture2D : public Texture {
public:
    Texture2D(VmaAllocator allocator, std::shared_ptr<GpuMemoryTracker> tracker, VkDevice device,
        const Texture2DUploadDesc& desc, std::uint32_t mipLevels)
        : Texture(allocator, std::move(tracker), device,
              TextureCreateInfo{ .width = desc.width,
                  .height = desc.height,
                  .mipLevels = mipLevels,
                  .format = desc.format,
                  .storage = desc.allowStorageImageAccess,
                  .transferDst = true,
                  .addressMode = desc.addressMode,
                  .maxAnisotropy = desc.maxAnisotropy,
                  .debugName = desc.debugName })
    {
    }

    // Last known GPU barrier state of the image; written back by one-shot runs.
    rg::ResourceState CurrentState() const noexcept { return m_currentState; }
    void SetCurrentState(rg::ResourceState state) noexcept { m_currentState = state; }

private:
    rg::ResourceState m_currentState{};
};

} // namespace gte
