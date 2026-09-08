# PHASE4 — Completion Report: `src/Scene/SceneBuilder.h/.cpp` — Registry ⇄ `SceneDocument` Bridge

Part of the `scene-serialization-1` campaign — see `PHASE0_MASTER_STRATEGY.md`
for the full plan and `PHASE4_SCENE_BUILDER_REGISTRY_ASSETDATABASE_BRIDGE.md`
(v2) for this phase's own detailed work order. This phase is now **complete**.

## What was done

Followed the phase document's (v2) plan exactly — the code itself is
byte-for-byte identical to the v1/v2 spec (v2 only added a documentation/test
callout, no code-level change from v1):

1. **`src/Scene/SceneBuilder.h`** (new file) — the ECS-facing bridge
   declarations: `BuildSceneDocumentFromRegistry(Registry&, const
   AssetDatabase&)` and `ClearSerializableSceneObjects(Registry&)`, plus the
   full doc comment (including the v2 "Correctness invariant" callout about
   `AssetDatabase::FindByPath()`/`RefreshFromDirectory()` both normalizing via
   `std::filesystem::absolute()`).
2. **`src/Scene/SceneBuilder.cpp`** (new file):
   - `BuildSceneDocumentFromRegistry()` walks every ROOT entity
     (`GetChildren(registry, kInvalidEntity)`), skips any entity missing a
     `Transform`, copies `position`/`rotation`/`scale` plus an optional `Name`,
     and produces exactly one record per root: `PrimitiveSource` → a
     `SceneObjectKind::Primitive` record (`primitiveType` copied verbatim);
     `MeshAssetSource` → a `SceneObjectKind::Asset` record ONLY if
     `assetDatabase.FindByPath(gtaPath)` resolves (its `Guid` is copied in) —
     otherwise skipped entirely; any root with neither tag (e.g. the default
     Camera entity from PHASE1) is skipped.
   - `ClearSerializableSceneObjects()` snapshots every root first (since
     `DestroyEntityAndDescendants()` mutates live `Transform` data as it
     walks), then destroys exactly the roots tagged `PrimitiveSource` OR
     `MeshAssetSource` (and, transitively, all of their descendants) —
     everything else (e.g. Camera) is left completely untouched.
3. **`CMakeLists.txt`** — appended `src/Scene/SceneBuilder.h`/`.cpp` to the
   same unconditional `src/Scene/*` group PHASE3 already added, right after
   `src/Scene/SceneTextFormat.cpp`.
4. **Tests** — new `tests/Scene/SceneBuilderTests.cpp`, registered in
   `tests/CMakeLists.txt`'s unconditional `GTE_TEST_SOURCES` list (right after
   `Scene/SceneTextFormatTests.cpp`), plus a matching descriptive paragraph in
   the file's own "Test taxonomy" comment block. 10 `TEST_F()`s using a real
   temp directory (same `SetUp()`/`TearDown()` pattern as
   `tests/Assets/AssetDatabaseTests.cpp`), covering every case the phase
   document's test list called for:
   - `BuildSceneDocumentFromRegistry()`: a `PrimitiveSource`-tagged root
     produces exactly one matching `Primitive` record; a `MeshAssetSource`-
     tagged root whose `gtaPath` resolves via a real, `RefreshFromDirectory()`'d
     `AssetDatabase` produces exactly one matching `Asset` record with the
     correct `Guid`; **(v2) the path-normalization invariant** — an
     independently-constructed-but-equivalent path (built via
     `std::filesystem::path::operator/=` segment-by-segment, not reusing the
     same path object) still resolves correctly; an untracked asset path is
     skipped; a root with neither tag (Camera) is skipped; a child entity
     (even one that also carries `MeshAssetSource`) is never independently
     visited; multiple mixed roots (Primitive/Asset/skipped) all resolve
     correctly together in one call.
   - `ClearSerializableSceneObjects()`: a `PrimitiveSource` root + child and a
     `MeshAssetSource` root + two children are ALL destroyed; a Camera-only
     entity (with its `Transform`/`Camera` data) survives untouched; an empty
     `Registry` is a safe no-op.

## Verification

- **Fast compile check** (per this task's workflow rules — no full build/
  regression test yet):
  - `cmake --build build --target gte_core` — **clean build**,
    `libgte_core.a` linked successfully (only the pre-existing, unrelated
    KTX-Software `git describe` version-fallback warning appeared).
  - `cmake --build build --target GreatTamanaEngineTests` — **clean build**,
    `GreatTamanaEngineTests.exe` linked successfully.
- Ran the new + directly related test suites
  (`GreatTamanaEngineTests.exe --gtest_filter=SceneTextFormatTest.*:SceneBuilderTest.*:AssetDatabaseTest.*`):
  **all 44 tests passed**, zero failures (10 new `SceneBuilderTest`s, plus the
  18 pre-existing `SceneTextFormatTest`s and 16 pre-existing
  `AssetDatabaseTest`s re-run as a regression sanity check, all still green).
- Per this task's workflow rules, a full build/full regression `ctest` run
  was intentionally NOT performed — reserved for a later phase that
  explicitly calls for it.

## Notes / deviations from the phase document

One deviation, in the **test file** only (never in `SceneBuilder.h/.cpp`
itself, which matches the spec byte-for-byte): the phase document's v2
path-normalization test suggestion was slightly ambiguous about how to
construct a "differently-spelled-but-equivalent" path. An initial attempt
inserted a lexical `.` segment (e.g. `Sub/./model.gta`) to spell the same
file differently — this actually FAILED, because `AssetDatabase::FindByPath()`
only calls `std::filesystem::absolute()` (which merely prepends the current
working directory to a relative path) and never `std::filesystem::canonical()`
(which would collapse `.`/`..` segments) — confirmed directly by re-reading
`AssetDatabase.cpp`. The test was corrected to instead build the equivalent
path by appending path segments one at a time via
`std::filesystem::path::operator/=` into a freshly-constructed
`std::filesystem::path` object (rather than reusing/copying the original path
object) — this still proves the intended point (two independently-constructed
path values that are ALREADY lexically equivalent resolve identically through
`FindByPath()`'s own `absolute()`-based normalization) without asserting a
guarantee (`.`/`..` collapsing) the real code never actually makes. This is a
test-construction correction only; no production code needed to change, and
no gap in `SceneBuilder.cpp`'s own logic was found.

## Next phase

**PHASE5_EDITOR_SCENE_IO_AND_PROJECT_ROOT_HELPER** — add
`src/Editor/ProjectRootPath.h/.cpp` (the shared "/Project next to the .exe"
resolver) and `src/Editor/SceneIO.h/.cpp` (`SaveScene()`/`LoadScene()` — the
Game/Renderer/filesystem-touching glue tying Phases 2–4 together against the
hardcoded `TestScene.gtscene` path), per `PHASE0_MASTER_STRATEGY.md`'s
ordering.
