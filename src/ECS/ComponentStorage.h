#pragma once

#include "Entity.h"

#include <cassert>
#include <cstddef>
#include <cstdint>
#include <utility>
#include <vector>

namespace gte {

// Type-erased base every ComponentStorage<T> implements, so Registry (see
// Registry.h) can hold one homogeneous list of "every pool that has ever
// been touched" and tell each one "entity X was destroyed, drop your
// component for it if you have one" without Registry needing to know T.
// Never used directly outside Registry/tests.
class IComponentPool {
public:
    virtual ~IComponentPool() = default;

    // Removes this entity's component if present. Returns true if a
    // component was actually removed, false if this entity never had one -
    // safe to call unconditionally (e.g. from Registry::DestroyEntity()).
    virtual bool Remove(Entity entity) = 0;

    virtual bool Has(Entity entity) const = 0;
};

// Sparse-set storage for one component type T - O(1) Add()/Remove()/Has()/
// Get(), and cache-friendly linear iteration over exactly the entities that
// actually have a T (see Size()/EntityAt()/ComponentAt() below), never
// scanning "every entity that exists" to find them. Same "address by index,
// never by a hash lookup" philosophy as GpuMemoryTracker (see AGENTS.md,
// "Entity-Component-System"), just keyed on Entity::index instead of a
// GpuResourceHandle.
//
// T is stored by value with no constraints beyond being default-constructible
// and movable - plain data structs only (e.g. Transform), never anything
// owning a live GPU/SDL resource directly. That belongs behind Renderer/
// Window's own RAII types, referenced from a component only by handle/value
// data, exactly like Buffer/RenderTexture are referenced by GpuResourceHandle
// rather than embedding a raw Vulkan handle everywhere.
//
// Not thread-safe (matches the rest of this single-threaded engine).
template <typename T>
class ComponentStorage : public IComponentPool {
public:
    static constexpr std::uint32_t kInvalidDenseIndex = static_cast<std::uint32_t>(-1);

    // Adds (or, if already present, overwrites in place) entity's component.
    // Returns a reference into the dense array - NOT stable across any
    // later Add()/Remove() call on this same pool (the dense array can
    // reallocate or get swap-moved) - re-fetch via Get()/TryGet() if you
    // need it again afterwards, the same "don't hold onto it" caveat
    // AGENTS.md already documents for GpuResourceHandle-tracked resources
    // after a resize.
    template <typename... Args>
    T& Add(Entity entity, Args&&... args)
    {
        EnsureSparseCapacity(entity.index);

        const std::uint32_t existing = m_sparse[entity.index];
        if (existing != kInvalidDenseIndex && m_dense[existing].entity == entity) {
            m_dense[existing].component = T(std::forward<Args>(args)...);
            return m_dense[existing].component;
        }

        m_sparse[entity.index] = static_cast<std::uint32_t>(m_dense.size());
        m_dense.push_back(DenseEntry{ entity, T(std::forward<Args>(args)...) });
        return m_dense.back().component;
    }

    // Swap-with-last removal, the standard sparse-set technique: keeps the
    // dense array tightly packed (no holes to skip during iteration) at the
    // cost of reordering it - never rely on dense iteration order staying
    // stable across a Remove().
    bool Remove(Entity entity) override
    {
        if (!Has(entity)) {
            return false;
        }

        const std::uint32_t denseIndex = m_sparse[entity.index];
        const std::uint32_t lastIndex = static_cast<std::uint32_t>(m_dense.size() - 1);

        if (denseIndex != lastIndex) {
            m_dense[denseIndex] = std::move(m_dense[lastIndex]);
            m_sparse[m_dense[denseIndex].entity.index] = denseIndex;
        }
        m_dense.pop_back();
        m_sparse[entity.index] = kInvalidDenseIndex;
        return true;
    }

    bool Has(Entity entity) const override
    {
        return entity.index < m_sparse.size()
            && m_sparse[entity.index] != kInvalidDenseIndex
            && m_dense[m_sparse[entity.index]].entity == entity;
    }

    T* TryGet(Entity entity)
    {
        return Has(entity) ? &m_dense[m_sparse[entity.index]].component : nullptr;
    }
    const T* TryGet(Entity entity) const
    {
        return Has(entity) ? &m_dense[m_sparse[entity.index]].component : nullptr;
    }

    T& Get(Entity entity)
    {
        T* component = TryGet(entity);
        assert(component != nullptr && "ComponentStorage<T>::Get() called for an entity with no such component - check Has()/TryGet() first");
        return *component;
    }
    const T& Get(Entity entity) const
    {
        const T* component = TryGet(entity);
        assert(component != nullptr && "ComponentStorage<T>::Get() called for an entity with no such component - check Has()/TryGet() first");
        return *component;
    }

    // Dense iteration - visits every live component exactly once, in
    // whatever order Remove()'s swap-with-last happens to leave them (never
    // creation order once a Remove() has happened).
    std::size_t Size() const noexcept { return m_dense.size(); }
    Entity EntityAt(std::size_t denseIndex) const { return m_dense[denseIndex].entity; }
    T& ComponentAt(std::size_t denseIndex) { return m_dense[denseIndex].component; }
    const T& ComponentAt(std::size_t denseIndex) const { return m_dense[denseIndex].component; }

private:
    struct DenseEntry {
        Entity entity;
        T component;
    };

    void EnsureSparseCapacity(std::uint32_t index)
    {
        if (index >= m_sparse.size()) {
            m_sparse.resize(index + 1, kInvalidDenseIndex);
        }
    }

    std::vector<std::uint32_t> m_sparse; // Entity::index -> dense slot, or kInvalidDenseIndex.
    std::vector<DenseEntry> m_dense;
};

} // namespace gte
