# PHASE2 — ECS Entity-By-Name Lookup + Unique-Naming Utilities

Parent: `PHASE0_MASTER_STRATEGY.md`. Depends on: nothing from Phase 1 (this
phase is completely independent of JSON/networking — pure ECS + pure
`PrimitiveType` string mapping).

## Step 1 — The Goal

Give the engine three small, pure, Tier-1-tested building blocks that Phase 3
needs and that will be reusable by any future "look this entity up by name"
or "spawn something with a Unity-style auto-deduplicated name" feature:

1. Find a live entity by its `Name` component value.
2. Check whether a given name is currently in use.
3. Produce a Unity-`"GameObject (1)"`-style unique name from a requested base
   name.
4. Parse a case-insensitive shape name string (`"cube"`, `"Sphere"`, ...) into
   a `PrimitiveType` enumerator.

## Step 2 — The Situation

- `src/ECS/Components/Name.h`'s `Name` struct is a single optional
  `std::string value` — there is currently **no** function anywhere in the
  engine that looks up an entity BY its `Name` value; every existing consumer
  (Hierarchy panel, Inspector) only ever reads a Name it already has an
  `Entity` handle for.
- `Registry::Storage<T>()` (`src/ECS/Registry.h`) exposes the underlying
  `ComponentStorage<Name>` directly (`Size()`/`EntityAt(i)`/`ComponentAt(i)`),
  which is exactly the dense, cache-friendly, "iterate every entity that
  actually HAS a Name" iteration this lookup needs — never scan "every entity
  that exists," only entities that actually carry a `Name`.
