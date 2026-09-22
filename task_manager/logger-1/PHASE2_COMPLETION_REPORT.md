# PHASE2 — Engine Integration and Frame Stamping — Completion Report

Parent: `PHASE0_MASTER_STRATEGY.md`. Phase document:
`PHASE2_ENGINE_INTEGRATION_AND_FRAME_STAMPING.md`.

## Summary

Wired the Phase 1 `Logger` into the real engine exactly per this phase's
Step 3 plan (as corrected immediately before this delegation — Step 2's
fact-checked account of the `Jobs/JobContinuation.cpp` and
`Application.cpp` constructor call sites was read and followed, not any
stale/cached understanding of the document):

### 3.1 Per-frame frame-number stamping

`src/Application/Application.cpp`, inside `Application::Run()`'s main loop,
immediately after the existing
`m_engineContext.time.Advance(deltaSeconds, playbackPaused, steppedThisFrame, kFixedStepSeconds);`
line:

```cpp
gte::Logger::SetCurrentFrame(m_engineContext.time.FrameCount());
```

Unconditional — no `#if GTE_ENABLE_EDITOR` guard, per the phase doc's
explicit instruction (the whole point of `Logger.h`'s dual-branch shape is
that call sites like this never need to know which configuration is
active).

### 3.2 Four brand-new, additive `GTE_LOG_*` call sites

