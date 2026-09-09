# PHASE2 — Cross-Thread Frame Capture Bridge

Parent: `PHASE0_MASTER_STRATEGY.md`. Depends on: Phase 1 (not code-wise, but
this phase is what those utilities will feed data into starting Phase 3) —
implementable independently/in parallel with Phase 1 if desired, but
documented here second per the recommended execution order.

## Step 1: The Goal (Where are we going?)

Build the **one, sanctioned, reviewed, thread-safe bridge** a network route
handler is ever allowed to touch to get real engine data — `class
FrameCaptureBridge` — plus update `AGENTS.md`'s "Networking" section to
document it as the sanctioned exception to the existing "a route handler
must never touch engine state" rule. This class is deliberately Vulkan-free,
Renderer-free, and engine-free: it only ever moves plain
`std::vector<std::uint8_t>` PNG bytes (+ width/height ints) between "the
network thread wants one" and "the main thread produced one" — every actual
pixel-capturing/PNG-encoding happens elsewhere (Phase 3/4) and hands its
*result* to this bridge, never the reverse.

## Step 2: The Situation (Where are we now?)

- `AGENTS.md`, "Networking", already states the exact shape this bridge must
  have, almost verbatim: *"a future endpoint that genuinely needs engine
  data (e.g. 'how many entities are in the scene') needs a dedicated,
  reviewed, thread-safe bridge built first (e.g. a fixed-size, mutex-guarded
  command/snapshot queue the main thread drains once per frame, mirroring
  the shape of `Jobs::detail::JobQueue`) — never a raw pointer/reference
  into live engine state handed to a handler lambda."* This phase builds
  exactly that, for exactly the two capture kinds this campaign needs
  (Swapchain, GameView) — not a generic, open-ended "engine command queue"
  for arbitrary future use; see `AGENTS.md`'s own precedent elsewhere
  ("Editor Module Structure": *"don't introduce an `IEditorPanel` abstraction
  preemptively; only reach for one if a genuine, stated requirement... shows
  up later"*) for why this deliberately stays narrow.
- `src/Application/Application.h/.cpp` is the engine's one composition root,
  already owning both `Renderer` and `Network::NetworkServer` as direct
  members, constructed in a fixed order (`Renderer` before
  `NetworkServer` — see `Application.h`'s own member-declaration-order
  comments). This bridge needs to be constructed BEFORE `NetworkServer` (so
  it can be handed into `NetworkServer`'s constructor — see Phase 3) and
  must outlive it.
- `NetworkServer`'s constructor takes no arguments today and is called from
  SEVEN separate places in `tests/Network/NetworkServerTests.cpp` — four
  locals literally named `server` plus one each named `probe`/`collider`/
  `healthy` — all as `gte::Network::NetworkServer <name>;` with no
  arguments (re-verified directly against the live file; an earlier draft
  of this document undercounted this as "five separate places") — Phase 3
  (not this phase) is what changes `NetworkServer`'s constructor signature;
  this phase's own class must be usable completely independently of
  `NetworkServer` so that change can be additive/backward-compatible.
- Precedent for a mutex+condition_variable cross-thread handoff already
  exists in this codebase's Job System
  (`src/Jobs/JobSystem.cpp`'s `WaitForJobs()`/`m_completionMutex`/
  `m_completionCondition`) — including a documented, previously-real
  "lost wakeup" race (`AGENTS.md`, "Job System": *"a worker's pending-count
  decrement MUST be bracketed by the SAME mutex `WaitForJobs()` holds while
  checking its own predicate... this is a real, confirmed-in-practice
  classic `condition_variable` lost-wakeup race"*) — this phase's own
  `FrameCaptureBridge` must follow that exact same "mutate the predicate
  only while holding the SAME mutex the waiter's `wait()` predicate check
  uses" discipline from the start, not discover the same bug the hard way a
  second time.

## Step 3: The Plan

### 3.1 — Data types

```cpp
// src/Application/FrameCaptureBridge.h
#pragma once
#include <condition_variable>
#include <cstdint>
#include <mutex>
#include <optional>
#include <string>
#include <vector>

namespace gte {

// Which real render target a capture request is asking for - see
// PHASE0_MASTER_STRATEGY.md's own two-endpoint design decision.
enum class FrameCaptureKind {
    Swapchain, // GET /get_swapchain - the literal, currently-presented OS window image.
    GameView,  // GET /get_game_view - the Game's own off-screen 3D-scene RenderTexture.
};

// A successfully captured, already-PNG-encoded still frame.
struct CapturedPngImage {
    std::vector<std::uint8_t> pngBytes;
    int width = 0;
    int height = 0;
};

// Why a capture request did NOT produce an image - surfaced to the network
// route as a specific HTTP status (see PHASE3/PHASE5's own route handlers).
enum class FrameCaptureFailureReason {
    TargetNotAvailable, // e.g. no Game view exists/visible this session, or the window is minimized.
    TimedOut,           // the main thread never serviced the request within the fixed timeout.
};

} // namespace gte
```

### 3.2 — `FrameCaptureBridge` class shape

```cpp
// still src/Application/FrameCaptureBridge.h, continued
namespace gte {

// The ONE reviewed, thread-safe bridge a Network route handler (background
// thread - see AGENTS.md, "Networking") is allowed to touch, for exactly
// the two capture kinds this campaign needs. Every method here is safe to
// call concurrently from any thread. Never hands out a raw pointer/reference
// into live engine state - every value crossing this boundary is a plain,
// independently-owned copy (CapturedPngImage's own std::vector, moved).
//
// Owned by Application (the composition root), constructed BEFORE
// NetworkServer (see Application.h) so it can be handed into NetworkServer's
// constructor - see PHASE3. Must outlive NetworkServer.
class FrameCaptureBridge {
public:
    FrameCaptureBridge() = default;
    ~FrameCaptureBridge() = default;

    FrameCaptureBridge(const FrameCaptureBridge&) = delete;
    FrameCaptureBridge& operator=(const FrameCaptureBridge&) = delete;

    // --- Called from the NETWORK thread (a route handler) only ----------

    // Blocks the CALLING (network) thread until either:
    //  - the main thread fulfills this request (returns the captured
    //    image), or
    //  - `timeoutMilliseconds` elapses with no fulfillment (returns
    //    FrameCaptureFailureReason::TimedOut), or
    //  - the main thread positively determines the target isn't available
    //    this frame (returns FrameCaptureFailureReason::TargetNotAvailable -
    //    see FailPendingRequest() below), or
    //  - a request of this SAME `kind` is already pending from a DIFFERENT
    //    caller (returns immediately, WITHOUT blocking at all, as a THIRD,
    //    distinct outcome - see the return type note below).
    //
    // Never blocks longer than timeoutMilliseconds. Never touches Vulkan/
    // Renderer/any engine subsystem - purely mutex/condition_variable
    // bookkeeping plus moving already-produced bytes.
    struct RequestResult {
        // Exactly one of these three is meaningful - std::nullopt on both
        // `image`/`failure` (both empty) means "another caller's request of
        // this same kind was already pending" (HTTP 503 - see PHASE3's own
        // route handler for exactly how this maps to a status code).
        std::optional<CapturedPngImage> image;
        std::optional<FrameCaptureFailureReason> failure;
        bool alreadyPending = false;
    };
    RequestResult RequestCaptureAndWait(FrameCaptureKind kind, int timeoutMilliseconds = 3000);

    // --- Called from the MAIN thread (Application::Run()) only ----------

    // True if a route handler is currently waiting on `kind` (i.e. the main
    // thread should actually go perform this capture this frame). A cheap,
    // side-effect-free read - never blocks.
    bool IsCaptureRequested(FrameCaptureKind kind) const;

    // Delivers a successfully captured+encoded image to whichever network
    // thread is waiting on `kind` (a safe no-op, doing nothing, if nothing
    // is currently pending for `kind` - e.g. the main thread producing a
    // capture "just in case" nobody asked for one this exact frame should
    // never happen given IsCaptureRequested()'s own guard, but this method
    // stays defensive regardless).
    void FulfillPendingRequest(FrameCaptureKind kind, CapturedPngImage image);

    // Tells whichever network thread is waiting on `kind` that this
    // request cannot be serviced right now (see
    // FrameCaptureFailureReason::TargetNotAvailable) - lets the network
    // thread fail FAST instead of waiting out the full timeout for a
    // request that can never succeed this session (e.g. /get_game_view with
    // no visible Game view).
    void FailPendingRequest(FrameCaptureKind kind, FrameCaptureFailureReason reason);

private:
    struct Slot {
        mutable std::mutex mutex;
        std::condition_variable conditionVariable;
        bool requested = false;
        bool fulfilled = false; // true once `result`/`failureReason` is meaningful.
        std::optional<CapturedPngImage> result;
        std::optional<FrameCaptureFailureReason> failureReason;
    };

    Slot& SlotFor(FrameCaptureKind kind);
    const Slot& SlotFor(FrameCaptureKind kind) const;

    Slot m_swapchainSlot;
    Slot m_gameViewSlot;
};

} // namespace gte
```

### 3.3 — Implementation notes (`FrameCaptureBridge.cpp`)

- `RequestCaptureAndWait()`:
  1. Lock `slot.mutex`.
  2. If `slot.requested` is already `true` (someone else's request is
     in-flight), unlock and return `{ .alreadyPending = true }` IMMEDIATELY
     — never wait at all. This is the HTTP 503 case (Locked Design Decision
     #4 in `PHASE0_MASTER_STRATEGY.md`).
  3. Otherwise set `slot.requested = true`, `slot.fulfilled = false`,
     clear `slot.result`/`slot.failureReason`.
  4. `conditionVariable.wait_for(lock, timeoutMilliseconds, [&]{ return
     slot.fulfilled; })` — the predicate form (never a bare `wait_for` with
     manual re-checking) is what closes the exact lost-wakeup race
     `AGENTS.md`'s Job System section already documents finding once before
     (see Step 2 above) — the predicate is evaluated WHILE HOLDING THE SAME
     LOCK the main thread's own `FulfillPendingRequest()`/
     `FailPendingRequest()` must take before setting `fulfilled = true` and
     calling `notify_one()` (never `notify_all()` — only one waiter can
     ever exist per slot, since a second concurrent request of the same
     kind is rejected up front in step 2 with no waiting at all).
  5. On success (`fulfilled == true` before timing out): move `slot.result`/
     `slot.failureReason` out into the return value; reset `slot.requested =
     false` so a future request can proceed.
  6. On timeout (`wait_for` returned `false`): reset `slot.requested =
     false` (so this slot isn't stuck "pending" forever after a caller gives
     up — a LATE `FulfillPendingRequest()` call arriving after this point
     must be a safe, silent no-op, see below) and return
     `{ .failure = FrameCaptureFailureReason::TimedOut }`.
- `IsCaptureRequested()`: `std::lock_guard`, return `slot.requested`.
- `FulfillPendingRequest()`/`FailPendingRequest()`: lock, guard with
  `if (!slot.requested) return;` (the "late arrival after the waiter already
  gave up on timeout" case from step 6 above — must be inert, never crash/
  assert), set the appropriate `result`/`failureReason` field, set
  `fulfilled = true`, unlock, `notify_one()`.
- Every method takes/releases its own lock — no method calls another
  method of this same class while already holding `slot.mutex` (avoids any
  self-deadlock risk entirely, by construction, rather than by convention).

### 3.4 — `AGENTS.md` update

Add a new bullet to the existing "Networking" section (right after the
existing "a route handler must be a PURE function..." bullet), stating
plainly:

> **`FrameCaptureBridge` (`src/Application/FrameCaptureBridge.h/.cpp`,
> `network-impl-2` campaign) is the ONE sanctioned exception to the rule
> above.** A route handler may call `FrameCaptureBridge::
> RequestCaptureAndWait()` and nothing else engine-side — it never reaches
> into `Renderer`/`Registry`/`Game`/`AssetDatabase` directly, even
> indirectly through this bridge; the bridge itself only ever moves
> already-produced, plain `CapturedPngImage` byte buffers, never a live
> pointer/reference. `Application::Run()` is the ONLY thing that ever calls
> `IsCaptureRequested()`/`FulfillPendingRequest()`/`FailPendingRequest()`,
> once per frame, from the main thread. A future endpoint needing DIFFERENT
> engine data must NOT extend this class's `FrameCaptureKind` enum for an
> unrelated purpose — build its own small, similarly-reviewed, similarly-
> narrow bridge instead, following this one's shape.

### 3.5 — Tests (Tier 1, GoogleTest, no live GPU/window)

`tests/Application/FrameCaptureBridgeTests.cpp`:

- `RequestCaptureAndWait()` on a slot nobody ever fulfills times out and
  returns `FrameCaptureFailureReason::TimedOut` within roughly the requested
  timeout window (use a SHORT timeout in the test, e.g. 100ms, so the test
  itself stays fast).
- A SEPARATE `std::thread` calls `FulfillPendingRequest(kind, someImage)`
  shortly after the main test thread calls `RequestCaptureAndWait(kind,
  <a much longer timeout>)` — assert the result is the exact same image
  (byte-for-byte), and that this returns well before the long timeout would
  have elapsed (proves the condition_variable wakeup, not the timeout, is
  what actually resolved it).
- Two back-to-back `RequestCaptureAndWait()` calls for the SAME kind from
  two different threads, with nothing fulfilling either — the SECOND one
  (started strictly after the first has already set `requested = true`)
  must return `alreadyPending == true` IMMEDIATELY (assert on wall-clock
  time elapsed, not just the return value, to prove it never actually
  waited).
- `FailPendingRequest()` delivers `FrameCaptureFailureReason::
  TargetNotAvailable` correctly, well before any timeout.
- A late `FulfillPendingRequest()`/`FailPendingRequest()` call, arriving
  AFTER a request has already timed out and reset itself, must not crash/
  assert/hang, and must not corrupt the NEXT request's own result (start a
  fresh `RequestCaptureAndWait()` afterward and confirm it behaves
  normally).
- `Swapchain` and `GameView` slots are provably independent — a pending
  request on one kind never blocks/interferes with a concurrent request on
  the other kind.
- Any test exercising real cross-thread interaction should be stress-
  repeated (e.g. `--gtest_repeat=50`) at least once before being trusted,
  per `AGENTS.md`'s own Job System precedent ("a single green run is not
  sufficient evidence for genuinely concurrent code").

### What NOT to do in this phase

- Do not touch `NetworkServer.h/.cpp`, `NetworkRoutes.h/.cpp`, or
  `Application.h/.cpp` yet — this phase is the bridge CLASS alone, fully
  covered by its own tests, with nothing wired to it yet. Phase 3 is the
  first real wiring.
- Do not add a generic "engine command" abstraction beyond the two named
  `FrameCaptureKind` values — see `AGENTS.md`'s own "don't introduce an
  abstraction preemptively" precedent quoted in Step 2 above.
