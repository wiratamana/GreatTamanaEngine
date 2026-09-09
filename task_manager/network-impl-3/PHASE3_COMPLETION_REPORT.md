# PHASE3 — COMPLETION REPORT: Game-Level `InstantiatePrimitive()` / `DeleteEntityByName()` APIs

Parent: `PHASE0_MASTER_STRATEGY.md`
Phase document: `PHASE3_GAME_LEVEL_INSTANTIATE_AND_DELETE_APIS.md`
Branch: `feature/network-impl`

## Summary

Implemented Phase 3 of the `network-impl-3` campaign exactly as specified:
a new plain-data `src/Game/EngineCommandResults.h` (`InstantiatePrimitiveOutcome`/
`DeleteEntityOutcome`) plus two new public `Game` methods —
`Game::InstantiatePrimitive()` and `Game::DeleteEntityByName()` — that perform
the actual ECS mutation this campaign is about, callable from completely
ordinary in-process C++ code (no HTTP, no JSON, no cross-thread bridge
anywhere in this phase). Built directly on top of Phase 2's
`EntityQuery.h` (`FindEntityByName`/`MakeUniqueEntityName`) and
`PrimitiveMeshGenerator::TryParsePrimitiveTypeName()` — neither of which
needed any changes.

## What was done

### 1. `src/Game/EngineCommandResults.h` (new file)

