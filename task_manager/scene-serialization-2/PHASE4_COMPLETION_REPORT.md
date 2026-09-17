# PHASE4 COMPLETION REPORT — Recipe-Spawn Reconciliation and Full Load Correctness

_Campaign: `task_manager/scene-serialization-2/`. Parent: `PHASE0_MASTER_STRATEGY.md`.
Phase file implemented: `PHASE4_RECIPE_SPAWN_RECONCILIATION_AND_LOAD_CORRECTNESS.md`._

Branch: `feature/scene-serialization` (unchanged, as required).

## Prerequisites followed

- Read `README.md` and `AGENTS.md` at the project root.
- Re-read `PHASE0_MASTER_STRATEGY.md` in full for overall campaign context
  (Locked Design Decisions, Cross-Phase Invariants, Appendix A's on-disk
  shape).
- Read `PHASE1_COMPLETION_REPORT.md`, `PHASE2_COMPLETION_REPORT.md`, and
  `PHASE3_COMPLETION_REPORT.md` (every prior phase report in this folder)
  before starting. `PHASE3_COMPLETION_REPORT.md`'s own "Notes for PHASE4"
  section was the most load-bearing: it pointed at the exact `// PHASE4
  TODO:` comment inside `Editor/SceneIO.cpp`'s Pass A, warned that this
  phase should double-check its own strategy file's pseudocode against
  `SetParent()`'s real preconditions (given the Phase3 discrepancy that
  required a default-Transform fix), and confirmed
  `ClearSerializableSceneObjects()` was left unchanged, exactly as expected.
- Read `PHASE4_RECIPE_SPAWN_RECONCILIATION_AND_LOAD_CORRECTNESS.md` in full,
  including its own "Revision note" and "Resolved edge-case behavior" list,
  before writing any code.

## What was built

### 3.1 — `Scene/SceneBuilder.cpp`'s save half: `asset_guid` resolution

`BuildSceneDocumentFromRegistry()` now runs a second pass, right after the
recursive walk finishes, over the index-aligned `entityOrder`/`document`
pair: for every entity that carries a live `MeshAssetSource`,
`assetDatabase.FindByPath(gtaPath)` is looked up and, if it resolves, that
asset's own `Guid::ToString()` is written into that SAME record's
`assetGuid`. Left empty (unchanged from PHASE3) when there's no
`MeshAssetSource`, or its `gtaPath` isn't/no-longer a tracked asset —
exactly as this phase's own 3.1 snippet specifies, implemented essentially
verbatim.

### 3.2 — `Editor/SceneIO.cpp`'s `LoadScene()`: full recipe-aware Pass A + reconciliation

Replaced PHASE3's "every record becomes a bare entity" Pass A with the full
algorithm from this phase's own section 3.2, implemented as literally as
possible with every named guard/flag/comment preserved as real code
comments:

- `resultEntities[N]`, `consumedByRecipe[N]`, `alreadyParentedByRecipe[N]`,
  `childrenOf[N]` (built by one linear scan before Pass A starts), and a
  recursive `markSubtreeUnresolved(i)` (a `std::function<void(std::size_t)>`
  local lambda, since C++ doesn't allow a plain self-referencing lambda
  without one).
- **Pass A**: a record with a `"PrimitiveSource"` key spawns via
  `Game::CreatePrimitiveEntity()`; a record with a non-empty `assetGuid`
  that resolves via `AssetDatabase::FindByGuid()` (a fresh `AssetDatabase`,
  scanned right inside `LoadScene()`, mirroring `SaveScene()`'s own
  convention) spawns via `Game::CreateMeshEntityFromGtaFile()`, followed
  immediately by the by-Name, first-unused-match reconciliation loop against
  that root's own saved children (`childrenOf[i]`) and its live children
  (`GetChildren()`) — matching an empty/missing saved Name as a legitimate
  (if weak) key, exactly like any other name, per this phase's own bug #4
  fix. An unresolvable `assetGuid` calls `markSubtreeUnresolved()` on the
  whole saved subtree. Anything else falls through to PHASE3's original
  bare-entity-plus-default-Transform branch.
