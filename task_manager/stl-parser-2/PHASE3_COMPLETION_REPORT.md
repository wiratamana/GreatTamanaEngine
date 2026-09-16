# PHASE3_COMPLETION_REPORT.md — Instantiate Mesh Asset Engine Command

**Phase:** `PHASE3_INSTANTIATE_MESH_ASSET_ENGINE_COMMAND.md`
**Campaign:** `stl-parser-2` (parent: `PHASE0_MASTER_STRATEGY.md`)
**Branch:** `feature/stl-parser-impl` (unchanged, as required)

## Summary

Implemented a new, bare-bones `Game::InstantiateMeshAssetFromGtaFile()` method
— an `Outcome`-returning wrapper around the already-existing, unchanged
`Game::CreateMeshEntityFromGtaFile()` — plumbed through as a fifth value
(`InstantiateMeshAsset`) on the already-existing `EngineCommandBridge`/
`EngineCommandKind`/`EngineCommandDispatch.cpp`. Every step of this phase
document's own "Step 3: The Plan" (3.1 through 3.6) was implemented exactly as
specified, re-verified against the real, current state of every file it
references before editing, per this task's own instructions. No HTTP route was
added — that remains `PHASE4`'s job, exactly as this phase's own Definition of
Done requires.

### Files changed

- `src/Game/EngineCommandResults.h` — added `InstantiateMeshAssetOutcome`
  (`success`/`errorMessage`/`entityIndex`/`entityGeneration`/`resolvedName`),
  placed right after `InstantiateLightOutcome`, with the doc comment copied
  verbatim from the phase document's own Section 3.1 code block.
- `src/Game/Game.h` — added the public
  `InstantiateMeshAssetOutcome InstantiateMeshAssetFromGtaFile(Renderer&, const std::string&)`
  declaration, placed right after `CreateMeshEntityFromGtaFile()`'s own
  declaration, doc comment copied verbatim from Section 3.2.
- `src/Game/Game.cpp` — implemented `InstantiateMeshAssetFromGtaFile()` right
  after `CreateMeshEntityFromGtaFile()`'s own definition, exactly per Section
  3.3's code block: one call to `CreateMeshEntityFromGtaFile()`, a
  `kInvalidEntity` failure branch, and — on success —
  `m_registry.TryGetComponent<Name>(root)` to read back `resolvedName`.
  Confirmed `Name::value` (`src/ECS/Components/Name.h`) is the exact field
  name before writing `name->value`, exactly as the phase document instructed;
  no adjustment was needed, it already matched. `ECS/Components/Name.h` is
  already `#include`d in `Game.cpp` (line 5), so no new include was needed.
- `src/Application/EngineCommandBridge.h`:
  - Added `InstantiateMeshAsset` as a fifth `EngineCommandKind` value, with the
    doc comment copied verbatim from Section 3.4.
  - Added the new `InstantiateMeshAssetCommand { std::string absoluteGtaPath; }`
    plain request payload struct, placed right after `DeleteEntityCommand`.
  - Added `InstantiateMeshAssetCommand instantiateMeshAsset;` to
    `EngineCommandRequest` and `InstantiateMeshAssetOutcome instantiateMeshAsset;`
    to `EngineCommandResult`, both right after their respective existing
    `instantiateLight` fields — confirmed `EngineCommandBridge.h` already
    `#include`s `"../Game/EngineCommandResults.h"` (line 27), so no new include
    was needed there either.
- `src/Application/EngineCommandDispatch.cpp` — added a new `case
  EngineCommandKind::InstantiateMeshAsset:` to `ExecuteEngineCommand()`'s
  `switch`, right after the existing `InstantiateLight` case, forwarding to
  `game.InstantiateMeshAssetFromGtaFile(renderer, request.instantiateMeshAsset.absoluteGtaPath)`
  exactly per Section 3.5's code block.

### Files added

None — per this phase document's own explicit Section 3.6 instruction (see
"Testability notes" below).

## Deviations from the strategy doc

None to the actual implementation plan. One transient self-inflicted mistake
was made and corrected during editing, documented here for transparency
(not a deviation from the strategy doc itself — a mechanical slip in applying
`edit_line`, caught and fixed before compiling):

