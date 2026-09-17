#pragma once

// Template body for RegisterComponentType<T>() (declared in
// ComponentTypeRegistry.h, included at the bottom of that header). Split out
// into its own file purely for readability - this is included transitively
// by anything that includes ComponentTypeRegistry.h, so it needs
// ECS/Registry.h for Registry::HasComponent<T>()/TryGetComponent<T>()/
// AddComponent<T>().

#include "../Registry.h"

#include <utility>

namespace gte {

template <typename T>
void RegisterComponentType(const std::string& typeName, std::vector<FieldDescriptor> fields)
{
    ComponentTypeDescriptor descriptor;
    descriptor.typeName = typeName;
    descriptor.hasComponent = [](Registry& registry, Entity entity) { return registry.HasComponent<T>(entity); };
    descriptor.tryGetConstComponent = [](Registry& registry, Entity entity) -> const void* {
        return registry.TryGetComponent<T>(entity);
    };
    descriptor.tryGetMutableComponent = [](Registry& registry, Entity entity) -> void* {
        return registry.TryGetComponent<T>(entity);
    };
    descriptor.ensureDefaultComponent = [](Registry& registry, Entity entity) {
        if (!registry.HasComponent<T>(entity)) {
            registry.AddComponent<T>(entity);
        }
    };
    descriptor.fields = std::move(fields);
    ComponentTypeRegistry::Instance().RegisterDescriptor(std::move(descriptor));
}

} // namespace gte
