# PHASE3 COMPLETION REPORT — JSON Scene Document, Full-Hierarchy Save, and a Working Generic Load

_Campaign: `task_manager/scene-serialization-2/`. Parent: `PHASE0_MASTER_STRATEGY.md`.
Phase file implemented: `PHASE3_JSON_SCENE_DOCUMENT_AND_HIERARCHY_SAVE_PLUS_GENERIC_LOAD.md`._

Branch: `feature/scene-serialization` (unchanged, as required).

## Prerequisites followed

- Read `README.md` and `AGENTS.md` at the project root.
- Re-read `PHASE0_MASTER_STRATEGY.md` in full for overall campaign context
  (Locked Design Decisions, Cross-Phase Invariants, Appendix A's exact
  on-disk JSON shape).
- Read `PHASE1_COMPLETION_REPORT.md` and `PHASE2_COMPLETION_REPORT.md` (the
  two prior phase reports in this folder) before starting — both confirmed
  zero behavior change and no note of anything deliberately left incomplete
  beyond what this phase's own `.md` file already describes.
- Read `PHASE3_JSON_SCENE_DOCUMENT_AND_HIERARCHY_SAVE_PLUS_GENERIC_LOAD.md`
  in full before writing any code.

## What was built

### 3.1 — `src/Scene/SceneDocument.h` rewritten

Old `SceneObjectKind`/`SceneObjectRecord` (root-only, fixed-schema) deleted
entirely. New shape, exactly matching the phase file's own code and
PHASE0's Appendix A:

- `SceneEntityRecord` — `parentIndex: std::optional<std::size_t>`,
  `siblingIndex: std::uint32_t`, `assetGuid: std::string` (empty this
  phase), `components: nlohmann::json` (generic bag).
- `SceneDocument` — `std::vector<SceneEntityRecord> entities`.

`src/Scene/` now legitimately depends on `nlohmann::json` directly, per
PHASE0's Locked Design Decision #1.

### 3.2 — `SceneTextFormat.h/.cpp` deleted; `SceneJsonFormat.h/.cpp` added

- Deleted `src/Scene/SceneTextFormat.h`, `src/Scene/SceneTextFormat.cpp`,
  and `tests/Scene/SceneTextFormatTests.cpp` in this same phase (not
  deferred), per the phase file's own explicit instruction.
- New `src/Scene/SceneJsonFormat.h/.cpp` — `kSceneJsonFormatVersion = 2`,
  `SerializeSceneDocument()` (via `nlohmann::json::dump(2)`, always
  succeeds), `DeserializeSceneDocument()` (never throws, returns
  `std::nullopt` on any malformed input per the exact rules enumerated in
  the header's own doc comment). Parsing convention: non-throwing
  `nlohmann::json::parse(text, nullptr, /*allow_exceptions=*/false)` +
  `.is_discarded()` check for the top-level parse (matching
  `Network/NetworkRoutes.cpp`'s own `ParseJsonNoThrow()`, confirmed via
  `search_in_dir` before writing this file, per the phase file's own
  instruction to "check what NetworkRoutes.cpp already does... and match it
  for consistency"), PLUS a defensive `try`/`catch (const
  nlohmann::json::exception&)` wrapped around the rest of the function
  (explicit structural checks come first; the catch is a backstop against
  any remaining `.get<T>()` edge case, e.g. an out-of-range integer
  literal) — belt-and-suspenders, but the net effect matches the phase
  file's "never throws" requirement exactly.
- New `tests/Scene/SceneJsonFormatTests.cpp` (18 tests) — empty document,
  one entity with no parent/no components, a 3-level parent chain by
  index, an arbitrary `components` bag round-tripping exactly, `asset_guid`
  round-tripping when present (and omitted from the JSON entirely when
  empty — confirmed against Appendix A's own "OPTIONAL... present ONLY
  for..." wording), rejection of invalid JSON text, a non-object top
  level, missing/wrong `gtscene_version`, non-array/missing `entities`, an
  out-of-range or negative `parent` index, non-object `components`, a
  non-object entity, tolerance of an unrecognized top-level AND per-entity
  key (forward-compat), and `sibling_index` defaulting to 0 when absent.

### 3.3 — `src/Scene/SceneBuilder.h/.cpp` rewritten — the SAVE half

`BuildSceneDocumentFromRegistry(Registry&, const AssetDatabase&)` keeps its
exact signature (per the phase file's own instruction — the `AssetDatabase`
parameter is accepted but unused this phase, explicitly `(void)`-cast with
a comment pointing at PHASE4). New algorithm: a recursive pre-order walk
(`WalkEntityRecursive()`, anonymous-namespace helper) starting from
`GetChildren(registry, kInvalidEntity)`, visiting every root and then every
one of its descendants — no `PrimitiveSource`/`MeshAssetSource` gating of
any kind. Each visited entity's own `SceneEntityRecord` is built by walking
`ComponentTypeRegistry::Instance().AllSortedByTypeName()` and calling
`tryGetConstComponent`/`writeJson` for whichever registered types that
SPECIFIC entity actually has — genuinely generic, zero per-component-type
branches, exactly as the phase file specifies.

`ClearSerializableSceneObjects()` was left completely untouched (still
`PrimitiveSource`/`MeshAssetSource`-root-only), per the phase file's own
explicit instruction — PHASE4's job.

### 3.4 — `Editor/SceneIO.cpp`'s `LoadScene()` rewritten — the GENERIC (simplified) LOAD half

Implemented the phase file's own two-pass (well, four-sub-pass: A, B1, B2,
B3) reconstruction algorithm essentially as specified, with ONE deliberate,
load-bearing correction — see "Discrepancy found" below. `SaveScene()`
itself only needed its two `#include`s swapped (`SceneTextFormat.h` →
`SceneJsonFormat.h`) — its own logic was already format-agnostic.