- While inserting the new `InstantiateMeshAsset` `case` into
  `EngineCommandDispatch.cpp`, an `edit_line` call targeting the existing
  `InstantiateLight` case's own `break;` line accidentally replaced that
  `break;` with the new case's opening content instead of preserving it —
  this would have made the `InstantiateLight` case fall through into the new
  `InstantiateMeshAsset` case (executing `game.InstantiateMeshAssetFromGtaFile()`
  unconditionally after every `InstantiateLight` dispatch — a real correctness
  bug). Caught immediately by re-reading the file after the edit (per this
  task's own "always re-verify against what's actually on disk" instruction),
  and fixed with one more `edit_line` call restoring the missing `break;`
  before compiling anything. The final, compiled file (reproduced below)
  has `InstantiateLight`'s own `break;` fully intact, and was confirmed via
  the passing `EngineCommandBridgeTest.FulfilledInstantiateLightRequestReturnsExactResultQuickly`/
  `EngineCommandEndpointsEndToEndTest.InstantiateLightValidPayloadReturns200AndSuccess`/
  `FullInstantiateLightThenSetTrsThenDeleteRoundTripAllReturn200` tests (all
  still pass — see "Build & test results" below).

Every element of this phase document's own Step 3 (3.1 through 3.6) was
otherwise implemented exactly as specified: the exact `InstantiateMeshAssetOutcome`
shape, the exact `InstantiateMeshAssetFromGtaFile()` wrapper body (one call to
the existing, unchanged `CreateMeshEntityFromGtaFile()`, no other
spawn-affecting logic), the exact fifth `EngineCommandKind` value/request-payload
struct/request-result field additions, and the exact dispatch `case`.

## Testability notes (per this phase document's own Section 3.6)

Per this phase document's own explicit instruction, **no new test file was
created**, mirroring `PHASE1`'s established precedent for
`NullEditorLayer::ImportExternalAssetIntoProject()` (an accepted gap, verified
by inspection, not filled with a contrived test):

- `Game::CreateMeshEntityFromGtaFile()` (the method this phase's new wrapper
  forwards to) has NO automated test coverage anywhere today — confirmed by
  searching `tests/` for its name, the only hit being an unrelated explanatory
  comment in `tests/Game/GameLoopPhysicsWithoutAnimationTests.cpp`. This
  phase's new `InstantiateMeshAssetFromGtaFile()` inherits the exact same gap.
- The `kInvalidEntity`/missing-file failure branch cannot be isolated as a
  Tier-1, `Renderer`-free test: both `CreateMeshEntityFromGtaFile()` and this
  phase's new wrapper take `Renderer&` (a reference, never a nullable
  pointer), and `Renderer`'s constructor (`explicit Renderer(Window&)`,
  `src/Renderer/Renderer.h`) unconditionally stands up a real SDL window and a
  real Vulkan instance/device/swapchain — there is no way to obtain a
  `Renderer&` at the call site at all without a live windowing/GPU
  environment, regardless of how early the callee itself would otherwise
  bail out. Confirmed a hard, permanent Tier-2 wall, exactly as the phase
  document's own analysis states.
