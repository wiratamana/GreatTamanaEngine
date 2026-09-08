# PHASE2 — `PrimitiveSource` Component + `TransformHierarchy::DestroyEntityAndDescendants()`

Part of the `scene-serialization-1` campaign — see `PHASE0_MASTER_STRATEGY.md`.
Depends on PHASE1 being complete (compiles cleanly). PHASE3/PHASE4 depend on
this phase's two new pieces.

## Step 1: The Goal (Where are we going?)

Give every primitive-spawned entity a small, plain-data ECS component
recording WHICH `PrimitiveType` it is — the missing piece that makes a
primitive entity's shape recoverable later (needed by PHASE4's
`BuildSceneDocumentFromRegistry()`), mirroring exactly how
`MeshAssetSource` already lets an asset-spawned root remember which
`*.gta` file it came from. Also add the one missing ECS-hierarchy primitive
this campaign's Load path needs: a way to destroy an entity and every
descendant of it in one call.

## Step 2: The Situation / The Problem (Where are we now?)

- `src/ECS/Components/MeshAssetSource.h` already exists as the exact pattern
  to mirror: a tiny plain struct (`std::string gtaPath;`), added only to a
  root entity, via `EntityBlueprintNode::meshAssetSourcePath` →
  `EntityInstantiator::Instantiate()`'s
  `if (!blueprint.meshAssetSourcePath.empty()) { registry.AddComponent<MeshAssetSource>(...); }`
  check (`src/Game/Instantiation/EntityInstantiator.cpp`, lines 27–29).
- `src/Game/Instantiation/EntityBlueprint.h`'s `EntityBlueprintNode` struct
  has no field at all today that identifies "this node is a primitive, and
  specifically THIS `PrimitiveType`."
- `src/Game/Instantiation/PrimitiveGpuCatalog.cpp`'s
  `PrimitiveGpuCatalog::Resolve(RenderSystem&, Renderer&, PrimitiveType type)`
  (lines 33–39) builds an `EntityBlueprint` with only `pipeline`/`mesh` set
  — `type` itself is never threaded any further than this one function.
- `src/ECS/TransformHierarchy.h` (confirmed by full read) declares
  `ComputeWorldMatrix()`, `ComputeWorldTransform()`, `IsDescendantOf()`,
  `GetChildren()`, `SetParent()`, `SetSiblingIndex()`, `MoveToLastSibling()`
  — **no function that destroys an entity plus its whole subtree exists**.
  `Registry::DestroyEntity()` (`src/ECS/Registry.h`) only ever destroys the
  ONE entity passed to it.

## Step 3: The Plan (A very detailed strategy)

### 3.1 — New component: `src/ECS/Components/PrimitiveSource.h`

Create this new file, modeled directly on `MeshAssetSource.h`'s own shape
and doc-comment style:

```cpp
#pragma once

#include "../../Renderer/Primitives/PrimitiveMeshGenerator.h"

namespace gte {

// Plain marker/metadata component - the PrimitiveType (see
// Renderer/Primitives/PrimitiveMeshGenerator.h) an entity was spawned with
// via Game::CreatePrimitiveEntity() (src/Game/Game.cpp) - the primitive-spawn
// counterpart of MeshAssetSource.h's gtaPath (which instead records which
// *.gta file an ASSET-spawned root came from). A MeshRenderer's
// MeshHandle/PipelineHandle alone cannot be reversed back into "this was a
// Cube" (they are just opaque resource-pool indices - see
// Renderer/ResourcePool.h) - this component is what makes that possible,
// needed by task_manager/scene-serialization-1's
// Scene/SceneBuilder.h::BuildSceneDocumentFromRegistry() to serialize a
// primitive entity back out to a *.gtscene file. Attached ONLY to the one
// entity CreatePrimitiveEntity() itself creates (a primitive spawn is
// always a single node with no children - see EntityBlueprint.h) - never to
// an asset-spawned entity, which carries MeshAssetSource instead, never
// both.
struct PrimitiveSource {
    PrimitiveType type = PrimitiveType::Cube;
};

} // namespace gte
```

### 3.2 — Thread `PrimitiveType` through `EntityBlueprintNode`

