# PHASE4 — COMPLETION REPORT: `EngineCommandBridge` (Cross-Thread) + `Application::Run()` Wiring

Parent: `PHASE0_MASTER_STRATEGY.md`
Phase document: `PHASE4_ENGINE_COMMAND_BRIDGE_AND_MAIN_LOOP_INTEGRATION.md`
Branch: `feature/network-impl`

## Summary

Implemented Phase 4 of the `network-impl-3` campaign exactly as specified:
a new `src/Application/EngineCommandBridge.h/.cpp` (the cross-thread bridge,
structurally mirroring `FrameCaptureBridge` but with a single global slot and
a real request payload + mutation outcome), a new
`src/Application/EngineCommandDispatch.h/.cpp` (the main-thread-only
dispatcher unpacking an `EngineCommandRequest` into calls to Phase 3's
`Game::InstantiatePrimitive()`/`DeleteEntityByName()`), and the
`Application.h/.cpp` + `NetworkServer.h/.cpp` wiring needed to construct/reach
it. Built directly on top of Phase 3's `Game::` methods and
`EngineCommandResults.h` — neither needed any changes. No JSON/httplib
dependency exists anywhere in this phase's own new code — the whole bridge
round-trip is exercisable and tested with plain, direct C++ calls, no real
HTTP request involved at all.

## What was done

### 1. `src/Application/EngineCommandBridge.h` / `.cpp` (new files)

Implemented exactly per the phase document's own reference design:

- `EngineCommandKind` (`InstantiatePrimitive`/`DeleteEntity`), plain payload
  structs `InstantiatePrimitiveCommand`/`DeleteEntityCommand`, and the tagged
  `EngineCommandRequest`/`EngineCommandResult` wrapper structs (a plain tag +
  sibling fields, matching the existing `FrameCaptureKind`/`CapturedPngImage`
  precedent — no `std::variant` needed for just two kinds).
- `EngineCommandBridge`: a **single global slot** (per PHASE0's Locked Design
  Decision #5 — unlike `FrameCaptureBridge`'s two independent per-kind
  slots), with `SubmitAndWait()` (network thread, blocking with timeout),
  `IsCommandPending()`/`TryPeekPendingCommandRequest()`/`FulfillCommand()`
  (main thread). `TryPeekPendingCommandRequest()` collapses the "is one
  pending" check and the "copy it out" step into ONE atomically-locked
  operation (the phase document's own "second-iteration fix" — avoiding the
  check-then-use race a separate `IsCommandPending()` + a second, separately
  locked peek call would have introduced).
- A late `FulfillCommand()` call arriving after a request already timed out
  is a safe, silent no-op (mirrors `FrameCaptureBridge::FulfillPendingRequest()`).

### 2. `src/Application/EngineCommandDispatch.h` / `.cpp` (new files)

`ExecuteEngineCommand(Game&, Renderer&, const EngineCommandRequest&)` — the
one, small, MAIN-THREAD-ONLY function that switches on `request.kind` and
calls straight into `Game::InstantiatePrimitive()`/`Game::DeleteEntityByName()`
(Phase 3), returning a fully-populated `EngineCommandResult`. Kept as its own
file, mirroring `RenderPasses.h/.cpp`'s existing precedent for extracting
`Run()`-helper free functions out of `Application.cpp` itself.

### 3. `CMakeLists.txt` (root) — new files registered

Added all four new files (`EngineCommandBridge.h/.cpp`,
`EngineCommandDispatch.h/.cpp`) to `gte_core`'s explicit
`target_sources(... PRIVATE ...)` list, immediately after the existing
`src/Application/FrameCaptureBridge.cpp` line — this project does not glob
sources (confirmed by Phase 2/3's own prior findings), so this step was
mandatory for the new symbols to link at all.

### 4. `src/Application/Application.h` (extended)

- `#include "EngineCommandBridge.h"` added alongside the existing
  `#include "FrameCaptureBridge.h"`.
- New member `EngineCommandBridge m_commandBridge;` declared immediately
  after the existing `FrameCaptureBridge m_captureBridge;` — BEFORE
  `Network::NetworkServer m_networkServer;` (same "constructed first,
  destroyed last relative to it" ordering rule `m_captureBridge` already
  follows), so its address can be handed into `m_networkServer`'s own
  constructor.

### 5. `src/Network/NetworkServer.h` / `.cpp` (extended)

- Forward-declared `namespace gte { class EngineCommandBridge; }`, mirroring
  the existing `FrameCaptureBridge` forward declaration.
- Constructor gained a SECOND defaulted, non-owning pointer parameter:
  `NetworkServer(FrameCaptureBridge* captureBridge = nullptr, EngineCommandBridge* commandBridge = nullptr)`
  — append-only, so every existing `tests/Network/NetworkServerTests.cpp`
  no-argument `NetworkServer server;` call site keeps compiling unchanged.
- New private member `EngineCommandBridge* m_commandBridge = nullptr;`
  (same non-owning shape as `m_captureBridge`).
- `RegisterRoutes()` gained the same second parameter, threaded through from
  the constructor exactly like `captureBridge` already is — deliberately NOT
  yet used to register any route this phase (a `(void)commandBridge;`
  placeholder plus an explanatory comment marks this as intentional, pointing
  at Phase 5 as the actual consumer).

### 6. `src/Application/Application.cpp` (extended)

- `#include "EngineCommandDispatch.h"` added.
- Constructor's member-initializer list: `m_networkServer(&m_captureBridge, &m_commandBridge)`
  (previously `m_networkServer(&m_captureBridge)`), with a doc comment
  explaining the same lifetime-ordering guarantee as the existing capture
  bridge comment.
- `Run()`: inserted the per-frame drain immediately after the SDL
  event-polling `{ ... }` scope's closing `}` and BEFORE
  `const Uint64 nowTicksNs = SDL_GetTicksNS();` — exactly the insertion point
  PHASE0's Locked Design Decision #6 calls for (as EARLY as possible, right
  after input polling, before `Game::Update()`/Physics/Animation run that
  frame):

  ```cpp
  if (const std::optional<EngineCommandRequest> request = m_commandBridge.TryPeekPendingCommandRequest()) {
      GTE_PROFILE_SCOPE("Application::ExecuteEngineCommand");
      const EngineCommandResult result = ExecuteEngineCommand(m_game, m_renderer, *request);
      m_commandBridge.FulfillCommand(result);
  }
  ```

  Drains at most ONE pending command per frame, per the phase document's own
  design — matches the reference implementation verbatim.

