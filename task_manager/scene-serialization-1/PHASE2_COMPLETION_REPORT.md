# PHASE2 — Completion Report: `PrimitiveSource` Component + `TransformHierarchy::DestroyEntityAndDescendants()`

Part of the `scene-serialization-1` campaign — see `PHASE0_MASTER_STRATEGY.md`
for the full plan and
`PHASE2_PRIMITIVE_SOURCE_TAG_AND_HIERARCHY_DESTROY_HELPER.md` for this phase's
own detailed work order. This phase is now **complete**.

## What was done

Followed the phase document's plan exactly — every file/step below matches
the work order 1:1:

1. **`src/ECS/Components/PrimitiveSource.h`** (new file) — a tiny plain-data
   component, `struct PrimitiveSource { PrimitiveType type =
   PrimitiveType::Cube; };`, modeled directly on `MeshAssetSource.h`'s own
   shape/doc-comment style. Records which `PrimitiveType` a
   `Game::CreatePrimitiveEntity()`-spawned entity is, the primitive-spawn
   counterpart of `MeshAssetSource::gtaPath`.
2. **`src/Game/Instantiation/EntityBlueprint.h`** — added
   `#include "../../Renderer/Primitives/PrimitiveMeshGenerator.h"` and
   `#include <optional>`, plus a new
   `std::optional<PrimitiveType> primitiveSourceType;` field on
   `EntityBlueprintNode`, placed right after `meshAssetSourcePath` and before
   `children`, with a doc comment explaining the mutual-exclusivity
   convention with `meshAssetSourcePath`.
3. **`src/Game/Instantiation/PrimitiveGpuCatalog.cpp`** —
   `PrimitiveGpuCatalog::Resolve()` now also sets
   `blueprint.primitiveSourceType = type;` alongside its existing
   `pipeline`/`mesh` assignments.
4. **`src/Game/Instantiation/EntityInstantiator.cpp`** — added
   `#include "../../ECS/Components/PrimitiveSource.h"` and a new block
   mirroring the existing `meshAssetSourcePath` check:
   `if (blueprint.primitiveSourceType.has_value()) { registry.AddComponent<PrimitiveSource>(entity, PrimitiveSource{ *blueprint.primitiveSourceType }); }`.
5. **`src/ECS/TransformHierarchy.h`/`.cpp`** — added
   `void DestroyEntityAndDescendants(Registry& registry, Entity entity);`,
   implemented as a post-order recursive destroy: snapshots `GetChildren()`
   into a local vector BEFORE recursing/destroying (since
   `Registry::DestroyEntity()` invalidates the live `Transform::parent` data
   `GetChildren()` reads), recurses into each child first, then destroys
   `entity` itself via `Registry::DestroyEntity()`. Safe no-op on an
   already-dead/invalid entity (checked via `registry.IsAlive(entity)` up
   front).
6. **`CMakeLists.txt`** — added
   `src/ECS/Components/PrimitiveSource.h` to `gte_core`'s source list,
   right after `src/ECS/Components/MeshAssetSource.h` (line 211/212).
   `TransformHierarchy.h`/`.cpp` were already registered — no change needed
   there.
7. **Tests** — extended both existing test files (both already registered in
   `tests/CMakeLists.txt`, no new CMake entries needed):
   - **`tests/ECS/TransformHierarchyTests.cpp`** — 4 new `TEST()`s for
     `DestroyEntityAndDescendants()`: a 3-level parent→child→grandchild chain
     (all three die); a leaf-only destroy leaving an unrelated sibling and
     the parent alive; a safe no-op on an already-dead entity, on
     `kInvalidEntity`, and on a default-constructed `Entity{}`; and a wider
     tree (parent with two children, one of which has its own grandchild,
     plus a completely unrelated 5th entity) confirming exactly the 4
     related entities die and the unrelated one survives.
   - **`tests/Game/EntityInstantiatorTests.cpp`** — 2 new `TEST()`s: a
     blueprint with `primitiveSourceType = PrimitiveType::Sphere` produces an
     entity carrying a `PrimitiveSource` component with `type ==
     PrimitiveType::Sphere`; a blueprint with `primitiveSourceType` left at
     `std::nullopt` produces an entity with **no** `PrimitiveSource`
     component at all — proving `PrimitiveSource` and `MeshAssetSource` never
     both/neither incorrectly attach.
   - No new test file/CMake entry was added for `PrimitiveGpuCatalog.cpp`'s
     one-line change — it needs a live `Renderer` to exercise, the same
     accepted "Tier 2, no automated coverage yet" bucket that class already
     falls into (confirmed by direct code review instead, per the phase
     document's own instruction).

## Verification

- **Fast compile check** (per this task's workflow rules — no full build/
  regression test yet):
  - `cmake --build build --target gte_core` — **clean build**,
    `libgte_core.a` linked successfully. Only pre-existing, unrelated
    warnings (KTX-Software `git describe` version-fallback) appeared, no
    errors.
  - `cmake --build build --target GreatTamanaEngineTests` — **clean build**,
    `GreatTamanaEngineTests.exe` linked successfully.
- Ran the new/extended test suites directly (`GreatTamanaEngineTests.exe
  --gtest_filter=*TransformHierarchyTest*:*EntityInstantiatorTest*`): **all
  34 tests passed** (23 `TransformHierarchyTest` + 11
  `EntityInstantiatorTest`, including all newly-added ones) — zero
  regressions in either pre-existing suite.
- Per this task's workflow rules, a full build/full regression `ctest` run
  was intentionally NOT performed — that is reserved for a later phase that
  explicitly calls for it.

## Notes / deviations from the phase document

None — every step was implemented exactly as specified in
`PHASE2_PRIMITIVE_SOURCE_TAG_AND_HIERARCHY_DESTROY_HELPER.md`, including
field placement, doc comments, and the CMake insertion point.

## Next phase

**PHASE3_SCENE_DOCUMENT_AND_TEXT_FORMAT** — build the new, always-compiled
`src/Scene/` module (`SceneDocument.h` plain data +
`SceneTextFormat.h/.cpp` pure text ⇄ `SceneDocument` read/write for the new
`*.gtscene` format), per `PHASE0_MASTER_STRATEGY.md`'s ordering.
