#pragma once

// Bookkeeping of the offscreen frame ring: slot and serial of the frame being built, and which slots still hold
// a submitted frame. Holds no GPU objects, so it runs without a device.

#include "OffscreenFrameTicket.h"

#include <algorithm>
#include <array>
#include <cstdint>

namespace gte {

class OffscreenFrameRing {
public:
    static constexpr std::uint32_t kMaxSlots = 4;

    explicit OffscreenFrameRing(std::uint32_t slotCount) noexcept
        : m_slotCount(std::clamp(slotCount, 1u, kMaxSlots))
    {
    }

    std::uint32_t SlotCount() const noexcept { return m_slotCount; }

    // The frame being built: slot is the real submits so far modulo the slot count, serial is that count plus one.
    OffscreenFrameTicket Current() const noexcept
    {
        return OffscreenFrameTicket{ static_cast<std::uint32_t>(m_submitted % m_slotCount), m_submitted + 1, m_completed };
    }

    // True from a successful slot wait until the frame is submitted; a skipped frame keeps it open.
    bool FrameOpen() const noexcept { return m_frameOpen; }
    void OpenFrame() noexcept { m_frameOpen = true; }

    // True while the frame last submitted into `slot` has not been noted complete.
    bool SlotPending(std::uint32_t slot) const noexcept { return slot < m_slotCount && m_slotSerial[slot] > m_completed; }
    bool AnyPending() const noexcept { return m_submitted > m_completed; }

    // A real submit: the slot now holds the frame and the next frame takes the next slot.
    void NoteSubmitted() noexcept
    {
        ++m_submitted;
        m_slotSerial[(m_submitted - 1) % m_slotCount] = m_submitted;
        m_frameOpen = false;
    }

    // Call only after a wait on the slot's fence completed. Frames complete in submit order.
    void NoteSlotComplete(std::uint32_t slot) noexcept
    {
        if (slot < m_slotCount) {
            m_completed = std::max(m_completed, m_slotSerial[slot]);
        }
    }

    // Call only after a wait on every pending slot completed.
    void NoteAllComplete() noexcept { m_completed = m_submitted; }

    std::uint64_t Submitted() const noexcept { return m_submitted; }
    std::uint64_t CompletedSerial() const noexcept { return m_completed; }

private:
    std::uint32_t m_slotCount = 1;
    std::uint64_t m_submitted = 0;
    std::uint64_t m_completed = 0;
    bool m_frameOpen = false;
    std::array<std::uint64_t, kMaxSlots> m_slotSerial{};
};

} // namespace gte