In `src/Game/Instantiation/EntityBlueprint.h`, add a new field to
`EntityBlueprintNode`, right after the existing `meshAssetSourcePath` field
(currently lines 49–54) and before `std::vector<EntityBlueprintNode> children;`
(line 56):

```cpp
    // Non-nullopt only on a node that should carry a PrimitiveSource
    // component (see ECS/Components/PrimitiveSource.h) - the single node a
    // primitive spawn request resolves to (PrimitiveGpuCatalog::Resolve()),
    // recording exactly which PrimitiveType it was generated from. Mutually
    // exclusive with meshAssetSourcePath above in practice (a node is either
    // a primitive OR an asset-spawned entity, never both), though nothing
    // here enforces that - EntityInstantiator::Instantiate() simply adds
    // whichever component(s) have a non-default value.
    std::optional<PrimitiveType> primitiveSourceType;
```

This needs two new `#include`s at the top of `EntityBlueprint.h`:
`#include "../../Renderer/Primitives/PrimitiveMeshGenerator.h"` (for
`PrimitiveType`) and `#include <optional>` (alongside the existing
`#include <string>`/`#include <vector>`).

### 3.3 — Set it in `PrimitiveGpuCatalog::Resolve()`

In `src/Game/Instantiation/PrimitiveGpuCatalog.cpp`,
`PrimitiveGpuCatalog::Resolve()` (lines 33–39), add one line:

```cpp
EntityBlueprint PrimitiveGpuCatalog::Resolve(RenderSystem& renderSystem, Renderer& renderer, PrimitiveType type)
{
    EntityBlueprint blueprint;
    blueprint.pipeline = EnsureDefaultPipeline(renderSystem, renderer);
    blueprint.mesh = EnsurePrimitiveMesh(renderSystem, renderer, type);
    blueprint.primitiveSourceType = type; // <-- new
    return blueprint;
}
```

### 3.4 — Add the component in `EntityInstantiator::Instantiate()`

In `src/Game/Instantiation/EntityInstantiator.cpp`, `Instantiate()`
(currently lines 10–45), add a new block right after the existing
`meshAssetSourcePath` check (lines 27–29):

```cpp
    if (blueprint.primitiveSourceType.has_value()) {
        registry.AddComponent<PrimitiveSource>(entity, PrimitiveSource{ *blueprint.primitiveSourceType });
    }
```

Add `#include "../../ECS/Components/PrimitiveSource.h"` to this file's
include list, alongside the existing `MeshAssetSource.h`/`MeshRenderer.h`/
`Name.h`/`Transform.h` includes (lines 002–005).

### 3.5 — New free function: `TransformHierarchy::DestroyEntityAndDescendants()`

In `src/ECS/TransformHierarchy.h`, add a new declaration at the end of the
file (after `MoveToLastSibling()`, before the closing `} // namespace gte`):

```cpp
// Destroys `entity` AND every descendant of it (recursively, via
// GetChildren() above), post-order (children destroyed before their own
// parent) so a destroyed entity's own children are never left querying a
// GetChildren() call against an already-dead parent mid-walk. Safe to call
// on an already-dead/invalid entity (a no-op, matching
// Registry::DestroyEntity()'s own safety guarantee). Each individual
// destroy goes through Registry::DestroyEntity() (removing the entity from
// every component pool it ever touched - see Registry.h), so no component
// type this entity or its descendants carry needs to be known here.
//
// Needed by task_manager/scene-serialization-1's
// Scene/SceneBuilder.h::ClearSerializableSceneObjects() (replacing a scene's
// content before a Load) - also independently useful for a future
// per-entity Hierarchy "Delete" command (see TODO.md, "Per-entity Hierarchy
// context menu").
void DestroyEntityAndDescendants(Registry& registry, Entity entity);
```

In `src/ECS/TransformHierarchy.cpp`, add the implementation (check the
existing file for its exact `#include`s/namespace wrapping and match style
— it already includes `Registry.h`/`Components/Transform.h`):

```cpp
void DestroyEntityAndDescendants(Registry& registry, Entity entity)
{
    if (!registry.IsAlive(entity)) {
        return;
    }

    // Snapshot BEFORE recursing/destroying - GetChildren() reads live
    // Transform::parent data that Registry::DestroyEntity() below is about
    // to invalidate, so every child must be captured into this local
    // vector up front, not re-queried mid-loop.
    const std::vector<Entity> children = GetChildren(registry, entity);
    for (const Entity child : children) {
        DestroyEntityAndDescendants(registry, child);
    }

    registry.DestroyEntity(entity);
}
```

