#include "ComponentTypeRegistry.h"
// editor-core-separation-13 campaign (Project Assembly Hot Reload plan,
// BIG-STEP 2), PHASE3 - RecordComponentType()'s own no-op-outside-a-bracket
// call, added to RegisterDescriptor() below. Confirmed ../../ (2 levels):
// src/ECS/Reflection/ -> src/ECS/ -> src/, then into Core/Plugins/ - mirrors
// this same directory's sibling file BuiltinComponentReflection.cpp's own
// identical-depth include of "../../Renderer/Primitives/PrimitiveMeshGenerator.h".
#include "../../Core/Plugins/ProjectAssemblyRegistrationLedger.h"

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

    const std::string typeName = descriptor.typeName; // copy BEFORE std::move below - editor-core-separation-13, PHASE3.
    m_descriptors.push_back(std::move(descriptor));
    std::sort(m_descriptors.begin(), m_descriptors.end(), [](const ComponentTypeDescriptor& a, const ComponentTypeDescriptor& b) {
        return a.typeName < b.typeName;
    });
    ProjectAssemblyRegistrationLedger::Instance().RecordComponentType(typeName); // editor-core-separation-13, PHASE3.
}

// editor-core-separation-13 campaign (Project Assembly Hot Reload plan,
// BIG-STEP 2, Hazard 1 fix) - see this method's own doc comment in
// ComponentTypeRegistry.h for the full reasoning.
void ComponentTypeRegistry::UnregisterDescriptor(const std::string& typeName)
{
    m_descriptors.erase(
        std::remove_if(m_descriptors.begin(), m_descriptors.end(),
            [&typeName](const ComponentTypeDescriptor& d) { return d.typeName == typeName; }),
        m_descriptors.end());
    // No re-sort needed - std::vector::erase() preserves the relative order
    // of every remaining element, and the vector was already sorted before
    // this call (RegisterDescriptor()'s own invariant) - removing entries
    // can never un-sort what remains.
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