`src/Editor/SceneIO.h`'s doc comments were rewritten to describe the new
JSON format and generic reconstruction instead of the old TEXT format/
spawn-dispatch loop.

### 3.5 — What is DELIBERATELY still broken/incomplete after this phase

(Both explicitly called out by the phase file itself, confirmed unchanged
here):

- A `PrimitiveSource`/`MeshAssetSource`-tagged entity round-trips its
  Transform/Name/tag-component fields correctly, but gets NO `MeshRenderer`
  after Load — not visible. Verified directly:
  `SceneBuilderTest.AssetRootWithTrackedPathProducesOneAssetRecord` confirms
  the saved record's `components` bag has no `"MeshAssetSource"` key (that
  type isn't registered — PHASE2), and `Editor/SceneIO.cpp`'s Pass A still
  creates every entity as a bare entity (with only a default `Transform`
  now — see below) — PHASE4's own job to special-case this.
- `ClearSerializableSceneObjects()` is unchanged, so a plain/Camera/Light
  entity this phase NOW serializes is still NOT cleared before a fresh
  Load — a repeated Load can accumulate duplicate Camera/Light entities.
  PHASE4 replaces this with `ClearEntireScene()`.

### 3.6 — `tests/Scene/SceneBuilderTests.cpp` rewritten

Rewritten in place against the new `SceneDocument`/`SceneEntityRecord`
shape, per the phase file's own per-test rename/rewrite list:

- `PrimitiveRootProducesOnePrimitiveRecord` — now asserts against
  `document.entities[0].components["PrimitiveSource"]["type"]`/
  `["Name"]["value"]`/`["Transform"]["position"|"scale"]` (raw JSON, no
  round trip through `ComponentTypeRegistry` needed for a save-side test,
  per the phase file's own note).
- `AssetRootWithTrackedPathProducesOneAssetRecord` — asserts `assetGuid`
  is EMPTY (PHASE3 doesn't populate it) and the `components` bag has
  `"Transform"` but explicitly NOT `"MeshAssetSource"`.
- `AssetRootResolvesViaNormalizedEquivalentPath` — kept as a regression
  guard for the underlying `AssetDatabase` path-normalization behavior
  PHASE4 will depend on again (confirmed directly via `FindByPath()` since
  this phase's own `BuildSceneDocumentFromRegistry()` doesn't act on it
  yet).
- `AssetRootWithUntrackedPathIsSkipped` → renamed/rewritten to
  `AssetRootWithUntrackedPathStillProducesARecordWithNoGuid` — asserts a
  record IS produced (with empty `assetGuid`), not that `document.entities`
  is empty.
- `RootWithNeitherTagIsSkipped` → renamed/rewritten to
  `PlainCameraRootProducesACameraRecordToo` — asserts a record IS produced
  with both `"Camera"` and `"Transform"` keys, including the exact
  `nearZ`/`farZ` values.
- `ChildEntityIsNeverIndependentlyVisited` → renamed/rewritten to
  `ChildEntityIsSerializedWithParentIndex` — asserts the OPPOSITE: 2
  records exist, and the child's own `parentIndex` correctly points at the
  root's own array index (found by scanning for the record with
  `parentIndex.has_value()`, rather than assuming index order, since
  `BuildSceneDocumentFromRegistry()` never documented one).
- `MultipleIndependentRootsAllResolveCorrectly` — updated to expect **3**
  records now (Primitive, Asset, AND Camera — Camera used to be skipped).
- The three `ClearSerializableSceneObjects()` tests were left completely
  UNCHANGED, per the phase file's own instruction.
- Top-of-file doc comment and the matching `tests/CMakeLists.txt` block
  rewritten to describe the new generic-hierarchy-walk behavior.

**One extra test file beyond the phase file's own explicit list**:
`tests/Scene/SceneRoundTripIntegrationTests.cpp` (1 test,
`SceneRoundTripIntegrationTest.CameraWithNonDefaultNearFarPlusChildTransformNameRoundTripsExactly`)
— this phase's Definition of Done requires "a manual/local sanity check...
save a scene containing a Camera with a non-default `nearZ`/`farZ`, plus a
plain empty Transform+Name child node parented under it, reload, and
confirm both values AND the parent/child relationship are restored
exactly." `Editor/SceneIO.cpp`'s real `LoadScene()` needs a live
`Game`+`Renderer` (Tier 2/GPU-dependent — not available in this
environment/session), so this test reproduces the EXACT SAME Pass
A/B1/B2/B3 reconstruction sequence against a plain `Registry` instead
(`SetParent`/`SetSiblingIndex`/`ComponentTypeRegistry`, no Game/Renderer
involved at all) — a real, automated stand-in for the required manual
check, kept permanently as regression coverage rather than thrown away
after one manual run. **This test is what caught the discrepancy below.**

## Discrepancy found in this phase's own strategy file (fixed, not silently)

`PHASE3_JSON_SCENE_DOCUMENT_AND_HIERARCHY_SAVE_PLUS_GENERIC_LOAD.md`'s own
section 3.4 pseudocode has Pass A create a completely bare entity
(`registry.CreateEntity()`, no components at all), with Pass B1
immediately calling `SetParent()` on it. This does not actually work:
`ECS/TransformHierarchy.h`'s `SetParent()` explicitly requires BOTH the
child and a non-invalid parent to already have a `Transform` component, or
it "fails outright... leaving `child` completely untouched" (its own doc
comment). Run exactly as written, Pass B1 would silently fail to wire up
ANY parent/child relationship, every single Load — confirmed directly: the
new `SceneRoundTripIntegrationTests.cpp` failed with the reconstructed
child's `Transform::parent` still `kInvalidEntity` and
`GetChildren(destinationRegistry, reconstructedCamera)` empty, using the
phase file's own literal pseudocode, before the fix below was applied.

**Fix applied** (`Editor/SceneIO.cpp`'s Pass A, mirrored in the new
integration test): add a default `Transform` component to every entity
right in Pass A, immediately after `CreateEntity()`, before Pass B1 ever
runs. This is safe and has no other observable effect: every entity
`BuildSceneDocumentFromRegistry()` ever visits already had a live
`Transform` to begin with (`SceneBuilder.h`'s own documented scope
limitation — "An entity with no Transform component at all is out of
scope"), so its own record's `components` bag always has a `"Transform"`
key too; Pass B2 immediately afterward overwrites this default with the
EXACT saved field values, since `ensureDefaultComponent()` never resets an
already-present component. The net behavior after the fix is exactly what
the phase file's own Step 1/Definition-of-Done describe ("a plain
empty Transform+Name child node parented under it... restored exactly") —
only the LITERAL Pass-A code differs from the file's own inline snippet,
by the minimum needed to make Pass B1 actually succeed. Both `SceneIO.cpp`
and the new integration test carry an in-code comment explaining this
exact discrepancy and fix, for the next reader.

No other discrepancy was found — every other piece of this phase's own
`.md` file (the `SceneDocument.h`/`SceneJsonFormat.h` shapes, the SAVE-half
recursive walk, Pass B2/B3, the `SceneBuilderTests.cpp` rename/rewrite
list) matched the real codebase and worked exactly as described once
implemented.

## CMake wiring

- Root `CMakeLists.txt`: `src/Scene/SceneTextFormat.h`/`.cpp` removed;
  `src/Scene/SceneJsonFormat.h`/`.cpp` added in their place (same list
  position).
- `tests/CMakeLists.txt`: `Scene/SceneTextFormatTests.cpp` removed;
  `Scene/SceneJsonFormatTests.cpp` and
  `Scene/SceneRoundTripIntegrationTests.cpp` added; the two pre-existing
  Scene-related doc-comment blocks rewritten to describe the new behavior.

## Verification

- Re-ran `cmake -S . -B build` (picks up the edited `CMakeLists.txt` files)
  — configure succeeded (only the pre-existing, unrelated KTX
  git-describe warning).
- Fast compile check: `cmake --build build --target GreatTamanaEngineTests`
  — succeeded, **zero compiler errors or warnings** from any new/changed
  code.
- Ran the new/changed tests directly:
  `tests\GreatTamanaEngineTests.exe --gtest_filter=SceneJsonFormatTest.*:SceneBuilderTest.*:SceneRoundTripIntegrationTest.*`
  — **29/29 passed** (18 `SceneJsonFormatTest`, 10 `SceneBuilderTest`, 1
  `SceneRoundTripIntegrationTest`).
- Re-ran the full `Scene*`/`ECS`/`ComponentTypeRegistryTest`/
  `BuiltinComponentReflectionTest` filter set together — **59/59 passed**,
  confirming no regression in Phase 1/2's own reflection-core tests or the
  pre-existing `TransformHierarchyTest`/`SceneGridMathTest` suites.
- `GTE_ENABLE_EDITOR=OFF` fast compile check: re-ran `cmake -S . -B
  build-editor-off` then `cmake --build build-editor-off --target
  gte_core` — succeeded with zero errors (this phase's own `src/Scene/`
  changes have no Editor dependency; `Editor/SceneIO.cpp` itself is simply
  not compiled in this configuration, as before).
- Per this phase's own rule and the campaign-wide workflow rule, did
  **not** run a full build or the full `ctest` regression suite (reserved
  for Phase 6).
- `git status` confirms only the intended files changed: `CMakeLists.txt`,
  `tests/CMakeLists.txt`, `src/Editor/SceneIO.h`/`.cpp`,
  `src/Scene/SceneBuilder.h`/`.cpp`, `src/Scene/SceneDocument.h` (modified);
  `src/Scene/SceneTextFormat.h`/`.cpp`,
  `tests/Scene/SceneTextFormatTests.cpp` (deleted);
  `src/Scene/SceneJsonFormat.h`/`.cpp`,
  `tests/Scene/SceneJsonFormatTests.cpp`,
  `tests/Scene/SceneRoundTripIntegrationTests.cpp` (new) —
  `tests/Scene/SceneBuilderTests.cpp` modified in place.

## Definition of Done — checked against the phase file

- [x] `SceneDocument.h` rewritten to the hierarchy-aware, generic-component
      shape; old `SceneObjectKind`/`SceneObjectRecord` fully removed.
- [x] `SceneTextFormat.h/.cpp` and `tests/Scene/SceneTextFormatTests.cpp`
      deleted; `SceneJsonFormat.h/.cpp` and
      `tests/Scene/SceneJsonFormatTests.cpp` added and passing.
- [x] `SceneBuilder.cpp`'s save half walks the ENTIRE hierarchy recursively
      and captures every registered component generically.
- [x] `Editor/SceneIO.cpp`'s `LoadScene()` reconstructs hierarchy + applies
      every generic component field correctly for non-recipe entities
      (fixed to actually work — see "Discrepancy found" above).
- [x] `tests/Scene/SceneBuilderTests.cpp` rewritten per 3.6 — compiles and
      passes against the NEW `SceneDocument`/`SceneEntityRecord` shape,
      zero remaining reference to `SceneObjectRecord`/`SceneObjectKind`/
      `document.objects`.
- [x] A manual/local sanity check (promoted to a permanent, automated
      test — see `SceneRoundTripIntegrationTests.cpp` — since a live
      Game+Renderer wasn't available in this session/environment): a
      Camera with non-default `nearZ`/`farZ`, plus a plain empty
      Transform+Name child node parented under it, round-trips with both
      values AND the parent/child relationship restored exactly.
- [x] `PHASE3_COMPLETION_REPORT.md` (this file) documents the 3.5 "still
      broken" list.
- [x] Fast compile check passes, both `GTE_ENABLE_EDITOR=ON` (full
      `GreatTamanaEngineTests` target) and `GTE_ENABLE_EDITOR=OFF`
      (`gte_core` target alone).
- [x] `git add`/`git commit` follow next.

## Notes for PHASE4

- PHASE4's own recipe-spawn work replaces Pass A's loop body (the version
  that now adds a default `Transform` up front — see the discrepancy note
  above) with the recipe-aware spawn version; the explicit
  `// PHASE4 TODO:` comment at that exact spot in `SceneIO.cpp` still
  points there.
- PHASE4 should double-check its own strategy file's pseudocode (if any)
  against `SetParent()`'s real preconditions the same way this phase had
  to, given the bug found here.
- `ClearSerializableSceneObjects()` is unchanged; PHASE4 replaces it with
  `ClearEntireScene()` as already planned.