### 3.6 — CMakeLists.txt (no change needed for the component header)

`src/ECS/Components/PrimitiveSource.h` must be added to `gte_core`'s
`add_library(gte_core STATIC ...)` source list in the root `CMakeLists.txt`
— insert it alongside the other `src/ECS/Components/*.h` entries (currently
lines 207–214), e.g. right after `src/ECS/Components/MeshAssetSource.h`
(line 211):

```
    src/ECS/Components/MeshAssetSource.h
    src/ECS/Components/PrimitiveSource.h
```

`TransformHierarchy.h`/`.cpp` are already listed (lines 205–206) — no new
CMake entry needed for the `DestroyEntityAndDescendants()` addition itself,
since it's added to an already-registered file.

### 3.7 — Tests

**`tests/ECS/TransformHierarchyTests.cpp`** (extend the existing file —
confirm it's already in `tests/CMakeLists.txt`'s `GTE_TEST_SOURCES`, line
1549 `ECS/TransformHierarchyTests.cpp` — it is, no CMake change needed) — add
new `TEST()`s for `DestroyEntityAndDescendants()`:
- A 3-level parent → child → grandchild chain: destroying the top-level
  parent leaves ALL THREE entities dead (`registry.IsAlive()` false for
  each).
- A leaf entity with no children: destroying it only removes that one
  entity; an unrelated sibling entity (sharing the same parent) survives
  untouched.
- Calling it on an already-dead entity (or a default-constructed
  `Entity{}`/`kInvalidEntity`) is a safe no-op — no crash, registry
  otherwise unaffected.
- A parent with TWO independent children, only one of which has its own
  grandchild: destroying the parent removes all 4 entities; a completely
  unrelated 5th entity (no parent/child relationship to any of them)
  survives untouched.

**`tests/Game/EntityInstantiatorTests.cpp`** (extend the existing file,
already registered — line 1551) — add new `TEST()`s:
- A blueprint with `primitiveSourceType = PrimitiveType::Sphere` set
  produces an entity carrying a `PrimitiveSource` component whose `type`
  field equals `PrimitiveType::Sphere` (`registry.HasComponent<PrimitiveSource>(entity)`
  is true, `registry.GetComponent<PrimitiveSource>(entity).type ==
  PrimitiveType::Sphere`).
- A blueprint with `primitiveSourceType` left at its default (`std::nullopt`)
  — e.g. an asset-spawned child node — produces an entity with **no**
  `PrimitiveSource` component at all (`registry.HasComponent<PrimitiveSource>(entity)`
  is false) — proving the two component kinds (`PrimitiveSource` vs.
  `MeshAssetSource`) never both/neither incorrectly attach.

No new test file/CMake entry is needed for `PrimitiveGpuCatalog.cpp`'s own
one-line change (3.3) — it needs a live `Renderer` to exercise
(`EnsureDefaultPipeline()`/`EnsurePrimitiveMesh()`), the same accepted "Tier
2, no automated coverage yet" bucket this class already falls into (no
`PrimitiveGpuCatalogTests.cpp` exists today either). Confirm the one-line
change by direct code review instead.

## Step 4: What We Will NOT Do (Focus)

- We will **not** add a `PrimitiveSource`-equivalent for anything besides
  primitives — Camera entities, and any other future non-primitive/
  non-asset entity kind, are explicitly out of this campaign's serialization
  scope (see PHASE0's Design Decisions).
- We will **not** make `DestroyEntityAndDescendants()` reparent surviving
  siblings, renumber sibling indices, or otherwise "clean up" anything
  beyond actually destroying the requested subtree — that is exactly what
  `Registry::DestroyEntity()` already does per-entity today, just applied
  recursively.
- We will **not** wire `PrimitiveSource`/`DestroyEntityAndDescendants()` into
  any Editor UI in this phase (e.g. a "Delete" context-menu item) — they are
  added here purely as reusable primitives PHASE4 consumes; any Editor UI
  use beyond this campaign's own Save/Load path is out of scope.
