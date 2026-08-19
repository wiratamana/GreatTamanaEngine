#include "EntityManager.h"

namespace gte {

Entity EntityManager::Create()
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
        slot.occupied = true;
    } else {
        index = static_cast<std::uint32_t>(m_slots.size());
        m_slots.push_back(Slot{ 1, true });
    }

    m_aliveCount += 1;
    return Entity{ index, m_slots[index].generation };
}

void EntityManager::Destroy(Entity entity)
{
    if (entity.index >= m_slots.size()) {
        return; // Never valid on this manager - ignore.
    }

    Slot& slot = m_slots[entity.index];
    if (!slot.occupied || slot.generation != entity.generation) {
        return; // Already destroyed, or a stale handle from a since-reused slot.
    }

    slot.occupied = false;
    // slot.generation deliberately left as-is (not reset to 0) - Create()
    // bumps forward from it the next time this slot is reused, so two
    // different entities that ever occupied the same slot can never share a
    // handle.
    m_freeList.push_back(entity.index);
    m_aliveCount -= 1;
}

bool EntityManager::IsAlive(Entity entity) const noexcept
{
    if (!entity.IsValid() || entity.index >= m_slots.size()) {
        return false;
    }
    const Slot& slot = m_slots[entity.index];
    return slot.occupied && slot.generation == entity.generation;
}

} // namespace gte
