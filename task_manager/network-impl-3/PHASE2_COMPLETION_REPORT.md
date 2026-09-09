# PHASE2 — COMPLETION REPORT: ECS Entity-By-Name Lookup + Unique-Naming Utilities

Parent: `PHASE0_MASTER_STRATEGY.md`
Phase document: `PHASE2_ECS_ENTITY_LOOKUP_AND_UNIQUE_NAMING_UTILITIES.md`
Branch: `feature/network-impl`

## Summary

Implemented Phase 2 of the `network-impl-3` campaign exactly as specified:
three small, pure, Tier-1-tested ECS building blocks
(`FindEntityByName`/`IsEntityNameInUse`/`MakeUniqueEntityName`) plus the
inverse of `PrimitiveMeshGenerator::ToString()`
(`TryParsePrimitiveTypeName()`). Zero networking/JSON dependency anywhere in
this phase — everything here is pure `Registry`/`Entity`/`PrimitiveType`
logic, exactly as the phase's own "Goal" required, and builds directly on top
of Phase 1's already-compiling/tested JSON work without touching it.

## What was done

### 1. `src/ECS/EntityQuery.h` / `src/ECS/EntityQuery.cpp` (new files)

- `Entity FindEntityByName(Registry&, const std::string&)` — linear scan over
  `ComponentStorage<Name>`'s dense array (`Size()`/`EntityAt()`/`ComponentAt()`),
  case-sensitive exact match. Returns `kInvalidEntity` for an empty query
  string (even if some live entity happens to have an empty `Name::value`) or
  when no entity matches.
- `bool IsEntityNameInUse(Registry&, const std::string&)` — thin wrapper over
  the above.
- `std::string MakeUniqueEntityName(Registry&, const std::string&)` — Unity's
  `"X"`, `"X (1)"`, `"X (2)"`, ... auto-dedup convention, mirroring
  `ProjectPanelData::MakeUniqueDestinationPath()`'s own probe loop shape.
  Deliberately does not try to detect/increment an already-`"(N)"`-shaped
  base name — documented in the header as intentional, matching the
  filesystem precedent's own simplicity. No artificial upper bound on the
  probe loop, also documented as intentional (mirrors the filesystem
  precedent, which is equally unbounded).

Registered in the root `CMakeLists.txt`'s explicit `target_sources(gte_core
PRIVATE ...)` list, immediately after the existing
`src/ECS/TransformHierarchy.h`/`.cpp` pair — confirmed via `search_in_dir`
that this project does NOT glob sources, so this step was mandatory for the
new symbols to link at all (per the phase document's own "CRITICAL" note).

### 2. `src/Renderer/Primitives/PrimitiveMeshGenerator.h` / `.cpp` (extended)

- Added `#include <string>` (previously absent/only transitively available)
  and the new declaration `bool TryParsePrimitiveTypeName(const std::string&
  name, PrimitiveType& outType) noexcept` right below the existing
  `ToString(PrimitiveType)`.
- Implemented in the `.cpp`: ASCII-only lower-casing via `std::tolower`
  (`<cctype>` added), then an exact match against the 5 known lower-case
  literals (`"cube"`, `"sphere"`, `"capsule"`, `"cone"`, `"plane"`). Returns
  `false` and leaves `outType` completely untouched for anything else,
  including an empty string — verified by tests using a pre-seeded sentinel
  value.

### 3. Tests

- New `tests/ECS/EntityQueryTests.cpp` (9 tests), added to
  `tests/CMakeLists.txt`'s `GTE_TEST_SOURCES` list right after the existing
  `ECS/TransformHierarchyTests.cpp` line — covers every case enumerated in
  the phase document's own "3.4 — Tests" section (found-among-several,
  not-found, empty-query-with-a-real-empty-`Name`-entity-present,
  no-entities-with-Name-at-all, `IsEntityNameInUse` mirroring, base-name
  verbatim, single/second suffix probing order, and the
  already-`"(N)"`-shaped base name case).
- Extended the existing `tests/Renderer/PrimitiveMeshGeneratorTests.cpp` with
  3 new tests covering `TryParsePrimitiveTypeName()`: all 5 shape names in
  at least two casings each, an unrecognized string leaving `outType`
  untouched, and an empty string leaving `outType` untouched.

## Verification performed

1. **Fast compile check** (`cmake --build build --config Debug --target
   GreatTamanaEngineTests`, from the repo root): full build succeeded with
   zero errors/warnings related to this change (35/35 build steps, including
   `src/ECS/EntityQuery.cpp.obj`, `src/Renderer/Primitives/
   PrimitiveMeshGenerator.cpp.obj`, and both new/extended test object files).
   No other translation unit broke.
2. **Targeted test run** (`ctest -C Debug -R
   "EntityQuery|PrimitiveMeshGenerator" --output-on-failure`): **20/20 tests
   passed** — every pre-existing `PrimitiveMeshGeneratorTest` case plus all
   new `EntityQueryTest`/`TryParsePrimitiveTypeName_*` cases.
3. **Full regression pass** (`ctest -C Debug --output-on-failure`, per the
   phase document's own explicit recommendation to run the whole suite at
   this specific point, since it's cheap and buys confidence before Phase 3
   builds directly on top of it): **1110/1110 tests passed** (1
   pre-existing, machine-gated smoke test — `PmxLoaderRealModelSmokeTest.
   LoadsAnMmdModelIfPresentOnThisMachine` — skipped, same as every prior
   session in this repository). Zero regressions anywhere else in the suite.

No full engine build (`cmake --build build` targeting the main executable)
was run — only the test target, per this task's own workflow rules ("Fast
Compile Check" only, unless a phase explicitly calls for a full build; this
one didn't).

## Deviations from the phase document

None. `Registry::Storage<T>()` was confirmed public and non-const exactly as
the phase document assumed before finalizing the implementation (read
`Registry.h` directly rather than trusting the assumption blindly, per the
document's own "verify... before finalizing" note).

## Next phase

`PHASE3_GAME_LEVEL_INSTANTIATE_AND_DELETE_APIS.md` — new
`src/Game/EngineCommandResults.h` (plain outcome structs) plus new public
`Game::InstantiatePrimitive()`/`Game::DeleteEntityByName()` methods that do
the actual ECS mutation, built directly on top of this phase's
`FindEntityByName`/`MakeUniqueEntityName`/`TryParsePrimitiveTypeName`
utilities — nothing in this phase's own new code needs any changes for
Phase 3 to build on top of it.
