#pragma once

// Pool slot bookkeeping, free of any GPU type. A slot goes Free -> Reserved -> Written -> Retiring -> Free.
// Only a Written slot may be enabled in the cluster table, so a node is never drawn before its vertices exist.

#include <cstdint>
#include <deque>
#include <optional>
#include <vector>

namespace gte::terrain::gpu {

enum class SlotState : std::uint8_t { Free, Reserved, Written, Retiring };

// The frame the CPU is building. serial 0 is a synchronous frame: the GPU was idle before it started.
struct SlotFrame {
    std::uint64_t serial = 0;
    std::uint64_t completedSerial = 0; // Every frame up to this serial has finished on the GPU.

    constexpr bool IsAsync() const noexcept { return serial != 0; }
};

// True when a slot retired at `retiredSerial` may be written again: that frame is the first one that no longer
// reads it, so every frame before it must be done, and finished frames never run ahead of it.
constexpr bool SlotReusable(const SlotFrame& frame, std::uint64_t retiredSerial) noexcept
{
    return !frame.IsAsync() || frame.completedSerial >= retiredSerial;
}

class SlotAllocator {
public:
    explicit SlotAllocator(std::uint32_t capacity);

    std::uint32_t Capacity() const noexcept { return static_cast<std::uint32_t>(m_state.size()); }

    // A slot nobody can still read, lowest index first. nullopt when every slot is in use or still in flight.
    std::optional<std::uint32_t> Acquire(const SlotFrame& frame);

    // Reserved -> Written. False for any other state.
    bool MarkWritten(std::uint32_t slot) noexcept;

    // Reserved or Written -> Retiring (Free at once for a synchronous frame). False for a free or retiring slot.
    bool Release(std::uint32_t slot, const SlotFrame& frame);

    // Returns a Reserved slot (its write failed) straight to Free: no frame ever enabled it.
    bool Abandon(std::uint32_t slot) noexcept;

    SlotState State(std::uint32_t slot) const noexcept { return m_state[slot]; }
    // Changes every time a slot is handed out, so a stale holder can be detected.
    std::uint32_t Generation(std::uint32_t slot) const noexcept { return m_generation[slot]; }

    std::uint32_t UsedCount() const noexcept { return m_used; }          // Reserved + Written.
    std::uint32_t RetiringCount() const noexcept { return static_cast<std::uint32_t>(m_retiring.size()); }
    std::uint32_t FreeCount() const noexcept { return Capacity() - m_used - RetiringCount(); }

    // Moves every retiring slot that is reusable for `frame` to Free.
    void Collect(const SlotFrame& frame);

private:
    struct Retired {
        std::uint32_t slot;
        std::uint64_t serial;
    };

    std::vector<SlotState> m_state;
    std::vector<std::uint32_t> m_generation;
    std::vector<std::uint32_t> m_free; // Stack; the lowest index is on top.
    std::deque<Retired> m_retiring;    // Ascending serial.
    std::uint32_t m_used = 0;
};

} // namespace gte::terrain::gpu
