# PHASE4 — `EngineCommandBridge` (Cross-Thread) + `Application::Run()` Wiring

Parent: `PHASE0_MASTER_STRATEGY.md`. Depends on: Phase 3's `Game::InstantiatePrimitive()`/
`DeleteEntityByName()` + `EngineCommandResults.h`. Does **not** depend on
Phase 1 (no JSON/httplib anywhere in this phase — this bridge is fully
exercisable and testable with a plain, direct C++ call, no real HTTP request
involved at all).

## Step 1 — The Goal

Build the ONE reviewed, thread-safe bridge a future `NetworkServer` route
handler (Phase 5) will be allowed to touch to make Phase 3's Game-mutating
methods actually run on the main thread — mirroring `FrameCaptureBridge`'s
proven shape, generalized for a CALLER-SUPPLIED PAYLOAD (unlike a capture
request, which carries no data beyond "which kind") and a MUTATION OUTCOME
(success/failure + message, unlike a capture's plain image bytes). Wire it
into `Application`'s construction and `Application::Run()`'s per-frame loop,
per PHASE0's Locked Design Decision #6 (drain EARLY, right after input
polling, before `Game::Update()`).

## Step 2 — The Situation

- `src/Application/FrameCaptureBridge.h/.cpp` is the exact template to mirror
  structurally (mutex + `condition_variable` "slot", a blocking
  `RequestXAndWait()` called from the network thread, a cheap
  `IsXRequested()` + `FulfillX()`/`FailX()` pair called once per frame from
  the main thread) — **read it fully before writing anything** (already
  reproduced in full in this campaign's PHASE0 investigation, and in the file
  itself).
- Per PHASE0's Locked Design Decision #5, this new bridge uses a **single
  global slot** (unlike `FrameCaptureBridge`'s two independent per-kind
  slots) — only one engine command, of either kind, may be in flight across
  the whole bridge at once.
- `src/Application/Application.h` declares `FrameCaptureBridge m_captureBridge;`
  BEFORE `Network::NetworkServer m_networkServer;` specifically so its address
  can be handed into `NetworkServer`'s constructor (C++ member
  construction/destruction order — see that header's own comments on exactly
  why this ordering matters). The new bridge member must follow the same
  rule.
