# PHASE11 — COMPLETION REPORT: Fix the Profiling Module's SDL Clock Leak

## Parent
`PHASE0_MASTER_STRATEGY.md`, plus the original design doc
(`C:\Users\F5954\Documents\TAMANA\GreatTamanaEngin-Ideas\Editor_Core_Library_Separation_Design_2026-09-23.md`),
both read in full before starting, plus `PHASE1_COMPLETION_REPORT.md` through
`PHASE10_COMPLETION_REPORT.md` (all ten prior completion reports in this
campaign folder) read in full for continuation clues. Also re-read
`README.md`/`AGENTS.md` (repo root) per task instructions.

## Status: DONE

## Step 2 verification result (do not assume, verify)

Re-confirmed via `read_file`/`search_in_dir` against the real, current source
— the design doc's claim was NOT stale:

- `src/Profiling/ScopeTimer.h` — `#include <SDL3/SDL_timer.h>` at the top
  (gated `#if GTE_ENABLE_PROFILER`), and the `ScopeTimer` class itself (a
  header-only, fully-inline class — constructor/destructor bodies both live
  in the header) called `SDL_GetPerformanceCounter()` (constructor, line 53)
  and `SDL_GetPerformanceCounter()`/`SDL_GetPerformanceFrequency()`
  (destructor, lines 62-63) directly, inline, in the header. This is a real,
  live header-level SDL leak, not something already hidden in a `.cpp`.
- `src/Profiling/JobScopeTimer.h` — identical shape: `#include
  <SDL3/SDL_timer.h>` at the top, `JobScopeTimer`'s constructor (line 86) and
  destructor (lines 94-95) both call the same two raw SDL functions inline in
  the header.
- Confirmed via `search_in_dir` for `SDL_GetPerformance`/`SDL3/SDL_timer.h`
  across all of `src/Profiling/` that these were the ONLY two files in the
  module calling these functions from header-level inline code —
  `FrameProfiler.cpp` and `WorkerTimelineData.cpp` also call
  `SDL_GetPerformanceCounter()`/`SDL_GetPerformanceFrequency()`, but both
  already do so from a `.cpp` file (not a header), so they were never part of
  this phase's actual problem and were correctly left untouched, per the
  phase's own Step 2 instruction ("Confirm exactly where... are called and
  whether they're called from inline header code... or already from a
  `.cpp`"). `ProfilingTypes.h` only mentions `SDL_GetPerformanceCounter()` in
  a comment, never in real code — also correctly left untouched.
- Confirmed via `search_in_dir` for `src/Profiling` in `CMakeLists.txt` that
  every one of these files (including `ScopeTimer.h`/`JobScopeTimer.h`) sits
  in `gte_core`'s own unconditional `target_sources()` list (lines 573-581 of
  the pre-edit file) — confirming this was a genuine, live `gte_core`-side SDL
  header leak, not a hypothetical one.

## What I did

1. **NEW `src/Profiling/ProfilingClock.h`** — declares
   `gte::Profiling::GetProfilingPerformanceCounter()`/
   `GetProfilingPerformanceFrequency()`, both `noexcept`, returning
   `std::uint64_t`. This header itself includes zero SDL headers — only
   `<cstdint>`.
2. **NEW `src/Profiling/ProfilingClock.cpp`** — the ONLY file in
   `src/Profiling/` that now includes `<SDL3/SDL_timer.h>` for the purpose of
   an inline-header-visible call; its two functions are one-line forwarders
   to the real `SDL_GetPerformanceCounter()`/`SDL_GetPerformanceFrequency()`.
   Mirrors `Window.cpp`'s own established trick (hide the real SDL call
   behind a function whose declaration lives in a header with no SDL
   `#include` at all, and whose definition — the only place the SDL header is
   ever included — lives in a `.cpp`).
3. **`src/Profiling/ScopeTimer.h`** — added `#include "ProfilingClock.h"`,
   removed `#include <SDL3/SDL_timer.h>` entirely (the `#if
   GTE_ENABLE_PROFILER` block now only guards `#include <cstdint>`).
   Constructor/destructor bodies now call
   `GetProfilingPerformanceCounter()`/`GetProfilingPerformanceFrequency()`
   (unqualified — both call sites are already inside `namespace
   gte::Profiling { ... }`) instead of the raw SDL functions. Zero other
   change — the macro's own public call-site syntax (`GTE_PROFILE_SCOPE(name)`)
   is completely unchanged, and the `#else`/`GTE_ENABLE_PROFILER=OFF` branch
   was untouched (it never referenced SDL at all).
4. **`src/Profiling/JobScopeTimer.h`** — identical treatment: added
   `#include "ProfilingClock.h"`, removed `#include <SDL3/SDL_timer.h>`,
   constructor/destructor now call the same two new functions instead of the
   raw SDL ones. `GTE_PROFILE_JOB_SCOPE(name)`'s own public call-site syntax
   is unchanged.
5. **`CMakeLists.txt`** — registered `src/Profiling/ProfilingClock.h`/`.cpp`
   in `gte_core`'s unconditional `target_sources()` list, immediately after
   `src/Profiling/ProfilingTypes.h` and before `src/Profiling/FrameProfiler.h`
   (keeping the existing file-group ordering/readability).
6. Confirmed via a fresh `search_in_dir` pass (post-edit) that
   `SDL_GetPerformanceCounter`/`SDL_GetPerformanceFrequency`/
   `SDL3/SDL_timer.h` now appear in exactly the expected 3 files across
   `src/Profiling/` (`FrameProfiler.cpp`, `WorkerTimelineData.cpp` — both
   pre-existing, already-safe `.cpp`-only call sites, untouched by this
   phase — and the new `ProfilingClock.cpp`/`.h`), and that `ScopeTimer.h`/
   `JobScopeTimer.h` themselves contain zero occurrence of either.

