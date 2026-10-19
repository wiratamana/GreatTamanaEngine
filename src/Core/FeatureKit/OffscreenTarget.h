#pragma once

#include "Frame.h"
#include "Texture.h"
#include "../PixelFormat.h"

#include <cstdint>
#include <memory>
#include <string_view>

namespace gte::fx {

// A square depth-only target that can be sampled later (a depth map).
struct DepthOnly {
    std::uint32_t resolution = 0;
};

// A color target with its own depth. Undefined format matches the renderer's color format.
struct ColorDepth {
    std::uint32_t width = 0;
    std::uint32_t height = 0;
    PixelFormat format = PixelFormat::Undefined;
};

// A render view owned by the engine, one per view scope, imported into the graph every frame.
// A new size resizes it (GPU idle once); a failed or refused create/resize gives an invalid Texture and backs off.
class OffscreenTarget {
public:
    // Size Trim() shrinks an unused target to.
    static constexpr std::uint32_t kTrimmedExtent = 4;

    explicit OffscreenTarget(std::string_view baseName);
    ~OffscreenTarget();

    OffscreenTarget(const OffscreenTarget&) = delete;
    OffscreenTarget& operator=(const OffscreenTarget&) = delete;

    // The target for the frame's view; kept alive so the pass that writes it is never culled.
    // Invalid when the engine could not make or resize the view.
    Texture Acquire(Frame& frame, DepthOnly request);
    Texture Acquire(Frame& frame, ColorDepth request);

    // Shrinks an existing target to kTrimmedExtent to free its memory. Never creates one; the next
    // Acquire() of the full size resizes it back. Call only while the owning pass is enabled.
    void Trim(Frame& frame);

    // Current size of the frame's view target, or {0, 0} when none exists.
    Extent2D Extent(const Frame& frame) const;

private:
    struct Impl;
    std::unique_ptr<Impl> m_impl;
};

} // namespace gte::fx
