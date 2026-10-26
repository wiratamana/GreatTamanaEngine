#pragma once

// Which submitted frame each presenter slot holds. A pipelined timing read is trusted only for a frame that was
// really submitted and is still inside the ring. Vulkan-free.

#include "GpuTiming.h"

#include <array>
#include <cstdint>

namespace gte {

class GpuFrameRing {
public:
    enum class Status : std::uint8_t {
        None,    // The slot holds no submitted frame.
        Ready,   // The frame is inside the ring and can be read.
        Dropped, // The frame left the ring before it was read.
    };

    struct Taken {
        Status status = Status::None;
        std::uint64_t frameIndex = 0;
    };

    // Records that `slot` now holds the frame submitted as number `frameIndex`.
    void Submitted(std::uint32_t slot, std::uint64_t frameIndex) noexcept
    {
        if (slot < kGpuFramesInFlight) {
            m_entries[slot] = Entry{ frameIndex, true };
        }
    }

    // Empties `slot`. `submittedFrames` is the number of real submits so far. A frame older than the ring is Dropped.
    Taken Take(std::uint32_t slot, std::uint64_t submittedFrames) noexcept
    {
        if (slot >= kGpuFramesInFlight || !m_entries[slot].valid) {
            return {};
        }
        const std::uint64_t frameIndex = m_entries[slot].frameIndex;
        m_entries[slot].valid = false;
        const bool inRing = submittedFrames >= frameIndex && submittedFrames - frameIndex < kGpuFramesInFlight;
        return Taken{ inRing ? Status::Ready : Status::Dropped, frameIndex };
    }

private:
    struct Entry {
        std::uint64_t frameIndex = 0;
        bool valid = false;
    };

    std::array<Entry, kGpuFramesInFlight> m_entries{};
};

} // namespace gte