### 7. Tests — `tests/Application/EngineCommandBridgeTests.cpp` (new file)

Since `tests/Application/FrameCaptureBridgeTests.cpp` already existed, its
exact structure/spawn-a-thread-and-call pattern was mirrored for the new
bridge. Five tests, covering every contract enumerated in the phase
document's own "Verification for this phase" section:

- `RequestWithNoFulfillerTimesOut` — times out within the requested window.
- `FulfilledRequestReturnsExactResultQuickly` — a background thread polling
  `IsCommandPending()`/peeking the request/calling `FulfillCommand()` wakes
  the waiter well before a much longer timeout would have elapsed.
- `SecondConcurrentRequestReturnsAlreadyPendingImmediately` — a SECOND
  `SubmitAndWait()` call (deliberately of a DIFFERENT `EngineCommandKind`
  than the first, to prove the single-GLOBAL-slot design from PHASE0's
  Locked Design Decision #5 — not merely a per-kind collision) returns
  `alreadyPending == true` immediately, never actually waiting.
- `PendingStateIsObservableAndClearsAfterFulfillment` — `IsCommandPending()`/
  `TryPeekPendingCommandRequest()` correctly reflect pending state and go
  back to "nothing pending" once fulfilled.
- `LateFulfillmentAfterTimeoutIsInertAndDoesNotCorruptNextRequest` — a late
  `FulfillCommand()` call arriving after a timeout is a safe no-op that does
  not corrupt the next, fresh request's own result.

Added `Application/EngineCommandBridgeTests.cpp` to `tests/CMakeLists.txt`'s
always-built source list, immediately after the existing
`Application/FrameCaptureBridgeTests.cpp` line.

## Verification performed

1. **Fast compile check** (`cmake --build build --config Debug --target
   GreatTamanaEngineTests`, from the repo root): full build succeeded with
   zero errors/warnings related to this change — `gte_core` (including the
   two new `.cpp` files, `Application.cpp`, and `NetworkServer.cpp`) and
   `GreatTamanaEngineTests.exe` relinked successfully. No other translation
   unit broke.
2. **Targeted test run** (`ctest -C Debug -R "EngineCommandBridge"
   --output-on-failure`): **5/5 tests passed**.
3. **Full regression pass** (`ctest -C Debug --output-on-failure`, from
   `build/`): **1115/1115 tests passed** (1 pre-existing, machine-gated
   smoke test — `PmxLoaderRealModelSmokeTest.LoadsAnMmdModelIfPresentOnThisMachine`
   — skipped, same as every prior session in this repository). Zero
   regressions anywhere else in the suite, confirming `Application`'s
   constructor member-initializer-list reordering and `Run()`'s new main-loop
   body did not disturb any existing behavior.

No full engine build (`cmake --build build` targeting the `GreatTamanaEngine`
executable itself) and no `run_app_background` runtime sanity check were
performed — per this task's own workflow rules ("No Full Build: Do not run a
full build ... unless the Current task explicitly says to do full build.
Usually on the last one"), only the fast compile check (test target) plus the
full `ctest` pass above were run this phase. The `GreatTamanaEngine.exe`
runtime smoke check the phase document itself suggests as an extra, optional
sanity step is deferred to a later phase (e.g. Phase 6, which explicitly
calls for a full build/regression pass) rather than performed here.

## Deviations from the phase document

None. Every locked decision, file, struct shape, method signature, and
implementation body was implemented exactly as specified — including the
"second-iteration fix" collapsing `IsCommandPending()` + a separate peek into
one atomically-locked `TryPeekPendingCommandRequest()` call, and the
`(void)commandBridge;` placeholder in `NetworkServer.cpp`'s `RegisterRoutes()`
(this codebase's root `CMakeLists.txt` does not enable `-Wall`/`-Werror` for
`gte_core` itself, but the placeholder was added anyway per the phase
document's own explicit instruction, to keep the parameter's future Phase 5
usage obviously flagged in code).

## Next phase

`PHASE5_NETWORK_POST_ROUTES_AND_COMMAND_DISPATCH.md` — `NetworkServer` gains
real `httplib::Server::Post(...)` support; the two new routes
(`POST /instantiate_primitive`, `POST /delete_entity`) are registered, wired
through Phase 1's JSON parsers (`src/Network/NetworkRoutes.h`) and this
phase's `EngineCommandBridge::SubmitAndWait()` — the campaign's actual
user-facing HTTP surface. Nothing in this phase's own new code needs any
changes for Phase 5 to build on top of it.
