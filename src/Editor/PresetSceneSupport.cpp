#include "PresetSceneSupport.h"

#include "../Core/Logging.h"
#include "../ECS/Components/Name.h"
#include "../ECS/Registry.h"
#include "../ECS/TransformHierarchy.h"

#include <exception>

namespace gte {

void PresetLogError(const char* category, std::string_view what, std::string_view detail) noexcept
{
    try {
        GTE_LOG_ERROR(category, std::string(what).append(detail));
    } catch (...) {
    }
}

void PresetLogInfo(const char* category, const std::string& message) noexcept
{
    try {
        GTE_LOG_INFO(category, message);
    } catch (...) {
    }
}

std::string EntityText(Entity entity)
{
    return std::to_string(entity.index) + "/" + std::to_string(entity.generation);
}

void SetEntityName(Registry& registry, Entity entity, std::string_view name)
{
    if (Name* existing = registry.TryGetComponent<Name>(entity)) {
        existing->value = std::string(name);
    } else {
        registry.AddComponent<Name>(entity, Name{ std::string(name) });
    }
}

CreatedEntityGuard::CreatedEntityGuard(Registry& registry, const char* category) noexcept
    : m_registry(registry)
    , m_category(category)
{
}

CreatedEntityGuard::~CreatedEntityGuard()
{
    if (!m_armed || m_created.empty()) {
        return;
    }
    try {
        DestroyEntitiesAndDescendants(m_registry, m_created);
    } catch (const std::exception& e) {
        PresetLogError(m_category, "Removing a half-built scene failed: ", e.what());
    }
}

void CreatedEntityGuard::Add(Entity entity)
{
    m_created.push_back(entity);
}

void CreatedEntityGuard::Add(const std::vector<Entity>& entities)
{
    m_created.insert(m_created.end(), entities.begin(), entities.end());
}

std::vector<Entity> CreatedEntityGuard::Release() noexcept
{
    m_armed = false;
    return std::move(m_created);
}

} // namespace gte
