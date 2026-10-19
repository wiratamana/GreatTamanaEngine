#pragma once

#include "../Core/PixelFormat.h"
#include "../Core/TextureAddressMode.h"

namespace gte {

// Everything needed to create and upload a 2D texture from tightly packed 8-bit RGBA pixels.
struct Texture2DUploadDesc {
    const void* pixelsRgba8 = nullptr;
    int width = 0;
    int height = 0;
    const char* debugName = nullptr; // Stored by pointer - must outlive the texture.
    PixelFormat format = PixelFormat::RGBA8_UNorm;
    TextureAddressMode addressMode = TextureAddressMode::ClampToEdge;
    bool generateMips = false;           // Uploads a full box-filtered mip chain.
    bool allowStorageImageAccess = false;
    float maxAnisotropy = 1.0f;          // > 1 needs the device feature.
};

} // namespace gte
