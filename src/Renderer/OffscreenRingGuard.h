#pragma once

// CPU-side proof that a host write into one copy of a per-slot ring cannot race a frame still on the GPU.
// The validation layer cannot see a memcpy into mapped memory, so every ring owner asserts this itself.

#include "OffscreenFrameTicket.h"

#include <cstdint>

namespace gte {

// A write by frame `ticket` into a copy last used by serial `lastUsedSerial` is safe when that frame is
// done, or when the same frame already owns the copy. A synchronous ticket drained the GPU before it ran.
constexpr bool RingCopyWritable(const OffscreenFrameTicket& ticket, std::uint64_t lastUsedSerial) noexcept
{
    return !ticket.IsAsync() || lastUsedSerial == ticket.serial || ticket.completedSerial >= lastUsedSerial;
}

// One per copy of a ring. Records the newest frame that used the copy, so the GPU may still read it.
class RingCopyUse {
public:
    bool Writable(const OffscreenFrameTicket& ticket) const noexcept { return RingCopyWritable(ticket, m_lastUsedSerial); }

    // Claims the copy for `ticket`'s frame; false (and no change) when the copy is still in flight.
    bool Claim(const OffscreenFrameTicket& ticket) noexcept
    {
        if (!Writable(ticket)) {
            return false;
        }
        Note(ticket);
        return true;
    }

    // Records a read by `ticket`'s frame. A copy the GPU only reads may be shared by frames in flight.
    void Note(const OffscreenFrameTicket& ticket) noexcept
    {
        if (ticket.IsAsync() && ticket.serial > m_lastUsedSerial) {
            m_lastUsedSerial = ticket.serial;
        }
    }

    // Every frame that used the copy is done, for example after the GPU was drained.
    void Reset() noexcept { m_lastUsedSerial = 0; }

    std::uint64_t LastUsedSerial() const noexcept { return m_lastUsedSerial; }

private:
    std::uint64_t m_lastUsedSerial = 0;
};

// The copy a frame writes: its slot, folded into the ring size. Equal to the slot for every valid ticket.
constexpr std::uint32_t RingCopyOf(const OffscreenFrameTicket& ticket, std::uint32_t copies) noexcept
{
    return copies == 0 ? 0u : ticket.slot % copies;
}

} // namespace gte
