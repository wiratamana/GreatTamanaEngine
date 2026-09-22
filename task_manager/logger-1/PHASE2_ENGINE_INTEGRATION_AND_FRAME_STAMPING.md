# PHASE2 — Engine Integration and Frame Stamping

Parent: `PHASE0_MASTER_STRATEGY.md`. Depends on: `PHASE1_CORE_LOGGER_MODULE.md`
(read `PHASE1_COMPLETION_REPORT.md` first — it may record an API tweak that
changes an exact call shown below).

**Use `ask_questions` for any genuine ambiguity this document doesn't already
resolve.** If this phase itself delegates any further sub-task, that
delegation prompt must repeat this same instruction.

---

## Step 1: The Goal (Where are we going?)

Wire the Phase 1 `Logger` into the real engine: (a) stamp every log entry
with the real, current simulation frame number, once per real frame, and
(b) add a small, deliberately limited number of brand-new call sites across
more than one real engine subsystem (`Application`, `Jobs`, `Network`),
proving `GTE_LOG_*` is genuinely reachable with zero `#ifdef` from any file
in the engine, without touching a single existing `fprintf` call site. The
actual, rigorous proof that `Logger` is safe under REAL concurrent
multi-thread load already exists independently of this phase —
`PHASE1`'s own dedicated `LoggerTests.cpp` concurrency test, which spawns 8
raw `std::thread`s directly against `Logger::Log()` and asserts strict
ascending-id ordering under real contention. This phase's own four call
sites are a real-world integration smoke test layered on top of that
already-proven safety, not a second, independent proof of it — see Step 2
below for a corrected, fact-checked account of exactly which OS thread each
of these four call sites can and cannot be shown to run on.

## Step 2: The Situation (Where are we now?)

- `Application.cpp`'s main loop (`Application::Run()`) already computes
  `deltaSeconds`/`playbackPaused`/`steppedThisFrame` and calls:
  ```cpp
  m_engineContext.time.Advance(deltaSeconds, playbackPaused, steppedThisFrame, kFixedStepSeconds);
  ```
  immediately followed (same function, a few lines later) by
  `m_editorLayer->NewFrame();`. This is the ONE place, once per real frame,
  where `m_engineContext.time.FrameCount()` is guaranteed to already reflect
  the CURRENT frame.
