#pragma once

#include "../../Renderer/RenderGraph/RenderGraphHandles.h"
#include "../../Renderer/RenderGraph/ViewScopeUtil.h"

#include <cstdint>

namespace gte::fx {

// A graph texture handle stamped with the frame and view that produced it. Valid for every callback of that
// view in that frame; the handle's own generation never changes, so the serial is what catches a stale copy.
struct Texture {
    rg::TextureHandle handle;
    std::uint64_t frameSerial = 0;

    bool IsValid() const noexcept { return handle.IsValid() && frameSerial != 0; }
};

// Serial shared by all callbacks of one view in one frame. Never zero.
constexpr std::uint64_t FrameSerial(std::uint64_t frameIndex, rg::ViewScope scope) noexcept
{
    return frameIndex * rg::kViewScopeCount + rg::ViewScopeIndex(scope) + 1;
}

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
