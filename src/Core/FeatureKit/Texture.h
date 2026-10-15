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

} // namespace gte::fx
