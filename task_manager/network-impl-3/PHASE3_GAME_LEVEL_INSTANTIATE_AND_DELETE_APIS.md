# PHASE3 — Game-Level `InstantiatePrimitive()` / `DeleteEntityByName()` APIs

Parent: `PHASE0_MASTER_STRATEGY.md`. Depends on: Phase 2's `EntityQuery.h`
(`FindEntityByName`/`MakeUniqueEntityName`) and
`TryParsePrimitiveTypeName()`. Does **not** depend on Phase 1 (no JSON
anywhere in this phase) or on anything networking-related.

## Step 1 — The Goal

Add two new public methods to `Game` (`src/Game/Game.h/.cpp`) that do the
ACTUAL ECS mutation this whole campaign is about — callable from completely
ordinary in-process C++ code (no HTTP, no bridge, no JSON), so they can be
exercised/reasoned about/tested in total isolation from the networking layer
built on top of them in Phase 4/5:

- `Game::InstantiatePrimitive(...)` — spawns a primitive, sets its world
  position, gives it a guaranteed-unique display name, and optionally attaches
  it under a by-name-looked-up parent.
- `Game::DeleteEntityByName(...)` — destroys a live entity (and its
  descendants) looked up by name.

## Step 2 — The Situation

- `Game::CreatePrimitiveEntity(Renderer&, PrimitiveType)` (`src/Game/Game.h/.cpp`)
  already exists and is unchanged by this campaign — it spawns a `Transform`
  (identity) + `MeshRenderer` entity, no `Name`, no parent, reusing a shared
  GPU mesh/pipeline per shape via `MeshInstantiationSystem::SpawnPrimitive()`.
  This phase's new method is a thin ADDITIVE wrapper around it — it does not
  change `CreatePrimitiveEntity()`'s own behavior/signature at all (the
  Editor's existing "Create 3D Object" menu, `Panels/HierarchyPanel.cpp`, must
  keep working completely unchanged).
