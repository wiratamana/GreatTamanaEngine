#pragma once

#include "../../Renderer/RenderGraph/GpuBuffer.h"
#include "../../Renderer/RenderGraph/RenderGraphHandles.h"

#include <cstdint>

namespace gte::fx {

// What a buffer is for. The same flags the render graph uses.
using BufferUse = rg::GpuBufferUse;

// A graph buffer handle stamped with the frame and view that produced it. Valid for every callback
// of that view in that frame; a copy kept from an earlier frame is refused.
struct GpuBufferRef {
    rg::BufferHandle handle;
    std::uint64_t frameSerial = 0;
    std::uint64_t byteSize = 0; // The size the caller asked for. The allocation can be larger.
    BufferUse use = BufferUse::None;

    bool IsValid() const noexcept { return handle.IsValid() && frameSerial != 0; }
};

// Bytes a fill clears for a requested size: rounded up to a multiple of 4.
constexpr std::uint64_t FillByteCount(std::uint64_t byteSize) noexcept
{
    return (byteSize + 3) & ~std::uint64_t{ 3 };
}

} // namespace gte::fx
