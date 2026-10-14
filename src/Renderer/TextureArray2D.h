#pragma once

#include "Texture.h"
#include "TextureArrayTarget.h"

#include <cassert>

namespace gte {

// 2D array or cubemap (cube needs layers multiple of 6 and width == height; validated by GpuResourceFactory).
// Has a whole-array view plus one view per layer. Single mip level.
class TextureArray2D : public Texture {
public:
    TextureArray2D(VmaAllocator allocator, std::shared_ptr<GpuMemoryTracker> tracker, VkDevice device, int width,
        int height, int arrayLayers, PixelFormat format, bool hasDepth, bool isCubemap,
        bool allowStorageImageAccess = false, const char* debugName = nullptr)
        : Texture(allocator, std::move(tracker), device,
              TextureCreateInfo{ .dimension = isCubemap ? TextureDimension::Cube : TextureDimension::Array2D,
                  .width = width,
                  .height = height,
                  .layers = arrayLayers,
                  .format = format,
                  .renderTarget = true,
                  .storage = allowStorageImageAccess && !hasDepth,
                  .perLayerViews = true,
                  .debugName = debugName })
    {
        assert(hasDepth == IsDepth() && "TextureArray2D: hasDepth must match the format's aspect.");
        assert(!(hasDepth && allowStorageImageAccess) && "TextureArray2D: depth arrays cannot be storage images.");
    }

    // Non-owning description for RenderGraphBuilder::ImportTextureArray().
    TextureArrayTarget Target() const noexcept
    {
        TextureArrayTarget target;
        target.image = Image();
        target.imageView = View();
        target.extent = VkExtent2D{ static_cast<std::uint32_t>(Width()), static_cast<std::uint32_t>(Height()) };
        target.arrayLayers = ArrayLayers();
        target.format = NativeFormat();
        target.hasDepth = IsDepth();
        target.layerViews = LayerViews().data();
        target.layerViewCount = static_cast<std::uint32_t>(LayerViews().size());
        target.sampler = Sampler();
        return target;
    }
};

} // namespace gte
