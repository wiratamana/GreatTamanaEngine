#include "ComponentTypeRegistry.h"

#include <algorithm>
#include <cassert>

namespace gte {

ComponentTypeRegistry& ComponentTypeRegistry::Instance()
{
    static ComponentTypeRegistry instance;
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