## Compile-check / smoke-check results

**Incremental compile check only, per campaign policy** — no full clean
build, no full `ctest` regression pass (not required until Phase 14/19).

- `cmake --build build --target gte_core` — **succeeded cleanly** (a CMake
  re-run picked up the two new source files, then an 8-step incremental
  build: `ProfilingClock.cpp` compiled fresh, `RenderSystem.cpp`/
  `PhysicsSystem.cpp`/`Game.cpp`/`AnimationSystem.cpp`/`Application.cpp`
  recompiled as transitive includers of `ScopeTimer.h`/`JobScopeTimer.h`,
  `libgte_core.a` relinked cleanly). Only the pre-existing, unrelated
  `third_party/ktx` `git describe` warning appeared (same as every prior
  phase's own report).
- `cmake --build build --target gte_editor` — **no work to do** (correctly
  confirms `gte_editor`'s own sources don't need to rebuild against this
  purely `gte_core`-internal change — no signature/ABI-visible change to
  anything `gte_editor` includes).
- `cmake --build build --target GreatTamanaEngine` — **succeeded cleanly**
  (executable relinked, every `.spv` shader + `SDL3.dll` staged as usual).
- `cmake --build build --target GreatTamanaEngineTests` — **succeeded
  cleanly** (`Profiling/ScopeTimerTests.cpp`/`Profiling/JobScopeTimerTests.cpp`
  recompiled as direct includers of the changed headers, test binary
  relinked).
- `ctest -R "ScopeTimer|JobScopeTimer|FrameProfiler|WorkerTimeline"` —
  **37/37 passed (100%)**, confirming every existing Tier-1 test for the
  Profiling module (including the exact `ScopeTimerTest`/`JobScopeTimerTest`
  suites whose class-under-test this phase modified) still passes unmodified
  — this phase changed zero test-observable behavior, only where the clock
  read physically happens.
- Live smoke check: `run_app_background`'d `build/GreatTamanaEngine.exe`
  (PID 11464), then via `gte_send_request`:
  - `GET /activate_tab?name=Profiler` — brought the "Profiler" tab to the
    front, confirming the Editor UI command bridge still works.
  - `GET /get_swapchain` — screenshot confirmed the **Profiler panel shows
    live, real data**: a populated "CPU Frame Time" graph (30.37 ms / 33 FPS,
    a real min/max range of 9.07-48.59 ms across the visible history), and a
    populated "CPU Scopes" table (`Renderer::PresentViaRenderGraph` 19.75 ms,
    `RenderGraph::Execute(Offscreen)` 9.82 ms, `IEditorLayer::BuildUI` 0.60 ms,
    each with a real, non-zero `Calls` count) — this is direct, positive proof
    the new `GetProfilingPerformanceCounter()`/`GetProfilingPerformanceFrequency()`
    indirection still returns real, correctly-scaled clock values end-to-end
    (a broken/always-zero clock would show a flat 0 ms graph and empty/zero
    scope table, not this).
  - `GET /get_logs?min_level=Warning&limit=50` — `count: 0`, no new
    warnings/errors from boot.
  - `stop_app_background`'d the process (PID 11464) when done.

No `bug_report` was filed — every tool call behaved as expected this phase;
no anomaly was encountered.

## Definition of Done — checklist

- [x] `ScopeTimer.h`/`JobScopeTimer.h` contain zero `#include` of any SDL
      header (confirmed via `search_in_dir`, post-edit).
- [x] Profiler panel confirmed still showing correct live timing data (see
      screenshot evidence above).
- [x] `PHASE11_COMPLETION_REPORT.md` written (this file).
- [ ] git commit — done immediately after this report (see commit that
      follows).

## Deviations from the phase plan

None. The phase's own plan (Step 3) was executed exactly as written — this
was a genuinely real, live leak (not moot), so every numbered step applied.

## Out of Scope (confirmed, unchanged)

- Did **not** touch `GTE_ENABLE_PROFILER` itself — the independent switch
  stays exactly as it is, out of scope for this whole campaign per `PHASE0`'s
  Non-Goals.
- Did **not** touch `FrameProfiler.cpp`/`WorkerTimelineData.cpp`'s own
  pre-existing, already-`.cpp`-only `SDL_GetPerformanceCounter()`/
  `SDL_GetPerformanceFrequency()` calls — they were never a header-level leak
  in the first place (confirmed above), so there was nothing for this phase
  to fix there. They keep calling the raw SDL functions directly, which is
  fine: they are `.cpp` translation units, not headers other `gte_core` files
  `#include`, so they never leaked `<SDL3/SDL_timer.h>` into anything.
- Did **not** touch `ProfilingTypes.h`'s own comment-only mentions of
  `SDL_GetPerformanceCounter()` — pure documentation text, not compiled code.

## Files touched

- NEW: `src/Profiling/ProfilingClock.h`
- NEW: `src/Profiling/ProfilingClock.cpp`
- MODIFIED: `src/Profiling/ScopeTimer.h` (`#include` swap; two clock-read call
  sites redirected to the new functions; zero other change)
- MODIFIED: `src/Profiling/JobScopeTimer.h` (identical treatment)
- MODIFIED: `CMakeLists.txt` (registered the two new files in `gte_core`'s
  unconditional source list)
- NEW: `task_manager/editor-core-separation-1/PHASE11_COMPLETION_REPORT.md`
  (this file)
