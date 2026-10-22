#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <utility>
#include <vector>

namespace gte {

// Owning slot map keyed by a generational handle: insert, remove and lookup with a stale-handle guard.
// HandleT is a POD with `index` and `generation`, constructible as HandleT{index, generation}.
// The pool owns T until Remove() or destruction. Not thread-safe.
template <typename T, typename HandleT>
class ResourcePool {
public:
    ResourcePool() = default;

    ResourcePool(const ResourcePool&) = delete;
    ResourcePool& operator=(const ResourcePool&) = delete;
    ResourcePool(ResourcePool&&) = default;
    ResourcePool& operator=(ResourcePool&&) = default;

    // Takes ownership of `value`: reuses a freed slot (generation bumped, skipping 0) or grows the slot array.
    HandleT Insert(T&& value)
    {
        std::uint32_t index;
        if (!m_freeList.empty()) {
            index = m_freeList.back();
            m_freeList.pop_back();

            Slot& slot = m_slots[index];
            slot.generation += 1;
            if (slot.generation == 0) {
                slot.generation = 1; // Skip the reserved 0 sentinel on wraparound.
            }
            slot.value.emplace(std::move(value));
        } else {
            index = static_cast<std::uint32_t>(m_slots.size());
            Slot slot;
            slot.generation = 1;
            slot.value.emplace(std::move(value));
            m_slots.push_back(std::move(slot));
        }

        return HandleT{ index, m_slots[index].generation };
    }

    // Destroys the resource and frees its slot. No-op for an invalid or stale handle; true if something was removed.
    bool Remove(HandleT handle)
    {
        if (!IsValid(handle)) {
            return false;
        }
        m_slots[handle.index].value.reset();
        m_freeList.push_back(handle.index);
        return true;
    }

    // True only for a handle from Insert() that has not been Remove()'d: false for invalid, unknown or stale handles.
    bool IsValid(HandleT handle) const noexcept
    {
        return handle.generation != 0 && handle.index < m_slots.size() && m_slots[handle.index].value.has_value() &&
            m_slots[handle.index].generation == handle.generation;
    }

    T* TryGet(HandleT handle)
    {
        return IsValid(handle) ? &(*m_slots[handle.index].value) : nullptr;
    }
    const T* TryGet(HandleT handle) const
    {
        return IsValid(handle) ? &(*m_slots[handle.index].value) : nullptr;
    }

    // Number of live (inserted and not removed) resources.
    std::size_t LiveCount() const noexcept { return m_slots.size() - m_freeList.size(); }

private:
    struct Slot {
        std::optional<T> value;
        std::uint32_t generation = 0; // Persists across free/reuse cycles - never reset to 0.
    };

    std::vector<Slot> m_slots;
    std::vector<std::uint32_t> m_freeList;
};

} // namespace gte