- No `EngineCommandDispatchTests.cpp` (or similarly named dispatch-level test
  file) exists anywhere in this codebase today — confirmed by browsing
  `tests/Application/` (only `EngineCommandBridgeTests.cpp` exists there,
  testing the bridge's own mutex/timeout plumbing, never
  `ExecuteEngineCommand()`'s `switch` itself) and by searching all of `tests/`
  for `ExecuteEngineCommand` (zero hits). `ExecuteEngineCommand()` takes a
  live `Renderer&` unconditionally for every one of its five cases now, so
  this phase's new `InstantiateMeshAsset` case inherits the exact same
  un-tested, accepted Tier-2 gap every other case already has — not a new or
  worse gap than what already existed.

No genuinely new, Tier-1-testable pure-logic seam was found during this
phase's implementation (per the phase document's own "if... a genuinely new
pure-logic seam is found" contingency) — the wrapper is a two-branch, mostly
mechanical function with no independently-extractable logic of its own beyond
what's already covered by the analysis above.

## Build & test results (this machine)

### Targeted build

```
cmake --build build --target GreatTamanaEngineTests
cmake --build build --target GreatTamanaEngine
```

Both targets built successfully with no new warnings from
`EngineCommandResults.h`, `Game.h`/`.cpp`, `EngineCommandBridge.h`, or
`EngineCommandDispatch.cpp`. `build/CMakeCache.txt` was confirmed to have both
`GTE_ENABLE_EDITOR=ON` and `GTE_ENABLE_PROJECT_PANEL=ON` before starting,
matching the previous phases' own build configuration.

### Targeted test run (existing tests exercising the touched files)

Since this phase deliberately adds no new test file, "the new tests" for this
phase are the pre-existing suites that already exercise the exact files this
phase changed (`EngineCommandBridge.h`, `EngineCommandDispatch.cpp`,
`Game.h`/`.cpp`'s other public methods) — run to confirm the fifth
`EngineCommandKind` value/struct field additions introduced zero regression to
any of the four pre-existing kinds:

```
--gtest_filter=EngineCommandBridgeTest.*:EngineCommandEndpoints*:GameEntityCommandsTests.*:GameUpdateFreezeGatingTests.*

[==========] Running 23 tests from 3 test suites.
...
[==========] 23 tests from 3 test suites ran. (2826 ms total)
[  PASSED  ] 23 tests.
```

### Adjacent regression spot-check (broader Game/Network/Application suites)

Since `EngineCommandBridge.h`'s `EngineCommandRequest`/`EngineCommandResult`
struct LAYOUTS changed (new fields added to both) and
`EngineCommandDispatch.cpp`'s `switch` gained a new case immediately after an
existing one whose own `break;` was briefly, accidentally disturbed mid-edit
(see "Deviations" above), a broader spot-check beyond just the directly-named
new tests was run for extra confidence (a full `ctest` run is still deferred
to `PHASE5`, per the top-level workflow rules):

```
--gtest_filter=*EngineCommand*:*Network*:*Game*

[==========] Running 80 tests from 15 test suites.
...
[==========] 80 tests from 15 test suites ran. (5516 ms total)
[  PASSED  ] 80 tests.
```

Zero regressions. In particular, every `EngineCommandEndpointsEndToEndTest`
case for `InstantiatePrimitive`/`DeleteEntity`/`SetEntityTrs`/`InstantiateLight`
(including the `FullInstantiateLightThenSetTrsThenDeleteRoundTripAllReturn200`
round trip) still passes unchanged, confirming the new fifth `EngineCommandKind`
value did not disturb any of the four pre-existing ones.

No full `ctest` regression run was performed in this phase (per the top-level
workflow rules: PHASE1–PHASE4 only get a targeted/filtered test run; the full
suite is `PHASE5`'s own job).

## Definition-of-done checklist (this phase's slice)

- [x] `InstantiateMeshAssetOutcome` exists in `EngineCommandResults.h`.
- [x] `Game::InstantiateMeshAssetFromGtaFile()` exists, is a pure additive
  wrapper (verified by inspection: it calls `CreateMeshEntityFromGtaFile()`
  exactly once, with no other spawn-affecting logic of its own).
- [x] `EngineCommandKind::InstantiateMeshAsset` is a real, dispatched fifth
  value on the EXISTING `EngineCommandBridge` — not a new bridge.
- [x] `cmake --build build` succeeds for both `GreatTamanaEngineTests` and
  `GreatTamanaEngine`; a targeted/filtered test run (23) plus a broader
  adjacent regression spot-check (80) both pass with zero regressions. A full
  `ctest` run is deferred to `PHASE5` per the top-level workflow rules. An
  honest note on which Tier-2 gaps could not be covered (and why) is included
  above, per this phase document's own explicit instruction to not invent a
  contrived test.
- [x] No `POST /instantiate_asset` HTTP route exists yet — confirmed: this
  phase touched only `EngineCommandResults.h`, `Game.h`/`.cpp`,
  `EngineCommandBridge.h`, and `EngineCommandDispatch.cpp`; `NetworkServer.cpp`/
  `NetworkRoutes.h`/`.cpp` were not touched at all, exactly as required
  (`PHASE4`'s own job).

## Git

Source changes plus this report are committed together in one commit on
`feature/stl-parser-impl` (branch unchanged, per the workflow rules).