1. **`Application.cpp`, constructor** (`Application::Application(...)`) —
   placed at the very end of the constructor body, right after the existing
   `#if GTE_ENABLE_NETWORK` / `m_networkServer.Start(8080);` / `#endif`
   block (the ONLY conditional block that actually exists directly inside
   this constructor's own body — confirmed by reading the real file before
   editing; `SdlMemoryTracker::Install()` was correctly NOT used as the
   anchor, since it lives inside the unrelated `Application::SdlContext::SdlContext()`
   constructor, per this phase's corrected Step 3.2):
   ```cpp
   GTE_LOG_INFO("Application", "GreatTamanaEngine started.");
   ```
2. **`Jobs/JobContinuation.cpp`**, inside `ScheduleAfter()`'s own
   dependency-validation loop (confirmed: no `catch` block exists anywhere
   in this file, and this call site is NOT provably a Job-System
   worker-thread call — `ScheduleAfter()`/`DispatchAfter()` run on whichever
   thread calls them, and nothing under `src/` calls either in production
   today), immediately AFTER the existing `std::fprintf(stderr, ...)` call
   and BEFORE the existing `assert(false && ...)` line, mirroring the exact
   wording already printed to `stderr` (converted to a `std::string`, no
   reformatting/rewording):
   ```cpp
   GTE_LOG_ERROR("Jobs",
       "gte::Jobs::ScheduleAfter()/DispatchAfter(): a dependency must "
       "never be (or share underlying state with) its own output "
       "handle - ignoring this dependency to avoid a permanent "
       "deadlock.");
   ```
   Added `#include "../Editor/Logger.h"` to this file.
3. **`Network/NetworkServer.cpp`**, immediately after the existing
   `std::fprintf(stderr, "NetworkServer: failed to bind %s:%d - network
   endpoint disabled this run.\n", kBindHost, port);` call inside `Start()`
   (this call runs on the CALLING/main thread, since `Application` calls
   `Start()` synchronously during construction — not the Network background
   thread; documented as such, not claimed otherwise):
   ```cpp
   GTE_LOG_ERROR("Network", "failed to bind " + std::string(kBindHost) + ":"
       + std::to_string(port) + " - network endpoint disabled this run.");
   ```
4. **`Network/NetworkServer.cpp`**, immediately after the existing
   `std::fprintf(stdout, "NetworkServer: listening on %s:%d\n", kBindHost,
   resolvedPort);` success case:
   ```cpp
   GTE_LOG_INFO("Network", "listening on " + std::string(kBindHost) + ":"
       + std::to_string(resolvedPort));
   ```
   Added `#include "../Editor/Logger.h"` to this file (also needed by
   `PHASE3`'s new routes, so not wasted work).

Every one of these four is strictly additive — none of the pre-existing
`fprintf` calls were touched, reordered, or reworded (Locked Design Decision
2 in `PHASE0`).

## What was NOT touched (per this phase's own Step 3.3 / PHASE0's Non-Goals)

- `Application.cpp`'s two `RenderGraph ... Execute() failed` `fprintf`
  sites, `main.cpp`'s two sites, `Renderer/RenderGraph/RenderGraphCompiler.cpp`,
  `Renderer/RenderGraph/RenderPipeline.h`, and all three
  `Renderer/Vulkan/VulkanInstance.cpp` sites — completely untouched.
- No new network route or UI panel (PHASE3/PHASE4's job).
- No new automated test — `Logger`'s own behavior is already fully covered
  by `PHASE1`'s test file; these four call sites are simple, low-risk
  additions to existing production code paths.

## A mid-flight self-correction worth recording

While inserting the very first new `#include "../Editor/Logger.h"` line
into `Application.cpp`'s include block, an `edit_line` call targeted the
wrong 0-based line index (off-by-one against the actual file content at
that moment) and silently replaced `#include "../Encoding/PngEncoder.h"`
instead of inserting alongside it — the tool's own boundary-duplicate
auto-dedup then (correctly, given what it saw) removed what looked like a
duplicate `SdlMemoryTracker.h` line, compounding the same mistake. This was
caught immediately by the very next targeted compile check (`gte_core`
failed with `'EncodeRgba8ToPng' is not a member of 'gte::Encoding'` at four
call sites in `Application.cpp` — a clear, unambiguous signal, since none of
those call sites were touched by this phase's own intended edits). Fixed by
re-reading the real file and restoring `#include "../Encoding/PngEncoder.h"`
plus reordering the block so `../Editor/Logger.h` sorts alphabetically
first among the `../`-prefixed includes. A parallel, structurally identical
mistake was caught and self-corrected the same way while editing
`Network/NetworkServer.cpp`'s include block (an accidental duplicate
`#include "../Application/FrameDebuggerCommandBridge.h"` combined with a
dropped `#include "../Application/FrameCaptureBridge.h"`) — again caught
before any compile check was run, by re-reading the file immediately after
the edit and noticing the duplicate line in the tool's own returned
context. Both are recorded here as process notes, not tool bugs: the
`edit_line` tool behaved exactly as documented (replacing the line at the
index given, and applying its own documented boundary-dedup heuristic) —
the actual mistake was supplying a stale line index after previous edits
had already shifted the file's line numbers. No `bug_report` was filed,
since nothing malfunctioned; this is simply a caution for whoever executes
`PHASE3`/`PHASE4`: always re-`read_line`/`search_in_dir` to re-confirm the
current line number immediately before every `edit_line` call on a file
that was already edited earlier in the same session.

## Verification

- Fast, targeted incremental compile check (default `GTE_ENABLE_EDITOR=ON`
  configuration):
  - `cmake --build build --target gte_core` — succeeded cleanly (after the
    self-correction above), including `Application.cpp.obj`,
    `JobContinuation.cpp.obj`, and `NetworkServer.cpp.obj` all recompiling
    with their new `#include`s and call sites.
  - `cmake --build build --target GreatTamanaEngineTests` — succeeded
    (no test changes in this phase, confirms no regression).
  - `cmake --build build --target GreatTamanaEngine` — succeeded, full
    executable relinked.
- Manual sanity check: launched the built `GreatTamanaEngine.exe` via
  `run_app_background`, confirmed via `GET /get_swapchain` that the engine
  starts and renders normally (Editor UI, Scene/Game panels, Hierarchy,
  Project panel all visible and correct) with all four new log call sites
  and the per-frame `SetCurrentFrame()` call now live in the running binary,
  then stopped it via `stop_app_background`. No endpoint/UI exists yet to
  directly observe the logged entries themselves (expected — `PHASE3`/
  `PHASE4`'s job), so this was a pure "does it still start and run
  correctly" check, per this phase's own Definition of Done.
- No full build and no full `ctest` regression run were performed, per this
  campaign's own working agreement (only `PHASE5` runs those).

## Deviations from the plan

**None**, beyond the mid-flight tool-usage self-correction documented above
(which left the final code exactly matching the phase document's plan — no
design or scope deviation). All four call sites use the exact wording,
placement, and anchoring the corrected Step 3.2 specifies; `SetCurrentFrame()`
is placed exactly where Step 3.1 specifies; no existing `fprintf` site was
touched, reordered, or reworded.

## Exact final wording/placement of each new call site (for PHASE5's benefit)

At engine startup (`GTE_ENABLE_EDITOR=ON`, `GTE_ENABLE_NETWORK=ON`, the
default configuration), the Logger buffer will contain, in this order,
starting from entry id 1 (assuming a successful bind on port 8080):

1. `category="Network"`, `level=Info`, `message="listening on 127.0.0.1:8080"`
   (logged from inside `NetworkServer::Start()`, called from
   `Application`'s constructor, itself called before the constructor's own
   final `GTE_LOG_INFO` line below).
2. `category="Application"`, `level=Info`, `message="GreatTamanaEngine started."`
   (logged at the very end of `Application::Application(...)`'s body).

If the bind fails instead (e.g. port 8080 already in use by another running
instance), entry 1 would instead be `category="Network"`, `level=Error`,
`message="failed to bind 127.0.0.1:8080 - network endpoint disabled this
run."`, still followed by the `"Application"`/`Info`/`"GreatTamanaEngine
started."` entry.

The `Jobs`/`Error` entry (`ScheduleAfter()`/`DispatchAfter()`'s
self-dependency diagnostic) only fires if a caller actually passes a
self-referential dependency — not part of normal startup; PHASE1's own
`LoggerTests.cpp` already exercises the underlying macro directly, so this
is not otherwise expected to appear during a normal smoke-test run.

Every entry from frame 0 onward carries the real, live
`m_engineContext.time.FrameCount()` value as of the most recent
`Application::Run()` loop iteration's `Time::Advance()` call — entries
logged during construction (both entries above) are stamped with frame `0`
(the Logger's own default `s_currentFrame` value before `SetCurrentFrame()`
is ever called for the first time in `Run()`).

## Next phase

`PHASE3_NETWORK_ENDPOINTS_GET_LOGS_AND_CLEAR_LOGS.md` can proceed:
`Logger::SetCurrentFrame()` is live and called once per real frame,
`Network/NetworkServer.cpp` already has `#include "../Editor/Logger.h"`
(added in this phase, reusable as-is), and a genuine, PROVEN
Network-background-thread log call site will exist naturally once
`GET /get_logs`/`POST /clear_logs`'s own route handlers exist on that
thread.
