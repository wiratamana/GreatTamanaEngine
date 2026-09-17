#include "ComponentTypeRegistry.h"

#include <algorithm>
#include <cassert>

namespace gte {

// Forward-declared here, DEFINED in BuiltinComponentReflection.cpp
// (task_manager/scene-serialization-2/PHASE2_BUILTIN_COMPONENT_REFLECTION_REGISTRATION.md)
// - deliberately NOT included via a header, to keep this Phase-1 module free
// of any #include on a real ECS/Components/*.h file. This one bootstrap call
// site is the only place that knows real, engine-specific component types
// exist at all.
void RegisterBuiltinComponentReflections();

ComponentTypeRegistry& ComponentTypeRegistry::Instance()
{
    static ComponentTypeRegistry instance;
    static bool bootstrapped = false;
    if (!bootstrapped) {
        // Set BEFORE calling RegisterBuiltinComponentReflections() -
        // RegisterComponentType<T>() (ComponentTypeRegistry.inl) calls
        // Instance() again internally for every single component type it
        // registers, and that nested call must see `bootstrapped == true`
        // already, or this would recurse forever.
        bootstrapped = true;
        RegisterBuiltinComponentReflections();
    }
    return instance;
}

void ComponentTypeRegistry::RegisterDescriptor(ComponentTypeDescriptor descriptor)
{
    assert(Find(descriptor.typeName) == nullptr && "ComponentTypeRegistry::RegisterDescriptor() called twice for the same typeName - always a programmer error");

    m_descriptors.push_back(std::move(descriptor));
    std::sort(m_descriptors.begin(), m_descriptors.end(), [](const ComponentTypeDescriptor& a, const ComponentTypeDescriptor& b) {
        return a.typeName < b.typeName;
    });
}

const ComponentTypeDescriptor* ComponentTypeRegistry::Find(const std::string& typeName) const
{
    for (const ComponentTypeDescriptor& descriptor : m_descriptors) {
        if (descriptor.typeName == typeName) {
            return &descriptor;
        }
    }
    return nullptr;
}

const std::vector<ComponentTypeDescriptor>& ComponentTypeRegistry::AllSortedByTypeName() const
{
    return m_descriptors;
}

} // namespace gte
