# PHASE1 — Reflection Core & Math JSON Adapters

_Part of `task_manager/scene-serialization-2/`. **Parent: `PHASE0_MASTER_STRATEGY.md` — read it first.**_
Branch: `feature/scene-serialization`.

## Step 1: The Goal

Build the brand-new, generic, opt-in field-reflection primitives this whole
campaign is built on — with ZERO behavior change to the engine yet. Nothing in
`src/Scene/`, `src/Editor/`, or `src/Network/` is touched in this phase. At the
end of this phase there exists a small, fully unit-tested library that lets
ANY plain-data C++ struct register a list of its own fields once, and have
that list read back generically as JSON — proven against a throwaway,
test-only struct, not yet against any real ECS component (that's Phase 2).

## Step 2: The Situation / The Problem

This engine's ECS (`src/ECS/Registry.h`, `ComponentStorage.h`,
`IComponentPool`) has no reflection of any kind — `IComponentPool` only
exposes `Remove(Entity)`/`Has(Entity)`, nothing that describes a component
type's own fields. There is also no JSON adapter anywhere for this engine's
own math types (`Vec3`, `Quat`) — `nlohmann::json` only knows how to convert
built-in types (numbers, strings, bools) and STL containers automatically;
a custom type needs its own `to_json`/`from_json` free functions found via
ADL (argument-dependent lookup) in that type's own namespace.

`nlohmann::json` is already vendored (`cmake/FetchJson.cmake`,
`#include <nlohmann/json.hpp>`) and used today only inside
`src/Network/NetworkRoutes.cpp`. Per PHASE0's Locked Design Decision #1, this
phase is what actually widens its reach into `src/ECS/` for the first time.

## Step 3: The Plan

### 3.1 — New module: `src/ECS/Reflection/`

Create this new folder with four new files:

#### `src/ECS/Reflection/MathJsonAdapters.h`

Free `to_json`/`from_json` functions for `gte::Vec3` and `gte::Quat`,
declared INSIDE `namespace gte` (this is required — ADL only finds them if
they live in the same namespace as the type they convert). Reuse
`Math/Vec3.h`/`Math/Quat.h`'s own field names (`x`,`y`,`z`/`x`,`y`,`z`,`w`).
Represent each as a plain 3- or 4-element JSON ARRAY (`[x,y,z]`,
`[x,y,z,w]`) rather than a JSON object with named keys — shorter on disk,
and matches this file format's own existing convention (`SceneTextFormat.cpp`'s
old `position=%.6f,%.6f,%.6f` CSV form was already positional, not
named-field). Example shape:

```cpp
#pragma once
#include "../../Math/Vec3.h"
#include "../../Math/Quat.h"
#include <nlohmann/json.hpp>

namespace gte {

inline void to_json(nlohmann::json& j, const Vec3& v) { j = { v.x, v.y, v.z }; }
inline void from_json(const nlohmann::json& j, Vec3& v) {
    v.x = j.at(0).get<float>(); v.y = j.at(1).get<float>(); v.z = j.at(2).get<float>();
}
inline void to_json(nlohmann::json& j, const Quat& q) { j = { q.x, q.y, q.z, q.w }; }
inline void from_json(const nlohmann::json& j, Quat& q) {
    q.x = j.at(0).get<float>(); q.y = j.at(1).get<float>(); q.z = j.at(2).get<float>(); q.w = j.at(3).get<float>();
}

} // namespace gte
```

