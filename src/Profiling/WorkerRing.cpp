#include "WorkerRing.h"

namespace gte::Profiling {

WorkerDrainStats WorkerRing::Drain(
    std::uint64_t frameStartTicks, std::uint64_t frameEndTicks, WorkerEventBuffer& out) noexcept
{
    WorkerDrainStats stats;
    const std::uint64_t head = m_head.load(std::memory_order_acquire);

    // Producers lapped the consumer: the oldest tickets are gone.
    if (head - m_tail > kWorkerRingSlots) {
        const std::uint64_t skipped = head - m_tail - kWorkerRingSlots;
        stats.lost += static_cast<std::uint32_t>(skipped);
        m_lapCount += skipped;
        m_tail = head - kWorkerRingSlots;
    }

    while (m_tail < head) {
        const Slot& slot = m_slots[m_tail & (kWorkerRingSlots - 1)];
        const std::uint64_t expected = m_tail * 2 + 2;

        const std::uint64_t before = slot.seq.load(std::memory_order_acquire);
        if (before < expected) { // ticket reserved but not published yet
            stats.pending = true;
            break;
        }
        if (before > expected) { // a later ticket already reused the slot
            ++stats.lost;
            ++m_lapCount;
            ++m_tail;
            continue;
        }

        const std::uint64_t w0 = slot.words[0].load(std::memory_order_relaxed);
        const std::uint64_t w1 = slot.words[1].load(std::memory_order_relaxed);
        const std::uint64_t w2 = slot.words[2].load(std::memory_order_relaxed);
        std::atomic_thread_fence(std::memory_order_acquire);
        if (slot.seq.load(std::memory_order_relaxed) != before) { // overwritten while reading
            ++stats.lost;
            ++m_lapCount;
            ++m_tail;
            continue;
        }

        WorkerEvent event{};
        event.startTicks = w0;
        event.endTicks = w1;
        event.name = static_cast<NameId>(w2 & 0xFFFFFFFFu);
        event.worker = static_cast<std::uint16_t>((w2 >> 32) & 0xFFFFu);
        event.flags = static_cast<std::uint8_t>((w2 >> 48) & 0xFFu);

        if (event.endTicks > frameEndTicks) { // belongs to a later frame
            stats.pending = true;
            break;
        }
        if (event.startTicks < frameStartTicks) {
            event.flags = static_cast<std::uint8_t>(event.flags | kWorkerEventCarried);
            ++stats.carried;
        }
        if (out.Append(event)) {
            ++stats.drained;
        } else {
            ++stats.droppedFull;
        }
        ++m_tail;
    }
    return stats;
}

} // namespace gte::Profiling
