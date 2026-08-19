#pragma once

#include <cstdint>
#include <optional>
#include <utility>
#include <vector>

namespace gte {

// Generic owning slot-map keyed by a generational handle (HandleT) - the
// same slot + free-list + generation-bump recipe as EntityManager (see
// ECS/EntityManager.h/.cpp) and GpuMemoryTracker (see
// Memory/GpuMemoryTracker.h/.cpp), just genuinely shared as ONE generic
// implementation this time, rather than hand-copied a third/fourth time -
// unlike EntityManager vs. GpuMemoryTracker (which differ in real ways: one
// tracks aggregate byte totals + Editor debug names, the other just
// alive/dead), there is zero semantic divergence between "a pool of Mesh"
// and "a pool of Pipeline": both need exactly insert/remove/lookup with a
// generation guard, differing only in the payload type T. See AGENTS.md
// ("Entity-Component-System") for why this project uses this exact pattern
// (index + generation, minted only by the owning container) everywhere a
// handle is needed.
//
// HandleT must be a plain POD shaped like Entity/GpuResourceHandle/
// MeshHandle/PipelineHandle - an `index` + `generation` field pair,
// aggregate-constructible as `HandleT{index, generation}` - see
// MeshHandle.h/PipelineHandle.h.
//
// Unlike GpuMemoryTracker (which only ever TRACKS a resource that owns
// itself), this pool OWNS T for its entire lifetime - constructed in
// Insert(), destroyed in Remove()/the pool's own destructor. This is what
// lets a plain-data component (e.g. ECS/Components/MeshRenderer.h) reference
// a live Mesh/Pipeline by a cheap handle without ever embedding/owning one
// directly, per the rule AGENTS.md lays out for exactly that component.
//
// Not thread-safe (matches the rest of this single-threaded engine).
template <typename T, typename HandleT>
class ResourcePool {
public:
    ResourcePool() = default;

    ResourcePool(const ResourcePool&) = delete;
    ResourcePool& operator=(const ResourcePool&) = delete;
    ResourcePool(ResourcePool&&) = default;
    ResourcePool& operator=(ResourcePool&&) = default;

    // Takes ownership of `value`, returning a fresh handle - either reusing
    // a freed slot (bumping its generation forward, with the same
    // wraparound-skips-zero guard as EntityManager::Create()) or growing the
    // slot array.
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

    // Destroys this handle's resource and frees its slot for reuse. Safe to
    // call with an already-removed or otherwise invalid/stale handle
    // (no-op) - mirrors EntityManager::Destroy()/GpuMemoryTracker::Untrack()'s
    // same safety guarantee. Returns true if a resource was actually
    // removed.
    bool Remove(HandleT handle)
    {
        if (!IsValid(handle)) {
            return false;
        }
        m_slots[handle.index].value.reset();
        m_freeList.push_back(handle.index);
        return true;
    }

    // True only for a handle returned by Insert() that hasn't since been
    // Remove()'d - false for a default-constructed/invalid handle, a
    // not-yet-inserted index, or a stale handle from a since-reused slot.
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

private:
    struct Slot {
        std::optional<T> value;
        std::uint32_t generation = 0; // Persists across free/reuse cycles - never reset to 0.
    };

    std::vector<Slot> m_slots;
    std::vector<std::uint32_t> m_freeList;
};

} // namespace gte
