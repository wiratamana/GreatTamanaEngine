#pragma once

#include "Extent.h"
#include "PixelFormat.h"

#include <cstddef>
#include <vector>

namespace gte {

// Tightly-packed, row-major pixel readback of a render target - the AI-debug
// capture primitive behind Renderer::CaptureRenderTarget() and the
// /get_debug_texture network route. Zero Vulkan dependency.
struct CaptureResult {
    Extent2D extent;
    PixelFormat format = PixelFormat::Undefined;
    std::vector<std::byte> pixels;
};

} // namespace gte
