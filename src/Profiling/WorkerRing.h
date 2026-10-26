#pragma once

#include "EventBuffer.h"
#include "ProfilingBudget.h"
#include "ScopeEvent.h"

#include <array>
#include <atomic>
#include <cstdint>
#include <type_traits>

namespace gte::Profiling {

inline constexpr std::uint8_t kWorkerEventCarried = 1; // started before the frame it is reported in

// One finished job scope on a worker thread. Ticks are nanoseconds.
struct WorkerEvent {
    std::uint64_t startTicks;
    std::uint64_t endTicks;
    NameId name;
    std::uint16_t worker;
    std::uint8_t flags;
    std::uint8_t reserved;
};
static_assert(sizeof(WorkerEvent) == kWorkerEventBytes, "WorkerEvent must stay 24 bytes");
static_assert(std::is_trivially_copyable_v<WorkerEvent>);

using WorkerEventBuffer = EventBuffer<WorkerEvent, kMaxWorkerEventsPerFrame>;

struct WorkerDrainStats {
    std::uint32_t drained = 0;     // moved into the frame buffer
    std::uint32_t carried = 0;     // of those, started before the frame
    std::uint32_t droppedFull = 0; // frame buffer was full
    std::uint32_t lost = 0;        // overwritten by producers before they were read
    bool pending = false;          // later events are still being written or end after the frame
};

// Lock-free ring: many workers push, the main thread drains once per frame.
// A producer that laps the consumer overwrites the oldest slot and the loss is counted.
class WorkerRing {
public:
    // Any thread. Returns false when disabled. Never blocks and never allocates.
    // A producer stalled for a whole lap can rarely publish a mixed event.
    bool Push(const WorkerEvent& event) noexcept
    {
        if (!m_enabled.load(std::memory_order_relaxed)) {
            return false;
        }
        const std::uint64_t ticket = m_head.fetch_add(1, std::memory_order_relaxed);
        Slot& slot = m_slots[ticket & (kWorkerRingSlots - 1)];

        slot.seq.store(ticket * 2 + 1, std::memory_order_relaxed); // odd: being written
        std::atomic_thread_fence(std::memory_order_release);
        slot.words[0].store(event.startTicks, std::memory_order_relaxed);
        slot.words[1].store(event.endTicks, std::memory_order_relaxed);
        slot.words[2].store(Pack(event), std::memory_order_relaxed);
        slot.seq.store(ticket * 2 + 2, std::memory_order_release); // even: published
        return true;
    }

    // Any thread. The only flag a producer reads.
    void SetEnabled(bool enabled) noexcept { m_enabled.store(enabled, std::memory_order_relaxed); }
    bool Enabled() const noexcept { return m_enabled.load(std::memory_order_relaxed); }

    // Main thread only. Moves events that ended by frameEndTicks into out.
    // Events that started before frameStartTicks get kWorkerEventCarried.
    WorkerDrainStats Drain(std::uint64_t frameStartTicks, std::uint64_t frameEndTicks, WorkerEventBuffer& out) noexcept;

    // Events lost to overwriting since construction.
    std::uint64_t LapCount() const noexcept { return m_lapCount; }

private:
    struct Slot {
        std::atomic<std::uint64_t> seq{ 0 };
        std::array<std::atomic<std::uint64_t>, 3> words{};
    };
    static_assert(sizeof(Slot) == kWorkerRingSlotBytes);
    static_assert(std::atomic<std::uint64_t>::is_always_lock_free);

    static std::uint64_t Pack(const WorkerEvent& e) noexcept
    {
        return static_cast<std::uint64_t>(e.name) | (static_cast<std::uint64_t>(e.worker) << 32)
            | (static_cast<std::uint64_t>(e.flags) << 48);
    }

    std::array<Slot, kWorkerRingSlots> m_slots{};
    alignas(64) std::atomic<std::uint64_t> m_head{ 0 }; // next ticket to hand out
    std::atomic<bool> m_enabled{ true };
    alignas(64) std::uint64_t m_tail = 0; // main thread only: next ticket to read
    std::uint64_t m_lapCount = 0;
};

} // namespace gte::Profiling
