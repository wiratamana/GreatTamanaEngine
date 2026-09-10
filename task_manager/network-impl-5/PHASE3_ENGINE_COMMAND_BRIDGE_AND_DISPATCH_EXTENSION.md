# PHASE3 — `EngineCommandBridge`/`EngineCommandDispatch` Extension

Parent: `PHASE0_MASTER_STRATEGY.md`. Depends on: Phase 2's
`SetEntityTrsParams`/`SetEntityTrsOutcome`/`InstantiateLightParams`/
`InstantiateLightOutcome` structs and `Game::SetEntityTrs()`/
`Game::InstantiateLight()` methods. Does **not** depend on Phase 1 (no
JSON/httplib anywhere in this phase — the extended bridge is fully
exercisable and testable with a plain, direct C++ call, exactly like it
already is for the two existing kinds).

## Step 1 — The Goal

Extend the ALREADY-EXISTING `EngineCommandBridge`/`EngineCommandDispatch`
machinery (`src/Application/EngineCommandBridge.h/.cpp`,
`src/Application/EngineCommandDispatch.h/.cpp` — built by `network-impl-3`,
untouched by `network-impl-4`) with two more `EngineCommandKind` values,
without changing the bridge's own thread-safety mechanics at all. Confirm
`Application.h`/`Application.cpp` need ZERO changes (already true today —
see `PHASE0_MASTER_STRATEGY.md`'s Step 2).

## Step 2 — The Situation

- `EngineCommandBridge.h`'s `EngineCommandKind` enum, `EngineCommandRequest`/
  `EngineCommandResult` tagged structs, and the `EngineCommandBridge` class
  itself are exactly as `network-impl-3` left them (see
  `PHASE0_MASTER_STRATEGY.md`'s Step 2 for the full recap) — read the
  ACTUAL current file before editing, it is reproduced in full in this
  campaign's own `PHASE0_MASTER_STRATEGY.md` investigation for convenience,
  but the real file is the source of truth.
- `EngineCommandBridge.cpp`'s implementation (`SubmitAndWait()`/
  `IsCommandPending()`/`TryPeekPendingCommandRequest()`/`FulfillCommand()`)
  never branches on `EngineCommandKind` at all — it only ever moves whole
  `EngineCommandRequest`/`EngineCommandResult` VALUES around under a mutex.
  **This phase makes ZERO changes to `EngineCommandBridge.cpp`.** Only
  `EngineCommandBridge.h`'s two struct definitions change.
- `EngineCommandDispatch.cpp`'s `ExecuteEngineCommand()` is the ONE place
  that actually `switch (request.kind)`es — this is the ONLY function this
  phase adds new logic to.
- `EngineCommandBridge.h` already `#include`s `"../Game/EngineCommandResults.h"`
  (for `InstantiatePrimitiveOutcome`/`DeleteEntityOutcome`) — Phase 2's new
  `SetEntityTrsParams`/`SetEntityTrsOutcome`/`InstantiateLightParams`/
  `InstantiateLightOutcome` structs are ALREADY visible here with no new
  `#include` needed, since they live in that same header.
- Per Phase 2's own Step 3.1 rationale, `EngineCommandRequest`/
  `EngineCommandResult` embed Phase 2's `SetEntityTrsParams`/
  `InstantiateLightParams`/`SetEntityTrsOutcome`/`InstantiateLightOutcome`
  structs DIRECTLY as their own sibling fields — there is deliberately NO
  separate `SetEntityTrsCommand`/`InstantiateLightCommand` struct
  re-declared in `EngineCommandBridge.h` the way `InstantiatePrimitiveCommand`/
  `DeleteEntityCommand` already exist there today (those two OLDER structs
  are left completely untouched by this phase — this is a one-time,
  forward-only simplification for the two NEW kinds only, not a retroactive
  cleanup of the two existing ones).

## Step 3 — The Plan

### 3.1 — Extend `EngineCommandKind` in `src/Application/EngineCommandBridge.h`

```cpp
enum class EngineCommandKind {
    InstantiatePrimitive,
    DeleteEntity,
    // network-impl-5 campaign
    // (PHASE3_ENGINE_COMMAND_BRIDGE_AND_DISPATCH_EXTENSION.md) - two more
    // engine commands, sharing this SAME single-global-slot bridge (see
    // PHASE0_MASTER_STRATEGY.md's Locked Design Decision #8 - unchanged
    // from network-impl-3's own original Locked Design Decision #5).
    SetEntityTrs,
    InstantiateLight,
};
```

### 3.2 — Extend `EngineCommandRequest`/`EngineCommandResult`

```cpp
struct EngineCommandRequest {
    EngineCommandKind kind = EngineCommandKind::InstantiatePrimitive;
    InstantiatePrimitiveCommand instantiatePrimitive;
    DeleteEntityCommand deleteEntity;
    // network-impl-5 campaign - reuses Game's OWN request-parameter structs
    // directly (src/Game/EngineCommandResults.h, Phase 2) rather than
    // re-declaring an identical shape as a THIRD/FOURTH "Command" struct
    // here - see this phase document's own Step 2 note on why this
    // diverges, deliberately, from the two OLDER fields immediately above.
    SetEntityTrsParams setEntityTrs;
    InstantiateLightParams instantiateLight;
};

struct EngineCommandResult {
    EngineCommandKind kind = EngineCommandKind::InstantiatePrimitive;
    InstantiatePrimitiveOutcome instantiatePrimitive;
    DeleteEntityOutcome deleteEntity;
    // network-impl-5 campaign
    SetEntityTrsOutcome setEntityTrs;
    InstantiateLightOutcome instantiateLight;
};
```

Exactly one of the four sibling fields on each struct is meaningful at a
time, selected by `kind` — same documented convention the existing two
fields already establish (see each struct's own existing doc comment,
unchanged by this phase — just make sure the comment's own field list
mentions all four kinds once this edit lands, so it doesn't go stale).

### 3.3 — Extend `ExecuteEngineCommand()` in `src/Application/EngineCommandDispatch.cpp`

```cpp
EngineCommandResult ExecuteEngineCommand(Game& game, Renderer& renderer, const EngineCommandRequest& request)
{
    EngineCommandResult result;
    result.kind = request.kind;
    switch (request.kind) {
    case EngineCommandKind::InstantiatePrimitive: {
        const InstantiatePrimitiveCommand& cmd = request.instantiatePrimitive;
        result.instantiatePrimitive = game.InstantiatePrimitive(
            renderer, cmd.shape, cmd.requestedName, cmd.worldPosition, cmd.hasParent, cmd.parentName);
        break;
    }
    case EngineCommandKind::DeleteEntity: {
        result.deleteEntity = game.DeleteEntityByName(request.deleteEntity.name);
        break;
    }
    // network-impl-5 campaign - neither of these two touches `renderer` at
    // all (see PHASE2's own Testability note) - `renderer` stays
    // unreferenced in these two branches, exactly mirroring
    // DeleteEntity's own branch immediately above, which also never
    // touches it.
    case EngineCommandKind::SetEntityTrs: {
        result.setEntityTrs = game.SetEntityTrs(request.setEntityTrs);
        break;
    }
    case EngineCommandKind::InstantiateLight: {
        result.instantiateLight = game.InstantiateLight(request.instantiateLight);
        break;
    }
    }
    return result;
}
```

Never throws — `Game::SetEntityTrs()`/`InstantiateLight()` (Phase 2) already
degrade every failure mode into a plain `success == false` outcome, so this
function still has nothing further to catch, exactly like the existing two
branches.

### 3.4 — Confirm `Application.h`/`Application.cpp` need no changes

Re-read `Application::Run()`'s existing per-frame drain (already quoted in
`PHASE0_MASTER_STRATEGY.md`'s Step 2) and confirm it compiles and behaves
correctly completely unmodified — it already calls
`ExecuteEngineCommand(m_game, m_renderer, *request)` generically, and
`m_commandBridge`/`m_networkServer`'s construction/wiring
(`Application.h`'s member list, `Application.cpp`'s constructor) has no
per-`EngineCommandKind` logic anywhere in it. **Do not edit
`Application.h`/`Application.cpp` in this phase** — if the build fails here
after Steps 3.1–3.3, that is a signal one of those steps introduced an
actual compile error elsewhere (e.g. a missing `#include`), not that
`Application` itself needs a code change.

## Verification for this phase

- Fast compile check.
- Extend `tests/Application/EngineCommandBridgeTests.cpp` (existing file)
  with at least one round-trip test per NEW kind, mirroring
  `FulfilledRequestReturnsExactResultQuickly`'s own exact shape: build a
  `SetEntityTrs`-kind (and separately an `InstantiateLight`-kind)
  `EngineCommandRequest`, submit it from a background thread, have the main
  test thread `TryPeekPendingCommandRequest()`/`FulfillCommand()` it with a
  hand-built `EngineCommandResult`, and assert the exact request/result data
  round-trips through the bridge unchanged. This is a cheap, valuable
  regression proof that the bridge's existing generic mutex/condition-
  variable mechanics genuinely didn't need to change for a third/fourth kind
  — it directly demonstrates `PHASE0_MASTER_STRATEGY.md`'s own Step 2 claim
  about this.
- No new test file is needed for `EngineCommandDispatch.cpp` itself if none
  already exists for the two existing kinds (check first — if
  `ExecuteEngineCommand()` has no dedicated unit test today because it's
  trivial one-line-per-kind forwarding covered adequately by the end-to-end
  tests instead, it's acceptable to keep deferring that same treatment to
  Phase 5's end-to-end tests for the two new kinds too, for consistency).
- Write `PHASE3_COMPLETION_REPORT.md`, `git add`/`git commit`.
