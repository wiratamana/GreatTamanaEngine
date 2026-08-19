#pragma once

#include "Entity.h"

#include <cstddef>
#include <cstdint>
#include <vector>

namespace gte {

// Owns entity handle allocation/recycling - the ECS equivalent of
// GpuMemoryTracker (see Renderer/Memory/GpuMemoryTracker.h/.cpp): the exact
// same slot + free-list + generation-bump pattern, just tracking "is this
// entity alive" instead of a GPU allocation record. Deliberately holds NO
// component data itself - see Registry (Registry.h), which owns exactly one
// EntityManager plus every ComponentStorage<T> pool that exists.
//
// Not thread-safe (matches the rest of this single-threaded engine).
class EntityManager {
public:
    EntityManager() = default;

    // Allocates a fresh Entity - either reusing a free slot (bumping its
    // generation forward, with the same wraparound-skips-zero guard as
    // GpuMemoryTracker::Track()) or growing the slot array.
    Entity Create();

    // Marks `entity`'s slot as free for reuse. Safe to call with an
    // already-destroyed or otherwise invalid/stale entity (no-op) - mirrors
    // GpuMemoryTracker::Untrack()'s same safety guarantee.
    void Destroy(Entity entity);

    // True only for an entity returned by Create() that hasn't since been
    // Destroy()'d - false for a default-constructed entity/kInvalidEntity,
    // a not-yet-created index, or a stale handle from a since-reused slot.
    bool IsAlive(Entity entity) const noexcept;

    // Number of entities CURRENTLY alive (not the high-water mark of slots
    // ever allocated).
    std::size_t AliveCount() const noexcept { return m_aliveCount; }

private:
    struct Slot {
        std::uint32_t generation = 0; // Persists across free/reuse cycles - never reset to 0.
        bool occupied = false;
    };

    std::vector<Slot> m_slots;
    std::vector<std::uint32_t> m_freeList;
    std::size_t m_aliveCount = 0;
};

} // namespace gte
