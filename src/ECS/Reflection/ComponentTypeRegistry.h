#pragma once

// The one, global, process-wide table of every REGISTERED reflectable
// component type - see ComponentTypeDescriptor.h for the data shape being
// registered, ReflectFieldMacros.h for the macros that build individual
// FieldDescriptor values, and a later phase for real component
// registrations (Transform/Name/Camera/DirectionalLight/PrimitiveSource).

#include "ComponentTypeDescriptor.h"

#include <string>
#include <vector>

namespace gte {

// A Meyer's singleton (lazily constructed on first use, exactly like
// ECS/Registry.h's own detail::ComponentTypeId<T>() counter), deliberately
// NOT a global variable with static-initialization-order dependencies
// across translation units. Never mutated after the engine's first frame in
// practice, but nothing in this class itself enforces that.
class ComponentTypeRegistry {
public:
    static ComponentTypeRegistry& Instance();

    // Registers a new component type - typically called exactly once per
    // distinct T, ever, for the life of the process (see a later phase).
    // Asserts (debug builds only) if `typeName` was already registered - a
    // duplicate registration is always a programmer error, never a valid
    // runtime occurrence.
    void RegisterDescriptor(ComponentTypeDescriptor descriptor);

    // Looks up a previously-registered descriptor by its exact typeName -
    // nullptr if unknown (see FieldDescriptor's own doc comment: an
    // unrecognized component-type key in a loaded scene file is a silent,
    // forward-compatible no-op, never an error).
    const ComponentTypeDescriptor* Find(const std::string& typeName) const;

    // Every registered descriptor, sorted by typeName (ascending,
    // std::string's own operator<) - deterministic iteration order
    // regardless of registration order, so two runs of the SAME engine
    // build always serialize a given entity's "components" object with the
    // SAME KEY ORDER (nlohmann::json's own default object type is
    // order-preserving on insertion, not alphabetical - sorting here is
    // what makes a future Scene/SceneBuilder.cpp's output byte-for-byte
    // reproducible, useful for diffing saved scene files and for
    // deterministic Tier-1 tests).
    const std::vector<ComponentTypeDescriptor>& AllSortedByTypeName() const;

private:
    ComponentTypeRegistry() = default;
    std::vector<ComponentTypeDescriptor> m_descriptors; // kept sorted by typeName after every RegisterDescriptor() call.
};

// Generic helper template - builds a fully-populated ComponentTypeDescriptor
// for component type T and registers it in one call. `fields` is normally
// built from a list of GTE_REFLECT_FIELD(...)/GTE_REFLECT_ENUM_FIELD(...)
// macro invocations (see ReflectFieldMacros.h) - see a later phase for real,
// worked examples against Transform/Camera/etc.
template <typename T>
void RegisterComponentType(const std::string& typeName, std::vector<FieldDescriptor> fields);

} // namespace gte

#include "ComponentTypeRegistry.inl" // template implementation, split out to keep this header readable.
