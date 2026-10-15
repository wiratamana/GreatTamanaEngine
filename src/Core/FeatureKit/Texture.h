#pragma once

#include "../../Renderer/RenderGraph/RenderGraphHandles.h"

#include <cstdint>

namespace gte::fx {

// A graph texture handle stamped with the frame callback that produced it. Valid for that callback only;
// the handle's own generation does not change between frames, so the serial is what catches a stale copy.
struct Texture {
    rg::TextureHandle handle;
    std::uint64_t frameSerial = 0;

    bool IsValid() const noexcept { return handle.IsValid() && frameSerial != 0; }
};

// Binds a texture through its depth aspect (depth sampling).
struct DepthRef {
    Texture texture;
};

inline DepthRef Depth(Texture texture) noexcept
{
    return DepthRef{ texture };
}

// Sampling filter of a blit.
enum class BlitFilter : std::uint8_t { Linear, Nearest };

} // namespace gte::fx
