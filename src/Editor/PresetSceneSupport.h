// Helpers shared by the preset scene openers: non-throwing logging, entity naming, and scope guards.
#pragma once

#include "../ECS/Entity.h"

#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace gte {

class Registry;

// Log to the engine log under `category`; never throw.
void PresetLogError(const char* category, std::string_view what, std::string_view detail = {}) noexcept;
void PresetLogInfo(const char* category, const std::string& message) noexcept;

// "index/generation", the form the HTTP replies use.
std::string EntityText(Entity entity);

// Adds the Name component, or overwrites it.
void SetEntityName(Registry& registry, Entity entity, std::string_view name);

// Destroys every added entity (with its descendants) on destruction unless released.
class CreatedEntityGuard {
public:
    CreatedEntityGuard(Registry& registry, const char* category) noexcept;
    ~CreatedEntityGuard();
    CreatedEntityGuard(const CreatedEntityGuard&) = delete;
    CreatedEntityGuard& operator=(const CreatedEntityGuard&) = delete;

    void Add(Entity entity);
    void Add(const std::vector<Entity>& entities);

    // Disarms the guard and hands the entities over.
    std::vector<Entity> Release() noexcept;

private:
    Registry& m_registry;
    const char* m_category;
    std::vector<Entity> m_created;
    bool m_armed = true;
};

// Runs `undo` on destruction unless released.
template <typename Undo>
class ScopeUndo {
public:
    explicit ScopeUndo(Undo undo) noexcept : m_undo(std::move(undo)) { }
    ~ScopeUndo()
    {
        if (m_armed) {
            m_undo();
        }
    }
    ScopeUndo(const ScopeUndo&) = delete;
    ScopeUndo& operator=(const ScopeUndo&) = delete;

    void Release() noexcept { m_armed = false; }

private:
    Undo m_undo;
    bool m_armed = true;
};

} // namespace gte
