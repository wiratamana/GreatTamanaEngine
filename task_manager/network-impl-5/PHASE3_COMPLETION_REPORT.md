# PHASE3 — COMPLETION REPORT: `EngineCommandBridge`/`EngineCommandDispatch` Extension

Status: **DONE.** Both new `EngineCommandKind` values (`SetEntityTrs`,
`InstantiateLight`) and their corresponding `ExecuteEngineCommand()` `switch`
branches were added exactly as specified in
`PHASE3_ENGINE_COMMAND_BRIDGE_AND_DISPATCH_EXTENSION.md`. Zero new files were
created, and `Application.h`/`Application.cpp` were confirmed to need — and
received — no changes at all, matching `PHASE0_MASTER_STRATEGY.md`'s own Step
2 claim.

## What was implemented

### `src/Application/EngineCommandBridge.h`

- Extended `EngineCommandKind` with two new enumerators, `SetEntityTrs` and
  `InstantiateLight`, with the exact doc comment from the phase document's
  Step 3.1.
- Extended `EngineCommandRequest` with two new sibling fields,
  `SetEntityTrsParams setEntityTrs` and `InstantiateLightParams
  instantiateLight` — reusing Phase 2's own structs (`src/Game/
  EngineCommandResults.h`) DIRECTLY, exactly as the phase document's Step 3.2
  specifies, rather than re-declaring a third/fourth "Command" struct the way
  `InstantiatePrimitiveCommand`/`DeleteEntityCommand` already exist. No new
  `#include` was needed — `EngineCommandBridge.h` already `#include`s
  `"../Game/EngineCommandResults.h"` (for the two pre-existing Outcome
  structs), which already declares `SetEntityTrsParams`/`InstantiateLightParams`.
- Extended `EngineCommandResult` symmetrically with `SetEntityTrsOutcome
  setEntityTrs` and `InstantiateLightOutcome instantiateLight`.
- Updated the doc comment directly above `EngineCommandRequest` to mention all
  four fields (previously only referenced the original two), so it doesn't go
  stale, per the phase document's own note in Step 3.2.
- **Zero changes to any method body** in this file (`SubmitAndWait()`,
  `IsCommandPending()`, `TryPeekPendingCommandRequest()`, `FulfillCommand()`
  are all declared, unmodified) — confirming the phase document's own claim
  that the bridge's mutex/condition-variable mechanics are already fully
  kind-agnostic.

### `src/Application/EngineCommandDispatch.cpp`

- Added two new `switch (request.kind)` cases in `ExecuteEngineCommand()`:
  `EngineCommandKind::SetEntityTrs` calls `game.SetEntityTrs(request.setEntityTrs)`,
  and `EngineCommandKind::InstantiateLight` calls
  `game.InstantiateLight(request.instantiateLight)` — both exactly matching
  the phase document's Step 3.3 reference implementation, including the
  explanatory comment noting neither branch touches `renderer` (mirroring the
  pre-existing `DeleteEntity` branch, which also never touches it). No new
  `#include` was needed (`Game.h` was already included).

### `src/Application/Application.h` / `Application.cpp`

- **Untouched**, as required by the phase document's Step 3.4 and confirmed by
  `git status` showing no changes to either file. `Application::Run()`'s
  existing generic per-frame drain (`TryPeekPendingCommandRequest()` ->
  `ExecuteEngineCommand()` -> `FulfillCommand()`) already dispatches on
  `request.kind` generically inside `ExecuteEngineCommand()`, so it needed no
  edit to pick up the two new kinds.

### `tests/Application/EngineCommandBridgeTests.cpp` (existing file, extended)

Added two new round-trip tests, mirroring
`FulfilledRequestReturnsExactResultQuickly`'s own exact shape, per the phase
document's "Verification for this phase" section:

- `FulfilledSetEntityTrsRequestReturnsExactResultQuickly` — builds a
  `SetEntityTrs`-kind `EngineCommandRequest` (a translation-only edit),
  submits it from a background thread, has the main test thread
  `TryPeekPendingCommandRequest()`/`FulfillCommand()` it with a hand-built
  `EngineCommandResult`, and asserts the exact request/result data
  (`name`/`hasTranslation`/`success`/`translationChanged`/`rotationChanged`/
  `resultingPosition`) round-trips through the bridge unchanged.
