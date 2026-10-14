#pragma once

#include "Texture.h"
#include "VolumeTarget.h"

namespace gte {

// 3D texture, always storage + sampled (written by compute, then sampled).
class VolumeTexture : public Texture {
public:
    VolumeTexture(VmaAllocator allocator, std::shared_ptr<GpuMemoryTracker> tracker, VkDevice device, int width,
        int height, int depth, PixelFormat format, const char* debugName = nullptr)
        : Texture(allocator, std::move(tracker), device,
              TextureCreateInfo{ .dimension = TextureDimension::Tex3D,
                  .width = width,
                  .height = height,
                  .depth = depth,
                  .format = format,
                  .storage = true,
                  .debugName = debugName })
    {
    }

    // Non-owning description for RenderGraphBuilder::ImportVolumeTexture().
    VolumeTarget Target() const noexcept
    {
        VolumeTarget target;
        target.image = Image();
        target.imageView = View();
        target.extent = VkExtent3D{ static_cast<std::uint32_t>(Width()), static_cast<std::uint32_t>(Height()),
            static_cast<std::uint32_t>(Depth()) };
        target.format = NativeFormat();
        return target;
    }
};

} // namespace gte
