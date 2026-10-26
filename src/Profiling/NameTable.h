#pragma once

#include "ProfilingBudget.h"
#include "ScopeEvent.h"

#include <array>
#include <atomic>
#include <cstdint>
#include <mutex>
#include <string_view>

namespace gte::Profiling {

// Interns names into a fixed arena. Never allocates after construction.
// Intern takes a lock (cold path). Resolve is lock-free for any id already handed out.
class NameTable {
public:
    NameTable() noexcept;
    NameTable(const NameTable&) = delete;
    NameTable& operator=(const NameTable&) = delete;

    // Same text gives the same id. Empty text gives kInvalidNameId. A full table gives kOverflowNameId.
    NameId Intern(std::string_view text) noexcept;
    NameId Intern(const char* text) noexcept;

    // Stable for the table's lifetime. Unknown or invalid ids resolve to "".
    const char* Resolve(NameId id) const noexcept;

    // Ids handed out so far, including the overflow id.
    std::uint32_t Count() const noexcept { return m_count.load(std::memory_order_acquire); }

    // How many Intern calls hit a full table or an over-long name.
    std::uint32_t OverflowCount() const noexcept { return m_overflow.load(std::memory_order_relaxed); }

private:
    struct Entry {
        std::uint32_t offset;
        std::uint32_t length;
    };
    struct Slot {
        std::uint32_t id; // 0 = empty
        std::uint32_t hash;
    };

    mutable std::mutex m_mutex;
    std::array<char, kNameArenaBytes> m_arena{};
    std::array<Entry, kMaxNames + 1> m_entries{}; // indexed by id
    std::array<Slot, kNameSlots> m_slots{};
    std::uint32_t m_arenaUsed = 0;
    std::atomic<std::uint32_t> m_count{ 0 };
    std::atomic<std::uint32_t> m_overflow{ 0 };
};

// Process-wide table.
NameTable& GlobalNameTable() noexcept;
NameId InternName(const char* text) noexcept;
const char* ResolveName(NameId id) noexcept;

} // namespace gte::Profiling
