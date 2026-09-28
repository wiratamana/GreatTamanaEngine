#pragma once

#include "ComponentStorage.h"
#include "Entity.h"
#include "EntityManager.h"

#include <cassert>
#include <cstddef>
#include <memory>
#include <string>
#include <typeinfo>
#include <utility>
#include <vector>

namespace gte {

namespace detail {

// editor-core-separation-15 campaign (Project Assembly Hot Reload plan,
// BIG-STEP 4), PHASE5 - REAL FIX for a confirmed, live, active
// memory-corruption bug (see task_manager/editor-core-separation-15/
// PHASE5_COMPLETION_REPORT.md's "New gaps found (fixed)" section for the
// full incident writeup). ComponentTypeId<T>() used to hand out ids from a
// per-BINARY-IMAGE local static counter (a Meyer's singleton INSIDE this
// header-only inline function) - correct only as long as every caller of
// Storage<T>()/FindStorage<T>() for a given T is compiled into the SAME
// image. A Project Assembly .dll gets its OWN, independent copy of that
// counter (standard, expected PE-COFF behavior for an inline function's
// function-local static across a module boundary) - so the exact same C++
// type could get assigned a DIFFERENT numeric slot depending on whether the
// call happened to run inside GreatTamanaEditor.exe's own compiled code or
// inside a Project Assembly .dll's - and since Registry::m_pools is one
// single, shared std::vector, two UNRELATED types landing on the same
// numeric slot silently REINTERPRET each other's live ComponentStorage<T>
// object, corrupting real engine data (confirmed, live: a Project
// Assembly's own first custom component type collided with the real,
// built-in Transform component's slot and corrupted its storage the moment
// registry.AddComponent<CustomType>() ran).
//
// The fix: resolve each type's slot through ONE single, shared,
// PROCESS-WIDE authority - ResolveComponentTypeIdByName() below - defined
// out-of-line in Registry.cpp (compiled ONLY into gte_core, i.e. into
// GreatTamanaEditor.exe's own image) and correctly resolved/imported as the
// SAME single exported symbol from a Project Assembly .dll, mirroring
// ComponentTypeRegistry::Instance()'s own already-proven-correct shape (a
// plain out-of-line singleton in a real .cpp file, never a header-only
// inline/template Meyer's singleton). typeid(T).name() is used ONLY as a
// stable per-type STRING KEY into that shared table (never for RTTI-based
// dynamic dispatch, never on any hot path - looked up exactly once per (T,
// image) pair, then cached in ComponentTypeId<T>()'s own local static
// below, so every ordinary AddComponent<T>()/GetComponent<T>() call still
// pays zero extra cost) - this restores the original header comment's
// "small, dense, monotonically increasing integer id" property globally,
// process-wide, instead of merely per-image.
std::size_t ResolveComponentTypeIdByName(const std::string& mangledTypeName);

template <typename T>
std::size_t ComponentTypeId() noexcept
{
    static const std::size_t id = ResolveComponentTypeIdByName(typeid(T).name());
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

    // editor-core-separation-15 campaign (Project Assembly Hot Reload plan,
    // BIG-STEP 4), PHASE5 - a SECOND, related fix for a confirmed, live
    // crash found alongside the ComponentTypeId<T>() collision bug above.
    // A Project Assembly .dll's own custom component type's ComponentStorage<T>
    // pool object is heap-allocated (Storage<T>() above) but its VIRTUAL
    // FUNCTION TABLE (IComponentPool - ECS/ComponentStorage.h) is compiled
    // INTO that same .dll's own image. Once that .dll is FreeLibrary()'d
    // (ProjectAssemblyHost::UnloadProjectAssembly(), BIG-STEP 2), the pool
    // object itself still physically sits in Registry::m_pools (nothing
    // about an ordinary DLL unload touches the OTHER, unrelated Registry
    // object it happens to live inside) - but its vtable pointer now
    // dangles into unmapped memory. Any LATER virtual call through that
    // same IComponentPool* (e.g. DestroyEntity()'s own `pool->Remove(entity)`
    // loop over EVERY pool, called by ClearEntireScene() during ANY future
    // hot-reload cycle's own HOOK POINT B) is undefined behavior - confirmed,
    // live, via gdb: a real SIGSEGV inside Registry::DestroyEntity() on the
    // SECOND consecutive hot-reload cycle in one process session (the FIRST
    // cycle's own dangling pointer happened to still be readable/reusable
    // right after FreeLibrary() returned - the OS had not yet reused that
    // address range - making this bug intermittent rather than always-
    // reproducible on the very first reload).
    //
    // The fix: ResetStoragePool<T>() destroys (resets to nullptr) T's own
    // pool - called by ComponentTypeDescriptor::destroyPool
    // (ECS/Reflection/ComponentTypeRegistry.inl), in turn called by
    // ProjectAssemblyRegistrationLedger::UnregisterEverythingFor() for every
    // CUSTOM component typeName a specific Project Assembly's own ledger
    // entry recorded - BEFORE that assembly's .dll is FreeLibrary()'d, and
    // BEFORE ComponentTypeRegistry::UnregisterDescriptor() removes its
    // descriptor. Never called for a BUILT-IN component type (Transform,
    // Name, Camera, ...) - RegisterBuiltinComponentReflections() runs
    // OUTSIDE any project's own BeginRecordingFor()/EndRecording() bracket,
    // so its own typeNames are never recorded in ANY project's ledger entry
    // to begin with. Safe to call for a T whose pool doesn't currently
    // exist (id >= m_pools.size(), or m_pools[id] already null) - a no-op,
    // matching this whole file's own established "no-op on the
    // already-torn-down/never-built case" convention (e.g. DestroyEntity()
    // on an already-dead entity).
    template <typename T>
    void ResetStoragePool()
    {
        const std::size_t id = detail::ComponentTypeId<T>();
        if (id < m_pools.size()) {
            m_pools[id].reset();
        }
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