- `src/Network/NetworkServer.h`'s constructor already takes one defaulted,
  non-owning pointer parameter (`FrameCaptureBridge* captureBridge = nullptr`)
  specifically so every existing no-argument `NetworkServer server;`
  construction in `tests/Network/NetworkServerTests.cpp` keeps compiling
  unchanged (see that constructor's own doc comment) — this phase's own
  change to that constructor must preserve that same backward-compatibility
  property (a NEW defaulted pointer parameter, never a required one, appended
  after the existing one).
- `Application::Run()`'s existing per-frame loop structure (`src/Application/Application.cpp`)
  — the SDL event-polling block is wrapped in its own `{ ... }` scope tagged
  `GTE_PROFILE_SCOPE("Application::PollEvents")`; immediately AFTER that
  scope's closing `}` and BEFORE `const Uint64 nowTicksNs = SDL_GetTicksNS();`
  is computed is the exact insertion point Locked Design Decision #6 calls
  for ("right after input polling, before Game::Update()" — `m_game.Update()`
  itself doesn't run until well after `m_editorLayer->NewFrame()`/
  `m_renderer.BeginFrame()`, later in the same function body — the new drain
  must land BEFORE all of that, immediately after event polling).

## Step 3 — The Plan

### 3.1 — New file `src/Application/EngineCommandBridge.h`

```cpp
#pragma once

// The ONE reviewed, thread-safe bridge a Network route handler (background
// thread) is allowed to touch to make an ECS-MUTATING request happen on the
// main thread - network-impl-3 campaign, PHASE4. Structurally mirrors
// FrameCaptureBridge.h (see that file's own header comment for the shared
// design rationale) with two deliberate differences: (1) a request here
// carries a real PAYLOAD (which shape/name/position/parent, or which name to
// delete) rather than just "which kind"; (2) per PHASE0_MASTER_STRATEGY.md's
// Locked Design Decision #5, this bridge has a SINGLE GLOBAL slot - only one
// engine command, of EITHER kind, may be in flight at a time across the
// whole bridge, never one slot per kind.
//
// Deliberately free of Game/Registry/Renderer#includes - this header only
// ever moves plain EngineCommandResults.h outcome structs (+ plain request
// data: strings/Vec3/bools) between "the network thread wants this done" and
// "the main thread did it" - the actual ECS mutation happens elsewhere
// (Game::InstantiatePrimitive()/DeleteEntityByName(), Phase 3) and hands its
// *result* to this bridge, never the reverse. Same "moves already-produced
// plain data, never a live pointer/reference" rule FrameCaptureBridge.h's own
// header comment states.
//
// Owned by Application (the composition root), constructed alongside
// m_captureBridge, BEFORE NetworkServer (so its address can be handed into
// NetworkServer's constructor) - see Application.h.

#include "../ECS/Entity.h"
#include "../Game/EngineCommandResults.h"
#include "../Math/Vec3.h"

#include <condition_variable>
#include <mutex>
#include <optional>
#include <string>

namespace gte {

enum class EngineCommandKind {
    InstantiatePrimitive,
    DeleteEntity,
};

// Plain request payload for one InstantiatePrimitive command - copied
// wholesale across the thread boundary (see EngineCommandRequest below).
struct InstantiatePrimitiveCommand {
    std::string shape;
    std::string requestedName;
    Vec3 worldPosition;
    bool hasParent = false;
    std::string parentName;
};

// Plain request payload for one DeleteEntity command.
struct DeleteEntityCommand {
    std::string name;
};

// One pending engine command, tagged by `kind` - EXACTLY one of
// `instantiatePrimitive`/`deleteEntity` is meaningful, selected by `kind`
// (deliberately a plain tagged struct, not std::variant, matching this
// codebase's existing FrameCaptureKind + CapturedPngImage precedent - a
// single-purpose enum tag plus plain sibling fields, no visitor machinery
// needed for just two kinds).
struct EngineCommandRequest {
    EngineCommandKind kind = EngineCommandKind::InstantiatePrimitive;
    InstantiatePrimitiveCommand instantiatePrimitive;
    DeleteEntityCommand deleteEntity;
};

// The completed result of one EngineCommandRequest - `kind` mirrors the
// request's own `kind` (so a caller holding only the result can still tell
// which outcome field is meaningful).
struct EngineCommandResult {
    EngineCommandKind kind = EngineCommandKind::InstantiatePrimitive;
    InstantiatePrimitiveOutcome instantiatePrimitive;
    DeleteEntityOutcome deleteEntity;
};

class EngineCommandBridge {
public:
    EngineCommandBridge() = default;
    ~EngineCommandBridge() = default;

    EngineCommandBridge(const EngineCommandBridge&) = delete;
    EngineCommandBridge& operator=(const EngineCommandBridge&) = delete;

    // --- Called from the NETWORK thread (a route handler) only ----------

    struct SubmitResult {
        // Meaningful only when neither of the two flags below is set.
        std::optional<EngineCommandResult> result;
        // True (result/timedOut both meaningless) if ANOTHER command
        // (either kind) is already pending from a different caller - returns
        // IMMEDIATELY, without blocking at all, mirroring
        // FrameCaptureBridge::RequestResult::alreadyPending exactly.
        bool alreadyPending = false;
        // True (result meaningless) if `timeoutMilliseconds` elapsed with no
        // fulfillment from the main thread.
        bool timedOut = false;
    };
    SubmitResult SubmitAndWait(EngineCommandRequest request, int timeoutMilliseconds = 3000);

    // --- Called from the MAIN thread (Application::Run()) only ----------

    // True if a route handler is currently waiting on a command - a cheap,
    // side-effect-free read, never blocks. Kept as its own method purely for
    // read-only observability (e.g. a future test/diagnostic) - Application::Run()
    // itself must NOT use this together with a separate "now fetch it" call
    // (see TryPeekPendingCommandRequest() below for why that combination is
    // unsafe).
    bool IsCommandPending() const;

    // SECOND-ITERATION FIX (this doc's own review pass): the original design
    // here was a separate IsCommandPending() check followed by a second,
    // separately-locked PeekPendingCommandRequest() call that ASSERTED
    // IsCommandPending() had just returned true. That is a genuine
    // check-then-use race: the network thread's own SubmitAndWait() can time
    // out and reset m_requested to false in the (tiny, but non-zero) window
    // between those two separate lock/unlock cycles, which would fire the
    // assert (a debug-build CRASH) or, in release, hand back a stale/garbage
    // m_request - a direct violation of this campaign's own Cross-Phase
    // Invariant #3 ("never crashes the engine"). Fixed by collapsing
    // "is one pending" and "copy it out" into ONE atomically-locked
    // operation instead - there is no gap in which the network thread can
    // observe/mutate state in between.
    //
    // Returns a COPY of the currently-pending request's data, or
    // std::nullopt if nothing is pending right now. A copy, not a reference,
    // is returned deliberately - the main thread should never hold a
    // reference into this bridge's own internal, mutex-guarded storage past
    // this one call. This is the ONLY method Application::Run() should call
    // to fetch a pending command - never resurrect the old
    // IsCommandPending() + a separate fetch-by-reference pattern.
    std::optional<EngineCommandRequest> TryPeekPendingCommandRequest() const;

    // Delivers a completed result to whichever network thread is waiting -
    // a safe no-op if nothing is currently pending (mirrors
    // FrameCaptureBridge::FulfillPendingRequest()'s own defensive no-op
    // behavior for the same reason: a request that already timed out on the
    // network side must never crash the main thread that eventually finishes
    // it anyway).
    void FulfillCommand(EngineCommandResult result);

private:
    mutable std::mutex m_mutex;
    std::condition_variable m_conditionVariable;
    bool m_requested = false;
    bool m_fulfilled = false; // true once m_result is meaningful.
    EngineCommandRequest m_request;
    EngineCommandResult m_result;
};

} // namespace gte
```

### 3.2 — Implement in `src/Application/EngineCommandBridge.cpp`

Mirror `FrameCaptureBridge.cpp`'s `RequestCaptureAndWait()`/`IsCaptureRequested()`/
`FulfillPendingRequest()` logic exactly, adapted for a single slot (no
`SlotFor(kind)` indirection needed at all — there is only one slot, this
bridge's own members) and for carrying/returning `EngineCommandRequest`/
`EngineCommandResult` payloads instead of `CapturedPngImage`. Specifically:

- `SubmitAndWait()`: lock the mutex; if `m_requested` is already true, return
  `{ .alreadyPending = true }` immediately (no wait). Otherwise set
  `m_requested = true`, `m_fulfilled = false`, `m_request =
  std::move(request)`, then `m_conditionVariable.wait_for(lock,
  timeoutMilliseconds, [this] { return m_fulfilled; })`. On success, copy out
  `m_result`, reset `m_requested = false`, return `{ .result = m_result }`. On
  timeout, reset `m_requested = false` (so a late `FulfillCommand()` call
  arriving after this point is a safe, silent no-op per that method's own
  contract) and return `{ .timedOut = true }`.
- `IsCommandPending() const`: lock, return `m_requested`.
- `TryPeekPendingCommandRequest() const`: lock; return `m_requested ?
  std::optional<EngineCommandRequest>(m_request) : std::nullopt` - the check
  and the copy happen under the SAME lock acquisition, so there is no gap for
  the network thread's own `SubmitAndWait()` timeout path to reset
  `m_requested` in between (see this method's own header doc comment for why
  the OLD two-call "IsCommandPending() then a separately-locked peek" shape
  was a genuine, if narrow, crash-risking race).
- `FulfillCommand(EngineCommandResult result)`: lock; if `!m_requested`,
  return (no-op); otherwise set `m_result = std::move(result)`, `m_fulfilled =
  true`, unlock, `m_conditionVariable.notify_one()` — same "notify AFTER
  releasing the lock" shape `FrameCaptureBridge.cpp` already uses.

### 3.3 — New file `src/Application/EngineCommandDispatch.h/.cpp`

The thin, MAIN-THREAD-ONLY dispatcher `Application::Run()` calls once per
frame — the one place that actually unpacks an `EngineCommandRequest` and
calls into `Game`'s Phase-3 methods. Kept as its OWN small file (not inlined
into `Application.cpp`) mirroring `src/Application/RenderPasses.h/.cpp`'s own
precedent of extracting `Run()`-helper free functions into a sibling file
under `src/Application/`, so `Application.cpp` itself stays a thin
orchestration script.

```cpp
// EngineCommandDispatch.h
#pragma once

#include "EngineCommandBridge.h"

namespace gte {

class Game;
class Renderer;

// Executes ONE already-pending EngineCommandRequest against `game`/`renderer`
// (both must be the SAME live instances Application itself owns - this
// function is ONLY ever called from Application::Run(), on the main thread)
// and returns the completed EngineCommandResult, ready to hand to
// EngineCommandBridge::FulfillCommand(). Never throws - Game::
// InstantiatePrimitive()/DeleteEntityByName() (Phase 3) already degrade
// every failure mode into a plain success == false outcome, so this function
// has nothing further to catch.
EngineCommandResult ExecuteEngineCommand(Game& game, Renderer& renderer, const EngineCommandRequest& request);

} // namespace gte
```

```cpp
// EngineCommandDispatch.cpp
#include "EngineCommandDispatch.h"

#include "../Game/Game.h"
#include "../Renderer/Renderer.h"

namespace gte {

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
    }
    return result;
}

} // namespace gte
```

**CRITICAL — CMakeLists.txt registration (second-iteration finding): the root
`CMakeLists.txt` builds `gte_core` from an explicit, hand-maintained
`target_sources(gte_core PRIVATE ...)` file list (`add_library(gte_core
STATIC ...)`) - it does NOT glob for source files.** BOTH new `.cpp` files
this phase introduces (`EngineCommandBridge.cpp`, `EngineCommandDispatch.cpp`)
MUST be added to that list or they are simply never compiled into `gte_core` -
a guaranteed link failure the moment `Application.cpp`/`NetworkServer.cpp`
try to call into them, not a style nitpick. Add all four new lines
immediately after the existing `src/Application/FrameCaptureBridge.cpp` line
(same `src/Application/` block, right before `src/Application/RenderPasses.h`):
```
    src/Application/EngineCommandBridge.h
    src/Application/EngineCommandBridge.cpp
    src/Application/EngineCommandDispatch.h
    src/Application/EngineCommandDispatch.cpp
```

### 3.4 — Wire into `src/Application/Application.h`

- `#include "EngineCommandBridge.h"`.
- Add `EngineCommandBridge m_commandBridge;` immediately after the existing
  `FrameCaptureBridge m_captureBridge;` member (same "declared BEFORE
  m_networkServer so its address can be handed into that constructor" comment
  pattern — copy/adapt the existing comment there rather than leaving this
  new member undocumented).

### 3.5 — Wire into `src/Network/NetworkServer.h/.cpp`

- Forward-declare `namespace gte { class EngineCommandBridge; }` in
  `NetworkServer.h`, mirroring the existing `FrameCaptureBridge` forward
  declaration immediately above it.
- Constructor gains a SECOND defaulted, non-owning pointer parameter:
  `explicit NetworkServer(FrameCaptureBridge* captureBridge = nullptr, EngineCommandBridge* commandBridge = nullptr);`
  — append-only, preserving every existing call site's compilability (see
  Step 2 above).