A small, plain-data-only header (`Entity.h` + `<cstdint>`/`<string>` only —
no Renderer/Registry/ECS dependency beyond `Entity.h`, matching the phase
document's own rationale so both `Game.h` — this phase — and a future
`Application`-layer `EngineCommandBridge.h` — Phase 4 — can include it without
pulling in the other's heavier dependencies):

- `InstantiatePrimitiveOutcome` — `success`/`errorMessage`, `entityIndex`/
  `entityGeneration`, `resolvedName` (the actual, auto-deduplicated name), and
  `parentRequestedButNotFound`/`requestedParentName` (the non-fatal "parent
  name didn't resolve" warning case).
- `DeleteEntityOutcome` — `success`/`errorMessage`,
  `deletedEntityIndex`/`deletedEntityGeneration`.

Registered in the root `CMakeLists.txt`'s explicit `target_sources(gte_core
PRIVATE ...)` list, immediately after the existing `src/Game/Game.h` line —
matching the established "every header gets listed too, for consistency"
convention this codebase's `CMakeLists.txt` already follows (confirmed this
project does not glob sources, per Phase 2's own prior finding).

### 2. `src/Game/Game.h` (extended)

- Added `#include "EngineCommandResults.h"` to the existing include block.
  `Vec3` needed no dedicated include — confirmed (as the phase document
  predicted) it's already transitively available via the existing
  `ECS/Registry.h -> ... -> ECS/Components/Transform.h -> Math/Vec3.h` chain
  (`Game.cpp` already constructed a bare `Vec3{...}` before this phase with no
  `Vec3.h` include of its own).
- Added two new public method declarations, placed after
  `GetGpuSkinningPipelines()` (the last existing public method) and before the
  `private:` section, each with the full doc comment specified in the phase
  document:
  - `InstantiatePrimitiveOutcome InstantiatePrimitive(Renderer& renderer, const std::string& shapeName, const std::string& requestedName, const Vec3& worldPosition, bool hasParent, const std::string& parentName);`
  - `DeleteEntityOutcome DeleteEntityByName(const std::string& name);`

`CreatePrimitiveEntity()`'s own existing declaration/behavior is completely
untouched — the Editor's "Create 3D Object" menu (`Panels/HierarchyPanel.cpp`)
needed zero changes.

### 3. `src/Game/Game.cpp` (extended)

- Added `#include "ECS/Components/Name.h"`, `#include "ECS/EntityQuery.h"`,
  and `#include "ECS/TransformHierarchy.h"` to the existing include block.
- Implemented `Game::InstantiatePrimitive()` exactly per the phase document's
  reference implementation:
  1. Parses `shapeName` via `TryParsePrimitiveTypeName()` — fails the whole
     call (`success == false`, no entity created) for an unrecognized shape,
     with an error message listing the 5 valid names.
  2. Resolves the base name (falls back to the shape's own `ToString()` when
     `requestedName` is empty) and runs it through `MakeUniqueEntityName()`.
  3. Spawns via the existing `CreatePrimitiveEntity()` (defensively checked
     against `kInvalidEntity`, even though that's not currently a documented
     possibility — cheap insurance per the phase document's own note).
  4. Sets `Transform::position` to `worldPosition` (valid since a freshly
     spawned, still-unparented entity's local position IS its world
     position) and adds the `Name` component with the resolved unique name.
  5. If `hasParent` is true, looks up `parentName` via `FindEntityByName()`;
     on success, calls `SetParent(..., worldPositionStays = true)` (preserving
     the just-set world position); on failure, sets
     `parentRequestedButNotFound`/`requestedParentName` on the outcome but
     still reports overall `success == true` — the entity is never rolled
     back, matching PHASE0's Locked Design Decision #2.
- Implemented `Game::DeleteEntityByName()` exactly per the phase document's
  reference implementation: looks up the name via `FindEntityByName()`
  (failing with a distinct message for an empty name vs. a genuinely
  not-found name), then destroys the entity and every descendant via the
  pre-existing, unmodified `DestroyEntityAndDescendants()`.

Both implementations match the phase document's suggested reference
implementation verbatim — no deviation was needed after verifying the actual
include paths/signatures used elsewhere in this file (`ECS/Components/*.h`,
not a `../` relative path, matching every other include already in
`Game.cpp`).

## Verification performed

1. **Fast compile check** (`cmake --build build --config Debug --target
   GreatTamanaEngineTests`, from the repo root): after fixing one self-inflicted
   editing mistake (see "Deviations" below), the build succeeded cleanly —
   `src/Game/Game.cpp.obj` recompiled, `libgte_core.a` relinked, and
   `tests/GreatTamanaEngineTests.exe` relinked successfully, with zero
   errors/warnings related to this change. No other translation unit was
   affected (`EngineCommandResults.h` is header-only and only newly included by
   `Game.h`/`Game.cpp`).
2. Per this phase's own testability note (Step 3.4 of the phase document):
   `Game::InstantiatePrimitive()` touches a live `Renderer` (via
   `CreatePrimitiveEntity()` → `MeshInstantiationSystem` → `PrimitiveGpuCatalog`,
   real GPU resource creation), placing the method as a whole into this
   codebase's already-accepted "Tier 2, no automated coverage yet" bucket —
   expected and accepted, matching `CreatePrimitiveEntity()`'s own existing
   status. The parts of its logic that don't need a live Renderer (shape-name
   parsing, name deduplication, parent lookup) are already independently
   Tier-1-tested by Phase 2 and were not touched here.
   `Game::DeleteEntityByName()` touches no Renderer/GPU state at all (only
   `Registry`), so it would be a good, cheap Tier-1 test candidate — per the
   phase document's own explicit allowance ("if `Game`'s own constructor/other
   members make this awkward without a live `Renderer` anywhere nearby, it is
   fine to defer this specific test to Phase 6 instead"), this was
   **deferred to Phase 6** rather than added now, to keep this phase scoped to
   exactly what its own document asked for (this phase's own "Verification"
   section explicitly allows a plain fast compile check to be sufficient when
   no such test is added). No full build or full regression (`ctest` over the
   whole suite) was run, per this campaign's workflow rules — only this
   phase's own fast compile check, as instructed.

## Deviations from the phase document

One self-inflicted mistake made and corrected during editing (not a deviation
from the *design*, purely a mechanical editing slip): an intermediate
`edit_line` call targeting `Game.cpp` used a `length` value that (after being
silently clamped to the file's actual remaining line count, per that tool's
own documented behavior) removed more of the original file's tail than the
replacement content re-supplied, truncating `EnsureDefaultCameraExists()`'s
body, `Render()`, and the closing `namespace gte` brace. This was caught
immediately by the very next compile check (`error: expected '}' at end of
input`) rather than missed — the file was corrected via a full `write_file`
rewrite restoring every original line verbatim (cross-checked directly
against this same file's own content captured earlier in this session, before
any edits) plus the two new method implementations, and the subsequent
compile check confirmed a clean build. No other deviation from the phase
document's own plan — every locked decision, file, method signature, and
implementation body was implemented exactly as specified.

## Next phase

`PHASE4_ENGINE_COMMAND_BRIDGE_AND_MAIN_LOOP_INTEGRATION.md` — new
`src/Application/EngineCommandBridge.h/.cpp` (the cross-thread bridge,
mirroring `FrameCaptureBridge`) plus new `src/Application/EngineCommandDispatch.h/.cpp`
and `Application.h/.cpp` wiring (constructor, `Run()`'s early per-frame
drain), built directly on top of this phase's `Game::InstantiatePrimitive()`/
`Game::DeleteEntityByName()` — nothing in this phase's own new code needs any
changes for Phase 4 to build on top of it.
