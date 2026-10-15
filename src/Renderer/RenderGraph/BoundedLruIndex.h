#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

namespace gte::rg {

// Fixed-size key -> slot index with least-recently-used eviction. It stores keys only; values live in a
// parallel array owned by the caller, indexed by the returned slot.
template <typename Key, std::size_t Capacity>
class BoundedLruIndex {
    static_assert(Capacity > 0, "an LRU index needs at least one slot");

public:
    enum class Outcome : std::uint8_t { Hit, Inserted, Evicted };

    struct Result {
        std::size_t slot = 0;
        Outcome outcome = Outcome::Hit;
    };

    // Hit: `key` already owns `slot`. Inserted / Evicted: `slot` is newly claimed (Evicted took the least
    // recently used one); the caller replaces the value there, or calls Release() if it cannot.
    Result Touch(const Key& key, std::uint64_t tick)
    {
        std::size_t freeSlot = Capacity;
        std::size_t oldestSlot = Capacity;
        for (std::size_t i = 0; i < Capacity; ++i) {
            if (!m_used[i]) {
                freeSlot = freeSlot == Capacity ? i : freeSlot;
                continue;
            }
            if (m_keys[i] == key) {
                m_lastUsed[i] = tick;
                return Result{ i, Outcome::Hit };
            }
            if (oldestSlot == Capacity || m_lastUsed[i] < m_lastUsed[oldestSlot]) {
                oldestSlot = i;
            }
        }
        const bool hasFree = freeSlot != Capacity;
        const std::size_t slot = hasFree ? freeSlot : oldestSlot;
        m_keys[slot] = key;
        m_lastUsed[slot] = tick;
        m_used[slot] = true;
        return Result{ slot, hasFree ? Outcome::Inserted : Outcome::Evicted };
    }

    // Frees a slot claimed by Touch() whose value could not be built.
    void Release(std::size_t slot) noexcept { m_used[slot] = false; }

private:
    std::array<Key, Capacity> m_keys{};
    std::array<std::uint64_t, Capacity> m_lastUsed{};
    std::array<bool, Capacity> m_used{};
};

} // namespace gte::rg