- `FulfilledInstantiateLightRequestReturnsExactResultQuickly` — same shape for
  an `InstantiateLight`-kind request/result
  (`lightType`/`requestedName`/`worldPosition`/`success`/`resolvedName`).

Both tests are a direct, concrete regression proof of the phase document's own
central claim: the existing generic mutex/condition-variable mechanics
genuinely didn't need to change for a third/fourth `EngineCommandKind`. No
dedicated unit test file was added for `EngineCommandDispatch.cpp` itself —
per the phase document's own guidance, no such file existed for the two
pre-existing kinds either (that trivial one-line-per-kind forwarding is
covered adequately by `tests/Network/EngineCommandEndpointsEndToEndTests.cpp`,
consistent with keeping the same treatment for the two new kinds, deferred to
Phase 5's end-to-end tests once Phase 4 wires the actual HTTP routes).

## Deviations from the strategy document

None. Every enum value, struct field, doc comment, and reference
implementation was followed exactly as specified in
`PHASE3_ENGINE_COMMAND_BRIDGE_AND_DISPATCH_EXTENSION.md`. `edit_line`'s
auto-dedup safety net fired once while splicing the two new `switch` cases
into `EngineCommandDispatch.cpp` (it detected and removed one leftover
duplicate closing `}` boundary line) — verified by re-reading the resulting
file immediately afterward; the correct, single closing brace for the
`switch` statement is present, confirmed both by direct inspection and by the
file compiling/linking/running cleanly. Not a content-loss issue, noted here
purely for transparency, consistent with Phase 2's own completion report
calling out the same tooling behavior.

## Verification performed

1. **Fast, targeted compile check** (not a full rebuild), per this phase's own
   workflow rules:
   - `cmake --build build --target CMakeFiles/gte_core.dir/src/Application/EngineCommandDispatch.cpp.obj`
     — compiled cleanly, zero warnings/errors.
   - `cmake --build build --target CMakeFiles/gte_core.dir/src/Application/EngineCommandBridge.cpp.obj`
     — compiled cleanly (re-verifies `EngineCommandBridge.h`'s own extended
     structs compile correctly from the `.cpp` that actually implements the
     bridge's logic).
   - `cmake --build build --target CMakeFiles/gte_core.dir/src/Application/Application.cpp.obj`
     — compiled cleanly, confirming `Application.cpp` (which transitively
     includes the now-extended `EngineCommandBridge.h`) needed no source
     changes at all, exactly as the phase document predicted.
   - `cmake --build build --target tests/CMakeFiles/GreatTamanaEngineTests.dir/Application/EngineCommandBridgeTests.cpp.obj`
     — compiled cleanly, zero warnings/errors.
2. A full link of `GreatTamanaEngineTests` was then performed (cheap here,
   consistent with Phases 1/2's own verification approach, since nothing else
   needed rebuilding beyond a couple of transitively-dependent translation
   units — `NetworkServer.cpp` and the pre-existing end-to-end test file also
   recompiled and linked cleanly).
3. Ran the new tests in isolation:
   `tests\GreatTamanaEngineTests.exe --gtest_filter=EngineCommandBridgeTest.*`
   — **7 tests, all passed** (5 pre-existing + 2 new).
4. Ran the **full** test suite: **1240 tests total, 1239 passed**, 1
   pre-existing, documented machine-gated smoke test skipped
   (`PmxLoaderRealModelSmokeTest.LoadsAnMmdModelIfPresentOnThisMachine`) —
   zero failures, zero regressions from this phase's changes. (Per this
   phase's own workflow rules, this is a targeted-compile-check confirmation
   only, not the full clean rebuild/regression pass that is explicitly
   PHASE5's own job.)
5. Confirmed via `git status` that only the three files listed above were
   modified — no new files were created anywhere, and
   `src/Application/Application.h`/`Application.cpp` show no changes.

## What's next

Phase 4 (`PHASE4_NETWORK_POST_ROUTES_WIRING.md`) can now register the two new
`server.Post(...)` routes in `NetworkServer.cpp`'s `RegisterRoutes()`, wiring
Phase 1's JSON parsers/response builders together with this phase's
`EngineCommandBridge::SubmitAndWait()` extension — the campaign's actual
user-facing HTTP surface. No open questions or blockers were found.