- `Jobs/JobContinuation.cpp` (line ~206) already has an existing
  `std::fprintf(stderr, ...)` — **checked directly against the real file,
  this is NOT inside a `catch` block at all** (that file has no `catch`
  anywhere): it sits inside `ScheduleAfter()`'s own dependency-validation
  loop, firing when a caller passes a dependency that is (or shares state
  with) its own output `handle` — a caller-bug diagnostic, not exception
  handling. **It is also NOT provably a Job System worker-thread call
  site**: `ScheduleAfter()`/`DispatchAfter()` run on whichever thread calls
  them, and — confirmed by a repo-wide search — nothing under `src/`
  actually calls either function in production today (only tests do), so
  there is no real call chain proving this line executes on a
  `Jobs::JobSystem` worker thread specifically, as opposed to the main
  thread or any other caller. It is still a perfectly good place for a
  SECOND, brand-new, additive log call (an always-compiled file outside
  `Application.cpp`/`NetworkServer.cpp`, exercising the same "no `#ifdef`
  needed" property) — just do not describe it, here or in the completion
  report, as a proven worker-thread example; describe it plainly as "an
  additive log call next to `Jobs/JobContinuation.cpp`'s existing `fprintf`
  site".
- `Network/NetworkServer.cpp` (line ~932) already has an existing
  `std::fprintf(stderr, "NetworkServer: failed to bind %s:%d - network
  endpoint disabled this run.\n", ...)` inside `Start()` — note this
  particular call actually runs on the calling thread (the MAIN thread,
  since `Application` calls `Start()` synchronously during construction),
  not the Network background thread, so it does NOT by itself prove
  background-thread safety; it is still a reasonable place for an additive
  `GTE_LOG_ERROR` call (a genuinely useful, real diagnostic). **Given the
  correction above, none of this phase's four call sites is independently,
  provably a non-main-thread call site** — that is fine (see this
  document's Step 1 Goal: the rigorous concurrency proof already lives in
  `PHASE1`'s own dedicated 8-thread test), but do not claim otherwise in the
  completion report. A genuine, PROVEN Network-background-thread log call
  site (every registered route handler runs on `NetworkServer`'s own
  dedicated background thread, per `docs/conventions/networking.md`) will
  exist naturally starting in `PHASE3`, once `GET /get_logs`/
  `POST /clear_logs` route handlers exist on that thread — no need to
  manufacture one here.
- Every one of these files is always compiled (none of them live under
  `src/Editor/`), so each new `#include "../Editor/Logger.h"` in them is
  itself another real-world instance of the Phase 1 "safely includable from
  outside `src/Editor/`, regardless of `GTE_ENABLE_EDITOR`" property being
  exercised — confirm each one compiles cleanly in this phase's own
  incremental check.

## Step 3: The Plan

### 3.1 Per-frame frame-number stamping

In `src/Application/Application.cpp`:

- Add `#include "../Editor/Logger.h"` near the top, alongside this file's
  other includes.
- Immediately after the existing
  `m_engineContext.time.Advance(deltaSeconds, playbackPaused, steppedThisFrame, kFixedStepSeconds);`
  line, add:
  ```cpp
  // logger-1 campaign, Phase 2 - stamps every Logger entry recorded from
  // here until the next Advance() with THIS frame's number. Safe to call
  // unconditionally, with no #if GTE_ENABLE_EDITOR guard needed at this
  // call site - Logger::SetCurrentFrame() is a real no-op in that
  // configuration (see Editor/Logger.h).
  gte::Logger::SetCurrentFrame(m_engineContext.time.FrameCount());
  ```
- Do **not** wrap this call in `#if GTE_ENABLE_EDITOR` — the entire point of
  Phase 1's dual-branch header is that call sites like this one never need
  to know or care which configuration is active.

### 3.2 A small number of brand-new, additive `GTE_LOG_*` call sites

Exactly these four, no more (keep this phase's surface area small and
reviewable — this is a proof of mechanism, not a documentation/cleanup
pass):

1. **`Application.cpp`, constructor** (`Application::Application(...)`'s own
   body — after every other subsystem is already constructed): place this at
   the very end of that constructor's body, right after the existing
   `#if GTE_ENABLE_NETWORK` / `m_networkServer.Start(8080);` / `#endif` block.
   **Do not anchor this near `SdlMemoryTracker::Install()`** — despite
   `PHASE0`'s own Step 2 loosely describing that as a nearby precedent,
   checked directly against the real file, `SdlMemoryTracker::Install()`
   actually lives inside a COMPLETELY DIFFERENT constructor,
   `Application::SdlContext::SdlContext()` (the very first member
   subobject's own constructor, which runs and completes before
   `Application::Application()`'s own body even starts) — there is no such
   block directly inside `Application::Application()`'s own body to anchor
   next to. The only existing conditional block actually inside
   `Application::Application()`'s own body is the `#if GTE_ENABLE_NETWORK`
   one just described; place the new line right after it, unconditional
   (not wrapped in any `#if` — `GTE_LOG_INFO` already vanishes on its own
   when `GTE_ENABLE_EDITOR` is OFF):
   ```cpp
   GTE_LOG_INFO("Application", "GreatTamanaEngine started.");
   ```
2. **`Jobs/JobContinuation.cpp`**, inside `ScheduleAfter()`'s own
   dependency-validation loop (see Step 2's corrected description above —
   NOT a catch block, this file has no `catch` anywhere), immediately AFTER
   (never replacing) its existing `std::fprintf(stderr, ...)` call, right
   before the existing `assert(false && "ScheduleAfter()/DispatchAfter():
   self-dependency on output handle");` line:
   ```cpp
   GTE_LOG_ERROR("Jobs", <the same already-formatted message text the
       existing fprintf call already builds/uses, converted to a std::string
       - do not reformat or invent new wording, just mirror what's already
       being printed to stderr>);
   ```
   Needs `#include "../Editor/Logger.h"` added to `Jobs/JobContinuation.cpp`.
3. **`Network/NetworkServer.cpp`**, immediately after its existing
   `std::fprintf(stderr, "NetworkServer: failed to bind %s:%d - network
   endpoint disabled this run.\n", ...)` call inside `Start()`:
   ```cpp
   GTE_LOG_ERROR("Network", "failed to bind " + std::string(kBindHost) + ":"
       + std::to_string(port) + " - network endpoint disabled this run.");
   ```
   (Match whatever the real local variable/constant names for the host/port
   actually are at that call site — read the surrounding code first.) Needs
   `#include "../Editor/Logger.h"` added to `Network/NetworkServer.cpp`
   (this is also exactly what `PHASE3` will need anyway for the new routes,
   so this include is not wasted work).
4. **`Network/NetworkServer.cpp`**, immediately after its existing
   `std::fprintf(stdout, "NetworkServer: listening on %s:%d\n", ...)`
   success case:
   ```cpp
   GTE_LOG_INFO("Network", "listening on " + std::string(kBindHost) + ":"
       + std::to_string(resolvedPort));
   ```

Every one of these four is **additive only** — the pre-existing
`std::fprintf` calls right next to them are not touched, reordered, or
reworded in any way (Locked Design Decision 2 in `PHASE0`).

### 3.3 What this phase deliberately does NOT do

- Does not touch `Application.cpp`'s two `RenderGraph ... Execute() failed`
  `fprintf` sites, `main.cpp`'s two sites,
  `Renderer/RenderGraph/RenderGraphCompiler.cpp`,
  `Renderer/RenderGraph/RenderPipeline.h`, or any of
  `Renderer/Vulkan/VulkanInstance.cpp`'s three sites — these stay
  completely untouched (Locked Design Decision 2). Migrating any of these
  is explicitly out of scope for this whole campaign, not just this phase.
- Does not add any network route or UI panel — those are `PHASE3`/`PHASE4`.
- Does not write a new automated test in this phase — `Logger`'s own
  behavior is already fully covered by `PHASE1`'s test file; `Application`/
  `NetworkServer`/`JobContinuation` are not newly made independently
  testable by this phase's four call sites (they are simple, deliberately
  low-risk one-line additions to existing production code paths, exercised
  indirectly the moment the engine runs at all).

## Definition of Done for this phase

- Fast, targeted incremental compile check (default `GTE_ENABLE_EDITOR=ON`
  configuration) — confirm `gte_core` still builds cleanly with the four new
  call sites and three new `#include`s.
- A quick manual sanity check is enough for this phase (full end-to-end
  verification over HTTP is `PHASE5`'s job): e.g. briefly run the built
  engine (`run_app_background`) and confirm it still starts/renders
  normally with these new lines in place, then stop it
  (`stop_app_background`) — this phase does not yet have any way to
  observe the logged entries itself (no endpoint, no panel exist yet), so
  don't over-invest here.
- Write `PHASE2_COMPLETION_REPORT.md` next to this file, including the
  exact final wording/placement chosen for each of the four new call sites
  (so `PHASE5`'s smoke test author, if they want a real end-to-end example,
  knows exactly what to expect in the buffer at startup).
- `git add`/`git commit` (this phase's own changes + the report) with a
  clear message.