- `Transform::position`/`rotation`/`scale` (`ECS/Components/Transform.h`) are
  always PARENT-RELATIVE — for an entity with `parent == kInvalidEntity`
  (every freshly-`CreatePrimitiveEntity()`'d entity, always, today), local
  position IS world position. This is the load-bearing fact behind Step 2.3
  below.
- `ECS/TransformHierarchy.h::SetParent(registry, child, newParent,
  worldPositionStays = true)` recomputes `child`'s LOCAL transform fields so
  its WORLD transform is unchanged by the reparent — this is Unity's own
  `Transform.SetParent(Transform, bool)` default behavior, already
  implemented and unit-tested (`tests/ECS/TransformHierarchyTests.cpp`) —
  reuse it verbatim, do not reimplement any of its math.
- `ECS/TransformHierarchy.h::DestroyEntityAndDescendants(Registry&, Entity)`
  already exists (added originally for scene-serialization's Load-time scene
  clear, and explicitly future-proofed in its own doc comment for
  *"a future per-entity Hierarchy 'Delete' command"*) — this is EXACTLY what
  `DeleteEntityByName()` needs, unmodified.

## Step 3 — The Plan

### 3.1 — New file `src/Game/EngineCommandResults.h`

A small, plain-data-only header (no Renderer/Registry/ECS dependency beyond
`Entity.h` — deliberately kept this lightweight so BOTH `Game.h` (this phase)
AND `EngineCommandBridge.h` (Phase 4, an `Application`-layer file) can include
it without either pulling in the other's heavier dependencies):

```cpp
#pragma once

#include "../ECS/Entity.h"

#include <cstdint>
#include <string>

namespace gte {

// Outcome of one Game::InstantiatePrimitive() call - see that method's own
// doc comment (Game.h) for the full contract. `success == false` means
// `errorMessage` explains why and NO entity was created at all (every other
// field is meaningless in that case). `success == true` always means an
// entity WAS created, even if `parentRequestedButNotFound` is also true (a
// not-found parent is a non-fatal WARNING, never a reason to fail the whole
// request or roll back the just-created entity - see PHASE0's own Locked
// Design Decision #2).
struct InstantiatePrimitiveOutcome {
    bool success = false;
    std::string errorMessage;

    std::uint32_t entityIndex = 0;
    std::uint32_t entityGeneration = 0;
    // The entity's ACTUAL display name after Unity-style auto-de-duplication
    // (EntityQuery.h's MakeUniqueEntityName()) - may differ from whatever
    // name the caller originally requested.
    std::string resolvedName;

    // True only when the caller explicitly requested a parent BY NAME and no
    // live entity currently has that name - the entity was still created,
    // just left unparented (world space). Always false when the caller
    // requested no parent at all.
    bool parentRequestedButNotFound = false;
    std::string requestedParentName; // meaningful only when the flag above is true
};

// Outcome of one Game::DeleteEntityByName() call. `success == false` means
// `errorMessage` explains why (empty name, or no live entity currently has
// that name) and NOTHING was destroyed. `success == true` means the named
// entity (and every descendant of it) was destroyed -
// deletedEntityIndex/Generation identify exactly which entity that was
// (useful for a caller that wants to confirm/log which physical entity a
// name resolved to, since names are not enforced globally unique outside of
// InstantiatePrimitive()'s own auto-dedup - see PHASE0's Locked Design
// Decision #3).
struct DeleteEntityOutcome {
    bool success = false;
    std::string errorMessage;
    std::uint32_t deletedEntityIndex = 0;
    std::uint32_t deletedEntityGeneration = 0;
};

} // namespace gte
```

**CRITICAL — CMakeLists.txt registration (second-iteration finding): the root
`CMakeLists.txt` builds `gte_core` from an explicit, hand-maintained
`target_sources(gte_core PRIVATE ...)` file list (`add_library(gte_core
STATIC ...)`) - it does NOT glob for source files.** `EngineCommandResults.h`
is header-only (no matching `.cpp`), so skipping this step would NOT break
the build the way a missing `.cpp` registration does (Phase 2/Phase 4 both
add real `.cpp` files - see their own equivalent notes), but every other
header in this codebase's `Game/` section is explicitly listed there too -
add this one line immediately after the existing `src/Game/Game.h` line, for
consistency with that established convention:
```
    src/Game/EngineCommandResults.h
```

**CONFIRMED (second-iteration review): `Vec3` is already available in
`Game.h` transitively** - `Game.cpp` already constructs `Vec3{ 0.0f, 0.0f,
-5.0f }` directly in `EnsureDefaultCameraExists()` today, with no dedicated
`Vec3.h` include of its own, so the include chain `Game.h` already pulls in
(via `ECS/Registry.h` -> ... -> `ECS/Components/Transform.h` ->
`Math/Vec3.h`) already exposes it - the `#include "Math/Vec3.h"` line shown
in the code block below is defensive/harmless but not strictly required.

### 3.2 — Extend `src/Game/Game.h`

Add near `CreatePrimitiveEntity()`'s own declaration (keep the two visually
adjacent — they are closely related):

```cpp
#include "EngineCommandResults.h"
#include "Math/Vec3.h" // if not already transitively available - verify

// ... inside class Game, public section ...

// Spawns a primitive shape by NAME (network-impl-3 campaign) - the same
// underlying spawn as CreatePrimitiveEntity() above (shares its GPU mesh/
// pipeline cache, PrimitiveGpuCatalog - a second "cube" spawned this way
// costs no new GPU upload), plus three additive steps: (1) `shapeName` is
// parsed case-insensitively via PrimitiveMeshGenerator::TryParsePrimitiveTypeName() -
// an unrecognized shape name fails the whole call (no entity created) with
// `errorMessage` explaining which names ARE valid; (2) `requestedName`
// (falling back to the shape's own ToString() when empty - e.g. an
// unqualified "Cube" for a plain cube spawn with no name given) is passed
// through EntityQuery.h's MakeUniqueEntityName() and the RESULT is what
// actually gets added as this entity's Name component - Unity's own
// "GameObject", "GameObject (1)", ... auto-de-duplication behavior (see
// AGENTS.md/PHASE0's own Locked Design Decision #3); (3) the entity's
// Transform::position is set to `worldPosition` (valid since a freshly
// spawned, still-unparented entity's local position IS its world position -
// see ECS/Components/Transform.h), and if `hasParent` is true and
// `parentName` resolves (via EntityQuery.h's FindEntityByName()) to a live
// entity, ECS/TransformHierarchy.h's SetParent(..., worldPositionStays = true)
// attaches it there, preserving the exact world position just set. If
// `hasParent` is true but `parentName` does NOT resolve to a live entity,
// the entity is still created, left UNPARENTED (world space) -
// InstantiatePrimitiveOutcome::parentRequestedButNotFound is set to true and
// `requestedParentName` echoes `parentName` back, but this is never treated
// as a failure of the overall call (see PHASE0's Locked Design Decision #2).
//
// Never throws. Returns an outcome with success == false (and creates NO
// entity at all) only for an unrecognized `shapeName`.
InstantiatePrimitiveOutcome InstantiatePrimitive(Renderer& renderer, const std::string& shapeName,
    const std::string& requestedName, const Vec3& worldPosition, bool hasParent, const std::string& parentName);

// Destroys the live entity (and every descendant of it - see
// ECS/TransformHierarchy.h::DestroyEntityAndDescendants()) whose Name
// component value exactly equals `name` (network-impl-3 campaign). Returns
// success == false (destroying NOTHING) if `name` is empty or no live entity
// currently has that exact name. If more than one live entity happens to
// share the same name (possible only through some OTHER naming path than
// InstantiatePrimitive() above, which always auto-dedupes - see PHASE0's
// Locked Design Decision #3), the FIRST match found by
// EntityQuery.h::FindEntityByName()'s own dense-iteration-order rule is the
// one destroyed - documented as best-effort, matching FindEntityByName()'s
// own doc comment.
DeleteEntityOutcome DeleteEntityByName(const std::string& name);
```

### 3.3 — Implement in `src/Game/Game.cpp`

```cpp
#include "../ECS/Components/Name.h"
#include "../ECS/EntityQuery.h"
#include "../ECS/TransformHierarchy.h"
#include "Renderer/Primitives/PrimitiveMeshGenerator.h" // for TryParsePrimitiveTypeName - verify actual relative include path used elsewhere in this file

InstantiatePrimitiveOutcome Game::InstantiatePrimitive(Renderer& renderer, const std::string& shapeName,
    const std::string& requestedName, const Vec3& worldPosition, bool hasParent, const std::string& parentName)
{
    InstantiatePrimitiveOutcome outcome;

    PrimitiveType type{};
    if (!TryParsePrimitiveTypeName(shapeName, type)) {
        outcome.success = false;
        outcome.errorMessage = "unrecognized shape '" + shapeName
            + "' - expected one of: cube, sphere, capsule, cone, plane";
        return outcome;
    }

    const std::string baseName = requestedName.empty() ? std::string(ToString(type)) : requestedName;
    const std::string uniqueName = MakeUniqueEntityName(m_registry, baseName);

    const Entity entity = CreatePrimitiveEntity(renderer, type);
    // Defensive - CreatePrimitiveEntity() is not currently documented to ever
    // return kInvalidEntity, but this is cheap insurance against a future
    // change there (e.g. a GPU resource creation failure surfaced as
    // kInvalidEntity instead of an exception) silently producing a "success"
    // outcome with a bogus entity handle.
    if (entity == kInvalidEntity) {
        outcome.success = false;
        outcome.errorMessage = "failed to create primitive entity";
        return outcome;
    }

    Transform& transform = m_registry.GetComponent<Transform>(entity);
    transform.position = worldPosition;

    m_registry.AddComponent<Name>(entity, Name{ uniqueName });

    outcome.success = true;
    outcome.entityIndex = entity.index;
    outcome.entityGeneration = entity.generation;
    outcome.resolvedName = uniqueName;

    if (hasParent) {
        const Entity parentEntity = FindEntityByName(m_registry, parentName);
        if (parentEntity == kInvalidEntity) {
            outcome.parentRequestedButNotFound = true;
            outcome.requestedParentName = parentName;
        } else {
            SetParent(m_registry, entity, parentEntity, /*worldPositionStays=*/true);
        }
    }

    return outcome;
}

DeleteEntityOutcome Game::DeleteEntityByName(const std::string& name)
{
    DeleteEntityOutcome outcome;
    const Entity entity = FindEntityByName(m_registry, name);
    if (entity == kInvalidEntity) {
        outcome.success = false;
        outcome.errorMessage = name.empty()
            ? "name must not be empty"
            : ("no live entity found with name '" + name + "'");
        return outcome;
    }
    outcome.deletedEntityIndex = entity.index;
    outcome.deletedEntityGeneration = entity.generation;
    DestroyEntityAndDescendants(m_registry, entity);
    outcome.success = true;
    return outcome;
}
```

(Suggested-correct reference implementation, not a copy-paste mandate — verify
exact header include paths/relative paths used elsewhere in `Game.cpp` before
finalizing, e.g. confirm whether `PrimitiveMeshGenerator.h` is already
included via `Game.h`'s own `#include "Renderer/Primitives/PrimitiveMeshGenerator.h"`
and needs no separate include in the `.cpp` at all.)

### 3.4 — Testability note (be explicit about this in the completion report)

`Game::InstantiatePrimitive()` touches a live `Renderer` (via
`CreatePrimitiveEntity()` → `MeshInstantiationSystem` → `PrimitiveGpuCatalog`,
real GPU resource creation) — this makes the METHOD AS A WHOLE fall into this
codebase's already-accepted "Tier 2, no automated coverage yet" bucket
(AGENTS.md, "Testability & Regression Safety"), same bucket
`CreatePrimitiveEntity()` itself has always been in. This is **expected and
acceptable** — do not attempt to fake/mock a `Renderer` to force this
specific method into Tier 1. What IS worth double-checking here: the parts of
this method's own LOGIC that don't strictly need a live Renderer (shape-name
parsing via `TryParsePrimitiveTypeName()`, name deduplication via
`MakeUniqueEntityName()`, parent lookup via `FindEntityByName()`) are already
independently Tier-1-tested in Phase 2 — this phase does not need to
duplicate that coverage, only to verify (by manual/Tier-2 means, e.g. running
the Editor and calling this new path once GPU-touching-and-visible in Phase
4/5's end-to-end smoke test) that the WIRING between those already-tested
pure pieces and the GPU-touching spawn call is correct. `Game::DeleteEntityByName()`,
by contrast, touches NO Renderer/GPU state at all (only `Registry`) — note in
the completion report whether it would be worth a dedicated
`tests/Game/GameEntityCommandsTests.cpp`-style Tier-1 test file exercising
just this one method directly against a real `Game` instance's `Registry`
(via `GetRegistry()`) — if `Game`'s own constructor/other members make this
awkward without a live `Renderer` anywhere nearby, it is fine to defer this
specific test to Phase 6 instead, where it can be added alongside every other
final testing gap in one pass.

## Verification for this phase

- Fast compile check.
- If a Tier-1 test for `DeleteEntityByName()` was added per 3.4 above, run it
  specifically and confirm it passes; otherwise a normal fast compile check is
  sufficient for this phase (defer verification of `InstantiatePrimitive()`'s
  actual runtime behavior to Phase 6's end-to-end smoke test, once the full
  network path exists to exercise it through — or, if convenient, do a quick
  manual `run_app_background` + a temporary direct C++ call from a throwaway
  test/debug hook to sanity-check it now; not required to consider this phase
  done).
- Write `PHASE3_COMPLETION_REPORT.md`, `git add`/`git commit`.
