#pragma once

#include "Entity.h"

#include <cstddef>
#include <cstdint>
#include <limits>
#include <span>
#include <vector>

namespace gte {

// Entity set with O(1) Insert / Contains / Clear and no hashing: one {epoch, generation} slot per entity index.
// Clear() only bumps the epoch and keeps the memory. Live entities never share an index. Not thread safe.
class EntityMarkSet {
public:
    // Marks the entity. Invalid entities are ignored.
    void Insert(Entity entity)
    {
        if (!entity.IsValid()) {
            return;
        }
        if (entity.index >= m_slots.size()) {
            Grow(entity.index);
        }
        m_slots[entity.index] = Slot{ m_epoch, entity.generation };
    }

    void Insert(std::span<const Entity> entities)
    {
        for (const Entity entity : entities) {
            Insert(entity);
        }
    }

    bool Contains(Entity entity) const noexcept
    {
        return entity.index < m_slots.size() && m_slots[entity.index].epoch == m_epoch
            && m_slots[entity.index].generation == entity.generation && entity.IsValid();
    }

    // Forgets every entity. Keeps the slot memory.
    void Clear() noexcept
    {
        if (m_epoch == std::numeric_limits<std::uint32_t>::max()) {
            m_slots.assign(m_slots.size(), Slot{});
            m_epoch = 1;
            return;
        }
        ++m_epoch;
    }

private:
    struct Slot {
        std::uint32_t epoch = 0; // 0 never matches a live epoch
        std::uint32_t generation = 0;
    };

    void Grow(std::uint32_t index)
    {
        const std::size_t needed = static_cast<std::size_t>(index) + 1;
        std::size_t target = m_slots.size() + m_slots.size() / 2;
        m_slots.resize(target > needed ? target : needed);
    }

    std::vector<Slot> m_slots;
    std::uint32_t m_epoch = 1;
};

} // namespace gte
