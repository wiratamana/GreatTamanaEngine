#include "EntityQuery.h"

#include "Components/Name.h"
#include "Registry.h"

#include <cstdint>

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
    if (!IsEntityNameInUse(registry, baseName)) {
        return baseName;
    }
    for (std::uint32_t suffix = 1;; ++suffix) {
        std::string candidate = baseName + " (" + std::to_string(suffix) + ")";
        if (!IsEntityNameInUse(registry, candidate)) {
            return candidate;
        }
    }
}

} // namespace gte
