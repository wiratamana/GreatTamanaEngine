#pragma once

// Pure rules for reading GPU data back on the CPU while frames overlap.

#include "../../Renderer/GpuTiming.h"
#include "../../Renderer/OffscreenFrameTicket.h"

#include <cstdint>
#include <limits>

namespace gte::fx::detail {

// Frames between the frame that writes a readback cell and the frame that reads it.
inline constexpr std::uint64_t kReadbackSampleLagFrames = 2;
static_assert(kReadbackSampleLagFrames >= kGpuFramesInFlight, "a reader must never see a cell a frame in flight writes");

// Highest frame serial known to be done: everything for a synchronous frame, which drained the GPU first.
constexpr std::uint64_t CompletedSerialLimit(const OffscreenFrameTicket& ticket) noexcept
{
    return ticket.IsAsync() ? ticket.completedSerial : std::numeric_limits<std::uint64_t>::max();
}

// A cell holds the frame `lag` before `index` and was written by the frame with `cellSerial` (0 = synchronous).
// It is readable only once that frame is done. The frame index and the serial can drift apart when a frame skips
// its submit, so the index alone proves nothing.
constexpr bool CellReadable(std::uint64_t cellFrame, std::uint64_t index, std::uint64_t lag, std::uint64_t cellSerial,
    std::uint64_t completedSerial) noexcept
{
    return index >= lag && cellFrame == index - lag && cellSerial <= completedSerial;
}

} // namespace gte::fx::detail
