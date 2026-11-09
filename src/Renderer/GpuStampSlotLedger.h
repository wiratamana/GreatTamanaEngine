#pragma once

// Which frame slots hold GPU stamps that were recorded and not read yet. Pure bookkeeping, runs without a device.

#include <array>
#include <cstdint>

namespace gte {

template <std::uint32_t SlotCount>
class BasicGpuStampSlotLedger {
    static_assert(SlotCount >= 1);

public:
    bool Pending(std::uint32_t slot) const noexcept { return slot < SlotCount && m_pending[slot]; }

    // A new frame starts recording into `slot`. True when the stamps still waiting there were never read.
    bool BeginRecord(std::uint32_t slot) noexcept
    {
        return TakePending(slot);
    }

    // The frame in `slot` wrote its stamps; they can be read once its submit is complete.
    void MarkPending(std::uint32_t slot) noexcept
    {
        if (slot < SlotCount) {
            m_pending[slot] = true;
        }
    }

    // Claims the stamps of `slot` for a read. True once per recorded frame; false for an empty or unknown slot.
    bool TakePending(std::uint32_t slot) noexcept
    {
        if (!Pending(slot)) {
            return false;
        }
        m_pending[slot] = false;
        return true;
    }

private:
    std::array<bool, SlotCount> m_pending{};
};

} // namespace gte
