#pragma once

// Waits in progress, one record per thread, readable from any thread without a lock.

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <span>

namespace gte::Profiling {

inline constexpr std::size_t kMaxLiveBlockThreads = 8;

// Microseconds from the profiling clock.
std::uint64_t NowMicroseconds() noexcept;

struct LiveBlockEntry {
    std::uint32_t threadSlot = 0;
    const char* site = "";   // static text
    const char* detail = ""; // static text, may be empty
    std::uint64_t elapsedMicroseconds = 0;
};

// Each thread owns one slot (first come, first served). Threads beyond the limit are counted, not recorded.
// A slot is a seqlock: the owner writes, any thread reads. Nested waits record the outermost one.
class LiveBlockTable {
public:
    LiveBlockTable() noexcept;

    void Begin(const char* site, const char* detail, std::uint64_t nowMicroseconds) noexcept;
    void End() noexcept;

    // Copies the waits in progress into `out`. Returns how many entries were written.
    std::size_t Read(std::uint64_t nowMicroseconds, std::span<LiveBlockEntry> out) const noexcept;

    // Waits that started on a thread without a slot.
    std::uint32_t UnrecordedCount() const noexcept { return m_unrecorded.load(std::memory_order_relaxed); }

private:
    struct alignas(64) Slot {
        std::atomic<std::uint32_t> sequence{ 0 }; // odd while the owner writes
        std::atomic<const char*> site{ nullptr };
        std::atomic<const char*> detail{ nullptr };
        std::atomic<std::uint64_t> startMicroseconds{ 0 }; // 0 = not blocked
        std::uint32_t depth = 0;                           // owner thread only
    };

    // Slot of the calling thread for this table, or -1 when none is left.
    int SlotOfThisThread() noexcept;

    std::array<Slot, kMaxLiveBlockThreads> m_slots{};
    std::atomic<std::uint32_t> m_nextSlot{ 0 };
    std::atomic<std::uint32_t> m_unrecorded{ 0 };
    std::uint32_t m_id = 0;
};

} // namespace gte::Profiling