- Store `EngineCommandBridge* m_commandBridge = nullptr;` (private member,
  same shape as `m_captureBridge`).
- `NetworkServer.cpp`: `#include "../Application/EngineCommandBridge.h"`;
  thread `commandBridge` through to `RegisterRoutes()` exactly like
  `captureBridge` already is (Phase 5 is what actually uses it inside
  `RegisterRoutes()` — this phase only needs to make sure the pointer
  correctly ARRIVES there; it is fine/expected for this phase's
  `RegisterRoutes()` to not yet reference `commandBridge` at all, producing
  an "unused parameter" situation Phase 5 resolves — leave a `(void)commandBridge;`
  placeholder, or accept the compiler warning, whichever this codebase's
  existing warning discipline calls for; check current compiler warning flags
  before deciding).

### 3.6 — Wire into `src/Application/Application.cpp`

- `#include "EngineCommandDispatch.h"`.
- Constructor: `, m_networkServer(&m_captureBridge, &m_commandBridge)`
  (replaces the current `, m_networkServer(&m_captureBridge)`).
- `Run()`: immediately after the SDL event-polling `{ ... }` scope's closing
  `}` (see Step 2's precise insertion-point analysis above) and BEFORE
  `const Uint64 nowTicksNs = SDL_GetTicksNS();`, insert:

  ```cpp
  // network-impl-3 campaign (task_manager/network-impl-3/) - drains at most
  // ONE pending network-issued engine command (instantiate_primitive/
  // delete_entity) per frame, as EARLY as possible - right after input
  // polling, BEFORE Game::Update()/Physics/Animation run this frame - so a
  // freshly spawned/deleted entity is fully consistent for the REST of this
  // exact frame (see PHASE4_ENGINE_COMMAND_BRIDGE_AND_MAIN_LOOP_INTEGRATION.md,
  // and PHASE0_MASTER_STRATEGY.md's own Locked Design Decision #6).
  if (const std::optional<EngineCommandRequest> request = m_commandBridge.TryPeekPendingCommandRequest()) {
      GTE_PROFILE_SCOPE("Application::ExecuteEngineCommand");
      const EngineCommandResult result = ExecuteEngineCommand(m_game, m_renderer, *request);
      m_commandBridge.FulfillCommand(result);
  }
  ```

## Verification for this phase

- Fast compile check.
- New `tests/Application/EngineCommandBridgeTests.cpp` (check first whether
  `tests/Application/FrameCaptureBridgeTests.cpp` already exists — if so,
  mirror its exact structure/spawn-a-thread-and-call-SubmitAndWait pattern;
  if it does not exist, write this new bridge's test using the most direct
  single-threaded approach that still proves the contract: call
  `SubmitAndWait()` from a spawned `std::thread` while the main test thread
  polls `IsCommandPending()`/calls `TryPeekPendingCommandRequest()`/
  `FulfillCommand()`, asserting the correct result comes back; also test the
  `alreadyPending` path by holding one `SubmitAndWait()` deliberately
  in-flight on a background thread while a second concurrent call observes
  `alreadyPending == true`; also test the `timedOut` path with a very small
  `timeoutMilliseconds` and nothing ever fulfilling it). Add this new file to
  `tests/CMakeLists.txt`'s always-built source list (this module has no
  GPU/httplib dependency).
- Run this new test file plus a full `ctest` pass to confirm zero
  regressions in `Application`'s own construction/`Run()` wiring (a full
  build+run of the actual engine executable, `run_app_background`, briefly
  confirming it still starts up and the window still opens/renders normally,
  is a cheap and worthwhile sanity check here specifically, since this phase
  touches `Application`'s constructor member-initializer-list order and its
  main-loop body directly — a subtle member-order/lifetime mistake here would
  not necessarily show up as a compile error).
- Write `PHASE4_COMPLETION_REPORT.md`, `git add`/`git commit`.
