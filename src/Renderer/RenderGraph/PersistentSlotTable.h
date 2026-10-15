#pragma once

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace gte::rg {

// Index + epoch pair naming one slot. A default-constructed ref (epoch 0) is never live.
struct PersistentSlotRef {
    std::uint32_t index = 0;
    std::uint64_t epoch = 0;

    friend bool operator==(const PersistentSlotRef&, const PersistentSlotRef&) = default;
};

// Slot liveness table. A ref goes dead the moment its slot is released, even after the slot is reused.
// Epochs come from one process-wide counter, so a ref from another table is dead too.
class PersistentSlotTable {
public:
    // Takes a free slot (or appends one) and stamps it with a fresh epoch.
    PersistentSlotRef Acquire()
    {
        std::uint32_t index = 0;
        if (!m_freeList.empty()) {
            index = m_freeList.back();
            m_freeList.pop_back();
        } else {
            index = static_cast<std::uint32_t>(m_epochs.size());
            m_epochs.push_back(0);
        }
        m_epochs[index] = NextEpoch();
        return PersistentSlotRef{ index, m_epochs[index] };
    }

    // Kills every ref to the slot and recycles it. Stale or invalid refs are ignored.
    void Release(PersistentSlotRef ref)
    {
        if (!IsLive(ref)) {
            return;
        }
        m_epochs[ref.index] = NextEpoch();
        m_freeList.push_back(ref.index);
    }

    bool IsLive(PersistentSlotRef ref) const noexcept
    {
        return ref.epoch != 0 && ref.index < m_epochs.size() && m_epochs[ref.index] == ref.epoch;
    }

    std::size_t SlotCount() const noexcept { return m_epochs.size(); }

private:
    static std::uint64_t NextEpoch() noexcept
    {
        static std::atomic<std::uint64_t> counter{ 1 };
        return counter.fetch_add(1, std::memory_order_relaxed);
    }

    std::vector<std::uint64_t> m_epochs;
    std::vector<std::uint32_t> m_freeList;
};

} // namespace gte::rg
