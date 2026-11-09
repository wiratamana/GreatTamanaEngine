#pragma once

#include "RenderTarget.h"
#include "StaticImageDesc.h"
#include "Texture.h"
#include "VolumeTarget.h"

namespace gte {

// A sampled 2D or 3D image filled once by GpuResourceFactory::CreateStaticImage(), then only read.
// It stays in the shader-read layout, so one copy is safe for every frame in flight.
class StaticImage : public Texture {
public:
    StaticImage(VmaAllocator allocator, std::shared_ptr<GpuMemoryTracker> tracker, VkDevice device,
        const StaticImageDesc& desc)
        : Texture(allocator, std::move(tracker), device,
              TextureCreateInfo{ .dimension = desc.depth > 1 ? TextureDimension::Tex3D : TextureDimension::Tex2D,
                  .width = static_cast<int>(desc.width),
                  .height = static_cast<int>(desc.height),
                  .depth = static_cast<int>(desc.depth),
                  .format = desc.format,
                  .transferDst = true,
                  .addressMode = desc.addressMode,
                  .debugName = desc.debugName })
    {
    }

    bool IsVolume() const noexcept { return Depth() > 1; }

    // Non-owning descriptions for the render graph import.
    VolumeTarget VolumeTargetOf() const noexcept
    {
        VolumeTarget target;
        target.image = Image();
        target.imageView = View();
        target.extent = VkExtent3D{ static_cast<std::uint32_t>(Width()), static_cast<std::uint32_t>(Height()),
            static_cast<std::uint32_t>(Depth()) };
        target.format = NativeFormat();
        return target;
    }

    RenderTarget Target2D() const noexcept
    {
        RenderTarget target;
        target.image = Image();
        target.imageView = View();
        target.extent = VkExtent2D{ static_cast<std::uint32_t>(Width()), static_cast<std::uint32_t>(Height()) };
        target.format = NativeFormat();
        return target;
    }
};

} // namespace gte
