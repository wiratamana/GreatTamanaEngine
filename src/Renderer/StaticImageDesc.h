#pragma once

#include "../Core/PixelFormat.h"
#include "../Core/TextureAddressMode.h"

#include <cstdint>

namespace gte {

// Everything needed to create and upload a read-only 2D or 3D image from tightly packed texels
// (x fastest, then y, then z). depth == 1 makes a 2D image.
struct StaticImageDesc {
    const void* texels = nullptr; // Copied during the upload; need not outlive the call.
    std::uint32_t width = 0;
    std::uint32_t height = 0;
    std::uint32_t depth = 1;
    PixelFormat format = PixelFormat::RGBA8_UNorm; // RGBA8_UNorm or R8_UNorm.
    TextureAddressMode addressMode = TextureAddressMode::Repeat;
    const char* debugName = nullptr; // Copied by the constructor.
};

} // namespace gte
