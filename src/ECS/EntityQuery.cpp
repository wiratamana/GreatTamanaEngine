#include "EntityQuery.h"

#include "Components/Name.h"
#include "Registry.h"

#include <cstdint>
#include <string_view>
#include <unordered_set>
#include <utility>

namespace gte {

Entity FindEntityByName(Registry& registry, const std::string& name)
{
    if (name.empty()) {
        return kInvalidEntity;
    }
    ComponentStorage<Name>& storage = registry.Storage<Name>();
    for (std::size_t i = 0; i < storage.Size(); ++i) {
        if (storage.ComponentAt(i).value == name) {
            return storage.EntityAt(i);
        }
    }
    return kInvalidEntity;
}

bool IsEntityNameInUse(Registry& registry, const std::string& name)
{
    return FindEntityByName(registry, name) != kInvalidEntity;
}

std::string MakeUniqueEntityName(Registry& registry, const std::string& baseName)
{
    return MakeUniqueEntityName(registry, baseName, kInvalidEntity);
}

std::string MakeUniqueEntityName(Registry& registry, const std::string& baseName, Entity ignoredEntity)
{
    // Keep only names that can collide (they start with baseName), then probe the set.
    std::unordered_set<std::string_view> taken;
    ComponentStorage<Name>& storage = registry.Storage<Name>();
    for (std::size_t i = 0; i < storage.Size(); ++i) {
        if (storage.EntityAt(i) == ignoredEntity) {
            continue;
        }
        const std::string& value = storage.ComponentAt(i).value;
        if (value.compare(0, baseName.size(), baseName) == 0) {
            taken.insert(value);
        }
    }
    if (!taken.contains(baseName)) {
        return baseName;
    }
    for (std::uint32_t suffix = 1;; ++suffix) {
        std::string candidate = baseName + " (" + std::to_string(suffix) + ")";
        if (!taken.contains(candidate)) {
            return candidate;
        }
    }
}

AssignedEntityName AssignUniqueEntityName(Registry& registry, Entity entity, const std::string& requestedBase)
{
    if (!registry.IsAlive(entity)) {
        return {};
    }
    Name* existing = registry.TryGetComponent<Name>(entity);
    std::string base = requestedBase;
    if (base.empty()) {
        base = (existing != nullptr && !existing->value.empty()) ? existing->value : std::string("Entity");
    }
    std::string unique = MakeUniqueEntityName(registry, base, entity);
    const bool adjusted = unique != base;
    if (existing != nullptr) {
        existing->value = unique;
    } else {
        registry.AddComponent<Name>(entity, Name{unique});
    }
    return AssignedEntityName{std::move(unique), adjusted};
}

} // namespace gte
