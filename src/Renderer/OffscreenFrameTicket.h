#pragma once

// Vulkan-free identity of one offscreen frame: the ring slot it records into and how far the GPU has got.

#include <cstdint>

namespace gte {

struct OffscreenFrameTicket {
    std::uint32_t slot = 0;
    // 1-based number of this frame among the real submits; 0 marks a synchronous recording.
    std::uint64_t serial = 0;
    // Every frame with a serial up to this one has finished on the GPU.
    std::uint64_t completedSerial = 0;

    constexpr bool IsAsync() const noexcept { return serial != 0; }
};

} // namespace gte
