#include "LiveBlockTable.h"

#include "ProfilingClock.h"

namespace gte::Profiling {

namespace {

struct ThreadSlotCache {
    std::uint32_t tableId = 0; // 0 = empty
    int slot = -1;
};

constexpr std::size_t kCacheEntries = 4;

thread_local std::array<ThreadSlotCache, kCacheEntries> t_cache{};
thread_local std::size_t t_cacheNext = 0;

std::atomic<std::uint32_t> g_nextTableId{ 1 };

} // namespace

std::uint64_t NowMicroseconds() noexcept
{
    return GetProfilingPerformanceCounter() / 1000u;
}

LiveBlockTable::LiveBlockTable() noexcept
    : m_id(g_nextTableId.fetch_add(1, std::memory_order_relaxed))
{
}

int LiveBlockTable::SlotOfThisThread() noexcept
{
    for (const ThreadSlotCache& entry : t_cache) {
        if (entry.tableId == m_id) {
            return entry.slot;
        }
    }

    int slot = -1;
    std::uint32_t next = m_nextSlot.load(std::memory_order_relaxed);
    while (next < kMaxLiveBlockThreads) {
        if (m_nextSlot.compare_exchange_weak(next, next + 1, std::memory_order_relaxed)) {
            slot = static_cast<int>(next);
            break;
        }
    }

    t_cache[t_cacheNext] = ThreadSlotCache{ m_id, slot };
    t_cacheNext = (t_cacheNext + 1) % kCacheEntries;
    return slot;
}

void LiveBlockTable::Begin(const char* site, const char* detail, std::uint64_t nowMicroseconds) noexcept
{
    const int index = SlotOfThisThread();
    if (index < 0) {
        m_unrecorded.fetch_add(1, std::memory_order_relaxed);
        return;
    }

    Slot& slot = m_slots[static_cast<std::size_t>(index)];
    if (++slot.depth != 1) {
        return;
    }

    const std::uint32_t sequence = slot.sequence.load(std::memory_order_relaxed);
    slot.sequence.store(sequence + 1, std::memory_order_relaxed);
    std::atomic_thread_fence(std::memory_order_release);
    slot.site.store(site != nullptr ? site : "", std::memory_order_relaxed);
    slot.detail.store(detail != nullptr ? detail : "", std::memory_order_relaxed);
    slot.startMicroseconds.store(nowMicroseconds != 0 ? nowMicroseconds : 1, std::memory_order_relaxed);
    slot.sequence.store(sequence + 2, std::memory_order_release);
}

void LiveBlockTable::End() noexcept
{
    const int index = SlotOfThisThread();
    if (index < 0) {
        return;
    }

    Slot& slot = m_slots[static_cast<std::size_t>(index)];
    if (slot.depth == 0 || --slot.depth != 0) {
        return;
    }

    const std::uint32_t sequence = slot.sequence.load(std::memory_order_relaxed);
    slot.sequence.store(sequence + 1, std::memory_order_relaxed);
    std::atomic_thread_fence(std::memory_order_release);
    slot.startMicroseconds.store(0, std::memory_order_relaxed);
    slot.sequence.store(sequence + 2, std::memory_order_release);
}

std::size_t LiveBlockTable::Read(std::uint64_t nowMicroseconds, std::span<LiveBlockEntry> out) const noexcept
{
    std::size_t written = 0;
    constexpr int kAttempts = 4;

    for (std::size_t i = 0; i < m_slots.size() && written < out.size(); ++i) {
        const Slot& slot = m_slots[i];
        for (int attempt = 0; attempt < kAttempts; ++attempt) {
            const std::uint32_t before = slot.sequence.load(std::memory_order_acquire);
            if ((before & 1u) != 0) {
                continue;
            }
            const char* site = slot.site.load(std::memory_order_relaxed);
            const char* detail = slot.detail.load(std::memory_order_relaxed);
            const std::uint64_t start = slot.startMicroseconds.load(std::memory_order_relaxed);
            std::atomic_thread_fence(std::memory_order_acquire);
            if (slot.sequence.load(std::memory_order_relaxed) != before) {
                continue;
            }

            if (start != 0) {
                LiveBlockEntry& entry = out[written++];
                entry.threadSlot = static_cast<std::uint32_t>(i);
                entry.site = site != nullptr ? site : "";
                entry.detail = detail != nullptr ? detail : "";
                entry.elapsedMicroseconds = nowMicroseconds > start ? nowMicroseconds - start : 0;
            }
            break;
        }
    }
    return written;
}

} // namespace gte::Profiling