- `src/Editor/ProjectPanelData.h`'s `MakeUniqueDestinationPath()` is the
  EXISTING precedent for "auto-rename to avoid a collision" in this codebase
  (see `README.md`, "Project panel": *"auto-renaming — `"name (1).ext"`,
  `"name (2).ext"`, ... — rather than clobbering an existing same-named
  item"*) — read that function's actual implementation
  (`src/Editor/ProjectPanelData.cpp`) before writing the new one, to match its
  loop-and-probe shape/style exactly (just swapping "does this path exist on
  disk" for "does this name already belong to a live entity").
- `src/Renderer/Primitives/PrimitiveMeshGenerator.h` already has
  `const char* ToString(PrimitiveType type) noexcept` (produces `"Cube"`,
  `"Sphere"`, ...) — there is no inverse (`string -> PrimitiveType`) yet.

## Step 3 — The Plan

### 3.1 — New file `src/ECS/EntityQuery.h`

```cpp
#pragma once

#include "Entity.h"

#include <string>

namespace gte {

class Registry;

// First entity (in ComponentStorage<Name>'s current dense iteration order -
// NOT guaranteed stable across a Remove() on ANY entity's Name component,
// per ComponentStorage<T>'s own swap-with-last removal - see ComponentStorage.h)
// whose Name::value is EXACTLY equal to `name` (case-sensitive, byte-for-byte
// comparison - no normalization of any kind). Returns kInvalidEntity if no
// live entity currently has that exact name, or if `name` is empty (an empty
// name is never a valid lookup key - MakeUniqueEntityName() below never
// produces one, and this function treats an empty `name` as "not found" by
// definition rather than silently matching every entity with an empty Name,
// which would otherwise be a real footgun).
Entity FindEntityByName(Registry& registry, const std::string& name);

// True if ANY live entity currently has a Name component whose value equals
// `name` exactly - a thin wrapper over FindEntityByName() returning != kInvalidEntity,
// kept as its own named function purely for call-site readability
// (`if (IsEntityNameInUse(...))` reads better than a raw comparison at every
// call site - mirrors this codebase's general preference for named boolean
// helpers over inline comparisons, e.g. Entity::IsValid()).
bool IsEntityNameInUse(Registry& registry, const std::string& name);

// Unity's own "GameObject", "GameObject (1)", "GameObject (2)", ... auto-
// de-duplication convention, mirroring the already-established precedent in
// src/Editor/ProjectPanelData.h's MakeUniqueDestinationPath() (see that
// function's own implementation for the loop/probe shape to copy). If
// `baseName` is not currently in use (IsEntityNameInUse() == false), returns
// it verbatim, unmodified - the common case, and what keeps a single spawn
// with no naming collision looking exactly like the name the caller asked
// for. Otherwise probes "<baseName> (1)", "<baseName> (2)", "<baseName> (3)",
// ... in order, returning the first one NOT currently in use. `baseName` must
// be non-empty (callers - Phase 3's Game::InstantiatePrimitive() - are
// responsible for substituting a sensible default, e.g. the shape's own
// ToString(), before calling this when the caller supplied no name at all;
// this function itself does not know about PrimitiveType and must not gain a
// dependency on it).
std::string MakeUniqueEntityName(Registry& registry, const std::string& baseName);

} // namespace gte
```

**CRITICAL — CMakeLists.txt registration (second-iteration finding): the root
`CMakeLists.txt` does NOT glob for source files.** `gte_core` is built from an
explicit, hand-maintained `target_sources(gte_core PRIVATE ...)` file list
(the top-level `CMakeLists.txt`, `add_library(gte_core STATIC ...)` block) -
a new `.cpp` file that is never added to this list simply never gets compiled
into `gte_core`, which means every symbol it defines (e.g. `FindEntityByName`)
is missing at LINK time the moment anything in Phase 3+ tries to call it -
this is a real, guaranteed build failure if skipped, not a style nitpick. Add
**both** new lines to that list, immediately after the existing
`src/ECS/TransformHierarchy.cpp` line (right after the existing ECS block,
before `src/ECS/Components/Transform.h`):
```
    src/ECS/EntityQuery.h
    src/ECS/EntityQuery.cpp
```

### 3.2 — Implement in `src/ECS/EntityQuery.cpp`

```cpp
#include "EntityQuery.h"

#include "Components/Name.h"
#include "Registry.h"

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
```

(This is a suggested-correct implementation, not a copy-paste mandate — verify
`Registry::Storage<T>()` is accessible the way assumed above, i.e. confirm it
is a public, non-const method exactly as documented in `Registry.h` before
finalizing; note `Storage<T>()` is non-const-only in the current `Registry.h`
— `FindEntityByName`/etc. above therefore correctly take a non-const
`Registry&`, matching every other free function in `ECS/TransformHierarchy.h`,
which already takes `Registry&` non-const for exactly this reason.)

**Defensive note on the `MakeUniqueEntityName` loop**: unlike
`MakeUniqueDestinationPath()`'s filesystem probing (bounded in practice by how
many files a human could plausibly create), this loop has no engine-enforced
upper bound. This is accepted as-is (mirrors the filesystem precedent's own
unbounded loop) — do not add an artificial cap unless a future regression
actually demonstrates a problem; note this explicitly in the function's own
doc comment so a future reader doesn't "fix" it speculatively.

### 3.3 — Extend `src/Renderer/Primitives/PrimitiveMeshGenerator.h`

Add, right below the existing `ToString(PrimitiveType)` declaration:

```cpp
// The inverse of ToString() above - parses a shape name (case-INSENSITIVE:
// "Cube", "cube", "CUBE" all match) into its PrimitiveType. Recognizes
// exactly the 5 names ToString() itself can ever produce, lower-cased for the
// comparison ("cube", "sphere", "capsule", "cone", "plane") - never a fuzzy/
// partial match. Returns false (and leaves `outType` completely untouched -
// never partially/incorrectly written) for anything else, including an empty
// string. Used by the network instantiate_primitive endpoint
// (network-impl-3 campaign, see Game::InstantiatePrimitive()) to turn a
// JSON "shape" string field into a real PrimitiveType.
bool TryParsePrimitiveTypeName(const std::string& name, PrimitiveType& outType) noexcept;
```

**CONFIRMED (second-iteration review): add `#include <string>` to this
header unconditionally** - `PrimitiveMeshGenerator.h` today only includes
`"Renderer/Vertex.h"` (which itself only pulls in `<volk.h>`/`<array>`/
`<cstddef>` - no `<string>` anywhere in that chain) plus `<vector>` directly,
so `std::string` is NOT currently available in this header transitively.

Implement in `src/Renderer/Primitives/PrimitiveMeshGenerator.cpp` — lower-case
`name` into a local buffer (simple ASCII-only lower-casing is sufficient; this
is not a Unicode-text-processing concern, only a small fixed set of ASCII
shape-name literals), then compare against the 5 known lower-case literals.

### 3.4 — Tests

- New `tests/ECS/EntityQueryTests.cpp` (add to `tests/CMakeLists.txt`'s
  `GTE_TEST_SOURCES` list, unconditionally built — this module has no
  GPU/Renderer/httplib dependency at all, same "always built" bucket as
  `tests/ECS/TransformHierarchyTests.cpp`). Cover: `FindEntityByName()` finds
  the right entity among several; returns `kInvalidEntity` for an unused name;
  returns `kInvalidEntity` for an empty-string query even if some entity
  happens to have an empty `Name::value` (construct that exact case
  explicitly as a regression test); `IsEntityNameInUse()` mirrors
  `FindEntityByName()`'s own true/false answer; `MakeUniqueEntityName()`
  returns the base name verbatim when unused; returns `"X (1)"` when `"X"` is
  taken; returns `"X (2)"` when both `"X"` and `"X (1)"` are taken (probing
  order regression); a name that's already itself in the `"X (N)"` shape
  (e.g. base name literally `"Thing (1)"`, already in use) still probes
  correctly from `"Thing (1) (1)"` onward rather than doing anything clever
  with the existing suffix (document this as the DELIBERATE, simple behavior
  — no attempt to detect/increment an existing `(N)` suffix smartly, exactly
  mirroring `MakeUniqueDestinationPath()`'s own simplicity).
- Extend `tests/Renderer/PrimitiveMeshGeneratorTests.cpp` (existing file) with
  `TryParsePrimitiveTypeName()` cases: all 5 valid names in at least two
  different casings each (e.g. `"cube"`/`"CUBE"`/`"Cube"`); an unrecognized
  string; an empty string; confirm `outType` is left untouched (e.g.
  pre-seeded to a sentinel value the test can check survived) on a `false`
  return.

## Verification for this phase

- Fast compile check (`cmake --build build`) — this phase touches no
  GPU/Vulkan code, so this should be a quick, low-risk build.
- Run the new/changed tests specifically (`EntityQueryTests`,
  `PrimitiveMeshGeneratorTests`) and confirm all pass, plus a full `ctest`
  pass to confirm zero regressions elsewhere (this phase's new file is added
  to the always-built test set, so a full `ctest` run is cheap and worth
  doing here even though "Fast Compile Check only" is the general workflow
  rule — a full regression pass costs little at this exact point and buys
  confidence before Phase 3 builds directly on top of it).
- Write `PHASE2_COMPLETION_REPORT.md`, `git add`/`git commit`.
