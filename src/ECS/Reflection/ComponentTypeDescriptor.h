#pragma once

// Plain-data description of one reflectable ECS component TYPE, and one of
// its own FIELDs - see ComponentTypeRegistry.h for how these get built
// generically for any component type T (RegisterComponentType<T>()) and
// grouped into a single process-wide table, and ReflectFieldMacros.h for the
// GTE_REFLECT_FIELD/GTE_REFLECT_ENUM_FIELD macros that build individual
// FieldDescriptor values. Nothing under src/Scene/, src/Editor/, or
// src/Network/ is touched by this phase - see
// task_manager/scene-serialization-2/PHASE1_REFLECTION_CORE_AND_MATH_JSON_ADAPTERS.md.
//
// Zero engine behavior change: this header defines pure data + type-erased
// function objects only, and is not yet consumed by anything except its own
// Tier-1 tests (tests/ECS/Reflection/ComponentTypeRegistryTests.cpp) and,
// starting in a later phase, Scene/SceneBuilder.cpp.

#include "../Entity.h"

#include <nlohmann/json.hpp>

#include <functional>
#include <string>
#include <vector>

namespace gte {

class Registry;

// One field of one reflectable component type - see ComponentTypeRegistry.h
// for how these get grouped per component type and walked generically by
// Scene/SceneBuilder.cpp (a later phase). Deliberately type-erased via
// std::function over `const void*`/`void*` rather than a template, so
// ComponentTypeRegistry can hold a single homogeneous list of these across
// EVERY different component type T - the same "type-erase once, at
// registration time, rather than plumb T through every later caller"
// tradeoff IComponentPool (ECS/ComponentStorage.h) already makes.
struct FieldDescriptor {
    std::string name;

    // Writes this ONE field's current value into `outFields[name]`.
    // `component` must actually point at a live T (the SAME T this
    // descriptor was built for via GTE_REFLECT_FIELD/GTE_REFLECT_ENUM_FIELD,
    // see ReflectFieldMacros.h) - never validated at this layer, by
    // construction (see ComponentTypeDescriptor's own
    // `tryGetConstComponent` below, which is the only thing that ever
    // produces the pointer this receives).
    std::function<void(const void* component, nlohmann::json& outFields)> writeJson;

    // Reads this ONE field's value FROM `inFields[name]` INTO `*component`,
    // if `inFields` actually has that key - a MISSING key is not an error
    // (the field simply keeps whatever value `component` already had, e.g.
    // its type's own default-constructed value - forward/backward
    // compatibility with an older/newer saved file that didn't know about
    // this field yet). Returns false (and sets `errorMessage`) only for a
    // key that IS present but fails to convert (wrong JSON type, wrong
    // array length for a Vec3/Quat, an unrecognized enum string, ...) -
    // NEVER throws (wraps the underlying nlohmann::json access in
    // try/catch - see MathJsonAdapters.h's own doc comment above for why
    // this is required).
    std::function<bool(void* component, const nlohmann::json& inFields, std::string& errorMessage)> readJson;
};

// One reflectable component TYPE's full descriptor - see
// ComponentTypeRegistry.h's RegisterComponentType<T>() for how this gets
// built generically for any T, and Scene/SceneBuilder.cpp (a later phase)
// for the one place that actually walks `fields` for every live entity that
// has this component.
struct ComponentTypeDescriptor {
    std::string typeName; // e.g. "Transform" - the exact JSON key used in a saved entity's "components" object.

    // True if `entity` currently has this component - a thin, type-erased
    // wrapper over Registry::HasComponent<T>().
    std::function<bool(Registry& registry, Entity entity)> hasComponent;

    // Returns a non-owning pointer to `entity`'s own live component if
    // present, else nullptr - a type-erased Registry::TryGetComponent<T>().
    std::function<const void*(Registry& registry, Entity entity)> tryGetConstComponent;
    std::function<void*(Registry& registry, Entity entity)> tryGetMutableComponent;

    // Adds a DEFAULT-CONSTRUCTED T to `entity` if it doesn't already have
    // one (a type-erased Registry::AddComponent<T>(entity) - relies on
    // AddComponent<T>() OVERWRITING IN PLACE if already present, exactly
    // matching ComponentStorage<T>::Add()'s own documented behavior, so
    // this is safe to call unconditionally without checking hasComponent
    // first).
    std::function<void(Registry& registry, Entity entity)> ensureDefaultComponent;

    std::vector<FieldDescriptor> fields;
};

} // namespace gte