`j.at(i)` THROWS `nlohmann::json::out_of_range` if the array is too short,
and `.get<float>()` throws `nlohmann::json::type_error` if an element isn't
numeric — both exceptions propagate up to whatever calls `from_json`
indirectly (Phase 1's own `FieldDescriptor::readJson`, see 3.3 below), which
MUST catch `std::exception` at that boundary and turn it into a
non-throwing `bool` failure — never let a malformed scene file crash the
engine. Verify `Vec3`/`Quat`'s exact field names against
`src/Math/Vec3.h`/`src/Math/Quat.h` before writing this file (do not assume —
confirm `x`/`y`/`z`/`w` are public data members, not private-with-getters).

#### `src/ECS/Reflection/ComponentTypeDescriptor.h`

```cpp
#pragma once
#include "../Entity.h"
#include <nlohmann/json.hpp>
#include <functional>
#include <string>
#include <vector>

namespace gte {

class Registry;

// One field of one reflectable component type - see ComponentTypeRegistry.h
// for how these get grouped per component type and walked generically by
// Scene/SceneBuilder.cpp (Phase 3). Deliberately type-erased via
// std::function over `const void*`/`void*` rather than a template, so
// ComponentTypeRegistry can hold a single homogeneous list of these across
// EVERY different component type T - the same "type-erase once, at
// registration time, rather than plumb T through every later caller"
// tradeoff IComponentPool (ECS/ComponentStorage.h) already makes.
struct FieldDescriptor {
    std::string name;

    // Writes this ONE field's current value into `outFields[name]`.
    // `component` must actually point at a live T (the SAME T this
    // descriptor was built for via GTE_REFLECT_FIELD/GTE_REFLECT_ENUM_FIELD
    // below) - never validated at this layer, by construction (see
    // ComponentTypeDescriptor's own `tryGetConstComponent` below, which is
    // the only thing that ever produces the pointer this receives).
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
// built generically for any T, and Scene/SceneBuilder.cpp (Phase 3) for the
// one place that actually walks `fields` for every live entity that has
// this component.
struct ComponentTypeDescriptor {
    std::string typeName; // e.g. "Transform" - the exact JSON key used in a saved entity's "components" object (see PHASE0's Appendix A).

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
```

#### `src/ECS/Reflection/ComponentTypeRegistry.h` + `.cpp`

```cpp
// ComponentTypeRegistry.h
#pragma once
#include "ComponentTypeDescriptor.h"
#include <vector>

namespace gte {

// The one, global, process-wide table of every REGISTERED reflectable
// component type - a Meyer's singleton (lazily constructed on first use,
// exactly like ECS/Registry.h's own detail::ComponentTypeId<T>() counter),
// deliberately NOT a global variable with static-initialization-order
// dependencies across translation units. Never mutated after the engine's
// first frame in practice, but nothing in this class itself enforces that -
// see PHASE2's own doc comment for exactly when/how registration actually
// happens.
class ComponentTypeRegistry {
public:
    static ComponentTypeRegistry& Instance();

    // Registers a new component type - typically called exactly once per
    // distinct T, ever, for the life of the process (see PHASE2). Asserts
    // (debug builds only) if `typeName` was already registered - a
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
    // what makes Scene/SceneBuilder.cpp's output byte-for-byte
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
// macro invocations (see below) - see PHASE2 for real, worked examples
// against Transform/Camera/etc.
template <typename T>
void RegisterComponentType(const std::string& typeName, std::vector<FieldDescriptor> fields);

} // namespace gte

#include "ComponentTypeRegistry.inl" // template implementation - see 3.2 below for why this is split out.
```

Split the `RegisterComponentType<T>()` template body into a small
`ComponentTypeRegistry.inl` (included at the bottom of the header) so the
header stays readable — this is a genuinely new template that needs
`Registry::HasComponent<T>()`/`TryGetComponent<T>()`/`AddComponent<T>()`
(from `ECS/Registry.h`), so `ComponentTypeRegistry.h` must `#include
"../Registry.h"` too. Body:

```cpp
// ComponentTypeRegistry.inl
#pragma once
#include "../Registry.h"

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
```

`ComponentTypeRegistry.cpp` implements `Instance()` (Meyer's singleton),
`RegisterDescriptor()` (inserts, then re-sorts `m_descriptors` by
`typeName`, with a debug-only `assert` against a duplicate `typeName`),
`Find()` (linear scan is fine — this engine has a handful of component
types, never thousands), and `AllSortedByTypeName()` (returns
`m_descriptors` directly, already kept sorted).

### 3.2 — Reflection macros: `src/ECS/Reflection/ReflectFieldMacros.h`

```cpp
#pragma once
#include "ComponentTypeDescriptor.h"
#include <nlohmann/json.hpp>
#include <string>

// Builds ONE FieldDescriptor for a plain (non-enum) field `member` of
// component type `ComponentType`, relying on nlohmann::json's own built-in
// to_json/from_json for the field's type (works out of the box for
// float/int/bool/std::string, and for Vec3/Quat ONCE
// ECS/Reflection/MathJsonAdapters.h has been #included ahead of this macro's
// use site - callers MUST include that header first, see PHASE2's real
// usage for the exact include order). A missing JSON key is treated as
// "keep the current value" (see FieldDescriptor::readJson's own doc
// comment) - checked via `inFields.contains(#member)` BEFORE attempting
// `.get<...>()`, never relying on a caught exception for the ordinary
// "field omitted" case (exceptions are reserved for genuinely malformed
// input, not a routine, expected, forward-compatible omission).
#define GTE_REFLECT_FIELD(ComponentType, member) \
    gte::FieldDescriptor{ \
        #member, \
        [](const void* c, nlohmann::json& out) { \
            out[#member] = static_cast<const ComponentType*>(c)->member; \
        }, \
        [](void* c, const nlohmann::json& in, std::string& err) -> bool { \
            if (!in.contains(#member)) { return true; } \
            try { \
                static_cast<ComponentType*>(c)->member = in.at(#member).get<decltype(ComponentType::member)>(); \
            } catch (const std::exception& e) { \
                err = std::string("field '" #member "' on component '" #ComponentType "': ") + e.what(); \
                return false; \
            } \
            return true; \
        } \
    }

// Same contract as GTE_REFLECT_FIELD above, for an ENUM field that needs a
// human-readable string representation on disk rather than a raw integer
// (matches this engine's existing "ToString()/TryParseXxxName()" convention
// - e.g. Renderer/Primitives/PrimitiveMeshGenerator.h's
// ToString(PrimitiveType)/TryParsePrimitiveTypeName()). `ToStringFn` must be
// callable as `const char*(EnumType)`; `TryParseFn` must be callable as
// `bool(const std::string&, EnumType&)`.
#define GTE_REFLECT_ENUM_FIELD(ComponentType, member, ToStringFn, TryParseFn) \
    gte::FieldDescriptor{ \
        #member, \
        [](const void* c, nlohmann::json& out) { \
            out[#member] = ToStringFn(static_cast<const ComponentType*>(c)->member); \
        }, \
        [](void* c, const nlohmann::json& in, std::string& err) -> bool { \
            if (!in.contains(#member)) { return true; } \
            if (!in.at(#member).is_string()) { \
                err = "field '" #member "' on component '" #ComponentType "' must be a string"; \
                return false; \
            } \
            decltype(ComponentType::member) parsed{}; \
            if (!TryParseFn(in.at(#member).get<std::string>(), parsed)) { \
                err = "field '" #member "' on component '" #ComponentType "' has an unrecognized value"; \
                return false; \
            } \
            static_cast<ComponentType*>(c)->member = parsed; \
            return true; \
        } \
    }
```

### 3.3 — CMakeLists.txt wiring

Open `CMakeLists.txt`, find the existing `src/ECS/` source-file list (search
for `ECS/Registry.cpp` or `ECS/TransformHierarchy.cpp` to find the right
spot), and add the four new files as siblings:
`ECS/Reflection/ComponentTypeRegistry.cpp` as a compiled source, and
`ECS/Reflection/{MathJsonAdapters.h,ComponentTypeDescriptor.h,
ComponentTypeRegistry.h,ComponentTypeRegistry.inl,ReflectFieldMacros.h}` as
headers (this codebase's `CMakeLists.txt` lists headers explicitly too in
several places for IDE friendliness — match whatever the existing `ECS/`
block already does for `Registry.h`/`ComponentStorage.h`). This module needs
no new `target_link_libraries`/`find_package` — `nlohmann::json` (or
whatever exact target `cmake/FetchJson.cmake` defines — confirm the exact
target name from that file, it is already linked into `gte_core` today for
`NetworkRoutes.cpp` to compile) is already available to every `gte_core`
source file.

### 3.4 — Tier-1 tests: `tests/ECS/Reflection/ComponentTypeRegistryTests.cpp`

Per `AGENTS.md`'s Testability rule, this brand-new module needs its own test
file in the SAME phase. Define a small, PRIVATE, test-file-local struct (NOT
a real ECS component — this phase must not depend on Phase 2's real
component registrations, to keep the two phases' tests independent):

```cpp
struct DummyReflectedComponent { float value = 1.0f; std::string label; gte::Vec3 offset; };
```

Cover, at minimum:
- Registering `DummyReflectedComponent` with 3 fields via
  `GTE_REFLECT_FIELD`, then round-tripping: add the component to a live
  entity in a real `Registry`, mutate its fields, call `writeJson` for
  every field into one `nlohmann::json` object, then `readJson` that same
  object back into a FRESH default-constructed instance — assert every
  field matches.
- A JSON object MISSING one of the three keys — assert that field keeps its
  pre-existing/default value, and `readJson` still returns `true`.
- A JSON object with an EXTRA, unrecognized key — assert this is silently
  ignored (never touched by any `readJson` call, and never causes a
  failure at the `ComponentTypeDescriptor`/registry level — the "ignore
  unrecognized" behavior lives one layer up, at the Scene-document level in
  Phase 3, but per-field, a `FieldDescriptor::readJson` for one specific
  field never even looks at any key besides its own, so this is already
  implicitly covered — write the test anyway to nail it down explicitly).
- A field value of the WRONG JSON type (e.g. a string where
  `DummyReflectedComponent::value` expects a number) — assert `readJson`
  returns `false` with a non-empty `errorMessage`, and never throws.
- `ComponentTypeRegistry::Find("DummyReflectedComponent")` before vs. after
  registration, and `AllSortedByTypeName()`'s ordering with 2+ registered
  dummy types with deliberately out-of-alphabetical-order registration
  calls.
- A `Vec3` field (`offset` above) round-tripping through
  `MathJsonAdapters.h`'s `to_json`/`from_json` as a 3-element array,
  INCLUDING a malformed 2-element array producing a clean `false`/
  `errorMessage` (never a crash) — this exercises the try/catch boundary
  `GTE_REFLECT_FIELD`'s generated lambda wraps around `from_json`.

Add this new test file to `tests/CMakeLists.txt`'s Tier-1 test list
(mirroring exactly how `Scene/SceneTextFormatTests.cpp` or
`ECS/TransformHierarchyTests.cpp` are already listed there — find the exact
surrounding block via `search_in_dir` for `TransformHierarchyTests.cpp`).

## Definition of Done

- [ ] `src/ECS/Reflection/{MathJsonAdapters.h, ComponentTypeDescriptor.h,
      ComponentTypeRegistry.h, ComponentTypeRegistry.inl,
      ComponentTypeRegistry.cpp, ReflectFieldMacros.h}` all exist and compile.
- [ ] `CMakeLists.txt` lists every new file.
- [ ] `tests/ECS/Reflection/ComponentTypeRegistryTests.cpp` exists, is added
      to `tests/CMakeLists.txt`, and passes.
- [ ] Zero changes to any file under `src/Scene/`, `src/Editor/`,
      `src/Network/`, or any real `src/ECS/Components/*.h` — this phase is
      infrastructure-only.
- [ ] A fast compile check (`cmake --build build`, or the narrower
      `GreatTamanaEngineTests` target) succeeds.
- [ ] Write `PHASE1_COMPLETION_REPORT.md` in this same folder summarizing
      what was done, then `git add`/`git commit` (message mentioning
      `scene-serialization-2` PHASE1).