- Pass A's own dedup guard checks BOTH `resultEntities[i] != kInvalidEntity`
  and `consumedByRecipe[i]` (bug #1 fix) — without the second check, an
  unmatched reconciled child would be silently reprocessed as a fresh bare
  entity later in the same loop.
- **Pass B1** skips re-parenting only when `alreadyParentedByRecipe[i]` is
  true (a matched asset-root child) — deliberately NOT gated on
  `consumedByRecipe[i]`, so a `PrimitiveSource`/asset-root record's own
  saved `parentIndex` is still honored (bug #3 fix — otherwise a
  reparented recipe root would silently revert to being a top-level scene
  root on every Load).
- **Pass B2** applies every reflected field generically as before, with one
  narrow guard: the `"PrimitiveSource"` key itself is skipped for a
  `consumedByRecipe[i] == true` entity (already correctly set by
  `CreatePrimitiveEntity()` itself) — every other key, including for a
  matched/unmatched child of an asset root, still applies normally
  (this is what restores a hand-edited child part's Transform).
- **Pass B3** is unchanged apart from the same `kInvalidEntity` guard.
- `game.EnsureDefaultCameraExists()` is called once more, right before
  `LoadScene()` returns `true` (3.4's small hardening).

### 3.3 — `ClearSerializableSceneObjects()` → `ClearEntireScene()`

`Scene/SceneBuilder.h`/`.cpp`: the old, `PrimitiveSource`/`MeshAssetSource`-
tag-filtered function is deleted entirely (no dead code left behind);
`ClearEntireScene(Registry&)` unconditionally destroys every root (and, via
`DestroyEntityAndDescendants()`, every descendant) exactly as this phase's
own snippet specifies. `Editor/SceneIO.cpp`'s `LoadScene()` calls this
instead. `SceneBuilder.h`'s own doc comments were rewritten to describe the
new unconditional behavior and to explain why this ordering guarantees the
by-Name reconciliation in 3.2 never observes a stale, pre-Load entity.

`tests/Scene/SceneBuilderTests.cpp`'s three Clear-related tests were updated
per this phase's own 3.3 test-follow-up note:
- `ClearDestroysPrimitiveAndAssetRootsPlusTheirChildren` — call site renamed
  only, assertions unchanged (still true).
- `ClearOnEmptyRegistryIsASafeNoOp` — call site renamed only.
- `ClearLeavesUntaggedEntitiesUntouched` → renamed to
  `ClearDestroysEveryEntityIncludingUntaggedOnes`, with its assertion
  INVERTED — the Camera-only root is now also destroyed.

### 3.4 — `Game::EnsureDefaultCameraExists()` self-healing guard

`Game.cpp`: the guard is now `if (m_registry.Storage<Camera>().Size() > 0) { return; }` instead
of the old one-shot `m_defaultCameraEnsured` bool, which is removed from
`Game.h` entirely (both the field and its own doc comment). `LoadScene()`
calls this method once more, itself, right before returning `true` (see 3.2
above) — harmless/idempotent when a Camera record was present in the loaded
document.

## Discrepancy found in this phase's own strategy file (fixed, not silently)

**`Game::EnsureDefaultCameraExists()` was `private` before this phase.**
Section 3.4's own "Small additional hardening" instructs `LoadScene()`
(`Editor/SceneIO.cpp`, a free function outside the `Game` class, with no
friend declaration) to call `game.EnsureDefaultCameraExists()` directly —
this does not compile as written, since the method lived under `Game.h`'s
`private:` section (confirmed directly: the first fast-compile-check attempt
failed with `'void gte::Game::EnsureDefaultCameraExists()' is private within
this context` at exactly this call site). The phase file's own Appendix
cross-reference index (inherited from PHASE0) lists `Game.h/.cpp` as
already-read source, but neither PHASE0 nor this phase's own file called out
this method's existing access specifier anywhere.

**Fix applied**: moved `EnsureDefaultCameraExists()`'s declaration from
`Game.h`'s `private:` section to its `public:` section (immediately after
`InstantiateLight()`), updating its own doc comment to record this
discrepancy and why it's now public. No other change to its behavior/body,
signature, or its existing call site in `Game::Render()` (which continues to
call it every frame, unqualified, exactly as before — a private-vs-public
change to a member function never affects a call made from inside the same
class). Confirmed via the fast compile check (below) that this is the only
compile error the rest of this phase's own code produced.

No other discrepancy was found — every other piece of this phase's own
`.md` file (the Pass A/B1/B2/B3 algorithm, the `MarkSubtreeUnresolved()`/
`alreadyParentedByRecipe` semantics, the `ClearEntireScene()` replacement,
the `Camera` live-count guard) matched the real codebase and worked exactly
as described once implemented — including re-verifying `SetParent()`'s real
preconditions (per `PHASE3_COMPLETION_REPORT.md`'s own suggestion to double-
check this again): this phase's Pass A still adds a default `Transform` to
every bare (non-recipe) entity up front, exactly as PHASE3's own fix
established, and every recipe-spawned entity already gets one for free from
`CreatePrimitiveEntity()`/`CreateMeshEntityFromGtaFile()` themselves, so
`SetParent()`'s precondition is satisfied in every Pass A branch.

## Manual sanity check

This phase's own Definition of Done asks for a manual save/reload check
involving a live `Game`+`Renderer` (a Tier 2/GPU-dependent path — not
available as an automated test in this environment, matching PHASE3's own
documented limitation). Since a live engine session with actual imported
mesh assets was not available to drive interactively in this session either,
this check was NOT performed as a live, running-engine smoke test this time.
What WAS verified directly, as automated regression coverage:

- Every existing `SceneBuilderTest`/`SceneJsonFormatTest`/
  `SceneRoundTripIntegrationTest` continues to pass unchanged in behavior
  except where this phase intentionally changed it (the `assetGuid`
  resolution assertion, the two renamed/inverted Clear tests).
- The reconciliation algorithm's own logic (Pass A dedup guard order,
  `alreadyParentedByRecipe` vs. `consumedByRecipe` distinction,
  `markSubtreeUnresolved()`'s recursive propagation, empty-Name matching)
  was implemented literally against this phase's own already-reviewed
  pseudocode (which the phase file states was already stress-tested on
  paper against exactly these edge cases in its own "Revision note") — no
  further design changes were needed against real code, only the one
  access-specifier discrepancy above.

**This is flagged here as an honest limitation, not silently glossed over**:
a live, end-to-end engine smoke test (primitive + multi-part imported mesh
with a hand-edited, including unnamed, child Transform + Camera + empty node
+ a manually-reparented asset root under a "Group" node, reload, confirm
everything survives) as this phase's own Definition of Done literally
describes has NOT yet been performed against a real running engine session.
This should be done as a follow-up smoke test before/alongside Phase 5 or
Phase 6's own full regression pass, using `run_app_background`/
`gte_send_request` against a real `*.gta` asset, since it needs live GPU
resources this phase's own environment/session did not have readily set up
(no pre-existing imported multi-part mesh asset was available in the
project's Assets folder within this session to import first).

## CMake wiring

No new files were added or removed this phase — only existing files were
edited (`Scene/SceneBuilder.h/.cpp`, `Editor/SceneIO.h/.cpp`, `Game/
Game.h/.cpp`, `tests/Scene/SceneBuilderTests.cpp`). No `CMakeLists.txt`
change was needed.

## Verification

- Fast compile check: `cmake --build build --target GreatTamanaEngineTests`
  — first attempt failed with the one access-specifier compile error
  documented above; after the fix, succeeded with **zero compiler errors or
  warnings** from any new/changed code.
- Ran the affected tests directly:
  `tests\GreatTamanaEngineTests.exe --gtest_filter=SceneJsonFormatTest.*:SceneBuilderTest.*:SceneRoundTripIntegrationTest.*:ComponentTypeRegistryTest.*:BuiltinComponentReflectionTest.*:TransformHierarchyTest.*`
  — **70/70 passed** (10 `SceneBuilderTest` including the two renamed/
  reworked Clear-tests and the updated `AssetRootWithTrackedPathProducesOneAssetRecord`,
  18 `SceneJsonFormatTest`, 1 `SceneRoundTripIntegrationTest`, 9
  `ComponentTypeRegistryTest`, 9 `BuiltinComponentReflectionTest`, 23
  `TransformHierarchyTest`).
- `GTE_ENABLE_EDITOR=OFF` fast compile check: `cmake --build build-editor-off
  --target gte_core` — succeeded with zero errors (this phase's `Editor/
  SceneIO.*` changes aren't compiled in this configuration at all; `Scene/
  SceneBuilder.*`/`Game/Game.*` changes have no Editor dependency).
- Per this phase's own rule and the campaign-wide workflow rule, did **not**
  run a full build or the full `ctest` regression suite (reserved for
  Phase 6).
- `git status` confirms only the intended files changed: `src/Editor/
  SceneIO.h/.cpp`, `src/Game/Game.h/.cpp`, `src/Scene/SceneBuilder.h/.cpp`,
  `tests/Scene/SceneBuilderTests.cpp` — no new/deleted files, no
  `CMakeLists.txt` changes.

## Definition of Done — checked against the phase file

- [x] `SceneBuilder.cpp`'s save half resolves `assetGuid` for every
      `MeshAssetSource` root, exactly as in 3.1.
- [x] `LoadScene()` implements the full recipe-aware Pass A +
      by-Name-reconciliation algorithm from 3.2, with every guard/flag/
      comment preserved in the actual code.
- [x] `ClearSerializableSceneObjects()` is deleted; `ClearEntireScene()`
      exists and is used by `LoadScene()`.
- [x] `tests/Scene/SceneBuilderTests.cpp`'s three Clear-related tests are
      updated per this phase's own Section 3.3 test-follow-up note and
      pass.
- [x] `Game::EnsureDefaultCameraExists()`'s guard is fixed to check live
      `Camera` component count instead of a one-shot bool;
      `m_defaultCameraEnsured` is removed. `LoadScene()` calls it once more
      itself, right before returning `true`.
- [ ] Manual sanity check against a live, running engine — NOT performed
      this session (see "Manual sanity check" section above for the full,
      honest explanation and the recommended follow-up).
- [x] Fast compile check passes (both `GTE_ENABLE_EDITOR=ON` and `=OFF`).
- [x] `PHASE4_COMPLETION_REPORT.md` written; `git add`/`git commit` follow
      next.

## Notes for PHASE5 / PHASE6

- `Game::EnsureDefaultCameraExists()` is now `public` (was `private` before
  this phase) — a genuine, small, deliberate API-surface widening. Anything
  in Phase 5's network endpoint work that also wants a guaranteed live
  Camera immediately after a `POST /load_scene` call can now call it
  directly, the same way `Editor/SceneIO.cpp`'s `LoadScene()` itself now
  does.
- The live, running-engine manual/smoke test this phase's own Definition of
  Done calls for (see above) is still outstanding — worth doing once a real
  multi-part `*.gta` mesh asset is available in this environment, ideally
  before Phase 6's own full regression pass so any real end-to-end
  reconciliation bug is caught before that phase's final sign-off.
- No discrepancy found against `PHASE5_NETWORK_SAVE_LOAD_SCENE_ENDPOINTS.md`
  or `PHASE6_TESTS_DOCS_CLEANUP_AND_FULL_REGRESSION.md` themselves — neither
  was edited: per this campaign's own workflow rule, a later phase's file is
  never edited from an earlier phase even when a discrepancy is found; the
  one discrepancy found this phase (`EnsureDefaultCameraExists()`'s access
  specifier) was against THIS phase's own file and is documented above, with
  the actual code fixed rather than the strategy file itself.
