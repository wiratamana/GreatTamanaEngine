#pragma once

#include "ComponentStorage.h"
#include "Entity.h"
#include "EntityManager.h"

#include <cassert>
#include <cstddef>
#include <memory>
#include <utility>
#include <vector>

namespace gte {

namespace detail {

// Assigns each distinct component type T a small, dense, monotonically
// increasing integer id the first time it's used (a Meyer's-singleton
// counter per T) - deliberately NOT std::type_index/RTTI-based hashing, so
// Registry can index m_pools directly by id (an O(1) array lookup) instead
// of hashing a type_index on every single AddComponent<T>()/GetComponent<T>()
// call. Same "no hashing on the hot path, index directly instead" philosophy
// as GpuMemoryTracker's handle-indexed slot array (see AGENTS.md).
inline std::size_t NextComponentTypeId() noexcept
{
    static std::size_t next = 0;
    return next++;
}

template <typename T>
std::size_t ComponentTypeId() noexcept
{
    static const std::size_t id = NextComponentTypeId();
    return id;
}

} // namespace detail

// Owns one EntityManager plus one ComponentStorage<T> per distinct
// component type ever used with it - the engine's Scene/World object.
// Entities and their components are plain data (see Components/Transform.h
// for the first real component); Registry itself has no rendering/gameplay
// behavior and no dependency on Renderer/Window/SDL whatsoever, which keeps
// it Tier-1-testable (see AGENTS.md, "Testability & Regression Safety") - it
// operates purely on Entity/EntityManager/ComponentStorage<T>, all plain
// data/logic with no live GPU device or SDL window required.
//
// Not thread-safe (matches the rest of this single-threaded engine).
class Registry {
public:
    Registry() = default;

    Entity CreateEntity() { return m_entities.Create(); }

    // Destroys the entity AND removes its component from every pool that
    // has ever been touched on this Registry - an entity is never left with
    // a dangling component in some pool this forgot about, no matter which
    // component types it happened to carry. Safe to call on an
    // already-dead or otherwise invalid entity (no-op).
    void DestroyEntity(Entity entity)
    {
        if (!m_entities.IsAlive(entity)) {
            return;
        }
        for (auto& pool : m_pools) {
            if (pool) {
                pool->Remove(entity);
            }
        }
        m_entities.Destroy(entity);
    }

    bool IsAlive(Entity entity) const noexcept { return m_entities.IsAlive(entity); }
    std::size_t AliveEntityCount() const noexcept { return m_entities.AliveCount(); }

    template <typename T, typename... Args>
    T& AddComponent(Entity entity, Args&&... args)
    {
        assert(m_entities.IsAlive(entity) && "AddComponent() called for a dead/invalid entity");
        return Storage<T>().Add(entity, std::forward<Args>(args)...);
    }

    template <typename T>
    bool RemoveComponent(Entity entity)
    {
        return Storage<T>().Remove(entity);
    }

    template <typename T>
    bool HasComponent(Entity entity) const
    {
        const ComponentStorage<T>* pool = FindStorage<T>();
        return pool != nullptr && pool->Has(entity);
    }

    template <typename T>
    T* TryGetComponent(Entity entity)
    {
        ComponentStorage<T>* pool = FindStorage<T>();
        return pool != nullptr ? pool->TryGet(entity) : nullptr;
    }
    template <typename T>
    const T* TryGetComponent(Entity entity) const
    {
        const ComponentStorage<T>* pool = FindStorage<T>();
        return pool != nullptr ? pool->TryGet(entity) : nullptr;
    }

    template <typename T>
    T& GetComponent(Entity entity)
    {
        return Storage<T>().Get(entity);
    }
    template <typename T>
    const T& GetComponent(Entity entity) const
    {
        return Storage<T>().Get(entity);
    }

    // Gets (creating on first use) the ComponentStorage<T> pool for T -
    // exposed publicly so calling code can iterate Size()/EntityAt()/
    // ComponentAt() directly for a single-component-type view. A real
    // multi-component View<T...> that intersects several pools at once is a
    // natural follow-up once there's an actual system that needs one - see
    // AGENTS.md ("Entity-Component-System").
    template <typename T>
    ComponentStorage<T>& Storage()
    {
        const std::size_t id = detail::ComponentTypeId<T>();
        if (id >= m_pools.size()) {
            m_pools.resize(id + 1);
        }
        if (!m_pools[id]) {
            m_pools[id] = std::make_unique<ComponentStorage<T>>();
        }
        return static_cast<ComponentStorage<T>&>(*m_pools[id]);
    }

private:
    template <typename T>
    ComponentStorage<T>* FindStorage()
    {
        const std::size_t id = detail::ComponentTypeId<T>();
        if (id >= m_pools.size() || !m_pools[id]) {
            return nullptr;
        }
        return static_cast<ComponentStorage<T>*>(m_pools[id].get());
    }
    template <typename T>
    const ComponentStorage<T>* FindStorage() const
    {
        const std::size_t id = detail::ComponentTypeId<T>();
        if (id >= m_pools.size() || !m_pools[id]) {
            return nullptr;
        }
        return static_cast<const ComponentStorage<T>*>(m_pools[id].get());
    }

    EntityManager m_entities;
    std::vector<std::unique_ptr<IComponentPool>> m_pools; // indexed by detail::ComponentTypeId<T>()
};

} // namespace gte
