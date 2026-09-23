# PHASE11 — Fix the Profiling Module's SDL Clock Leak

## Parent
`PHASE0_MASTER_STRATEGY.md`. Depends on Phase 10 (independent of it in
practice, but sequenced here per the design doc's own Phase 5 grouping).

## Step 1: The Goal

`src/Profiling/ScopeTimer.h` and `JobScopeTimer.h` currently call
`SDL_GetPerformanceCounter()`/`SDL_GetPerformanceFrequency()` directly from
`<SDL3/SDL_timer.h>`, gated behind `#if GTE_PROFILE_SCOPE` (default ON — a
separate, independent switch from `GTE_ENABLE_EDITOR`, untouched by this
campaign). This transitively pulls an SDL header into ANY `gte_core`
translation unit using `GTE_PROFILE_SCOPE`/`GTE_PROFILE_JOB_SCOPE`. Hide
this behind a tiny internal clock function in a `.cpp`, mirroring
`Window.cpp`'s own trick of never exposing SDL types in a header.

## Step 2: The Situation / The Problem

Read `src/Profiling/ScopeTimer.h` and `JobScopeTimer.h` in full, current
state. Confirm exactly where `SDL_GetPerformanceCounter()`/
`SDL_GetPerformanceFrequency()` are called and whether they're called from
inline header code (the actual problem) or already from a `.cpp` (in which
case this phase may already be moot — verify, don't assume the design doc's
claim is still accurate).

## Step 3: The Plan

1. If confirmed to be a real header-level SDL include: create
   `src/Profiling/ProfilingClock.h`/`.cpp` (new) exposing:
   ```cpp
   namespace gte {
   std::uint64_t GetProfilingPerformanceCounter() noexcept;
   std::uint64_t GetProfilingPerformanceFrequency() noexcept;
   }
   ```
   with the `.cpp` (and ONLY the `.cpp`) including `<SDL3/SDL_timer.h>` and
   forwarding to `SDL_GetPerformanceCounter()`/`SDL_GetPerformanceFrequency()`.
2. Update `ScopeTimer.h`/`JobScopeTimer.h` to call these new functions
   instead of the raw SDL functions directly, removing
   `#include <SDL3/SDL_timer.h>` from both headers entirely.
3. Confirm via `search_in_dir` for `SDL3/SDL_timer.h` and
   `SDL_GetPerformanceCounter`/`SDL_GetPerformanceFrequency` across
   `src/Profiling/` — after this phase, only `ProfilingClock.cpp` should
   reference the raw SDL calls.
4. Compile-check: incremental build. Confirm `GTE_PROFILE_SCOPE`/
   `GTE_PROFILE_JOB_SCOPE` call sites elsewhere in the engine still compile
   unmodified (this is a purely internal implementation-hiding refactor,
   the macro's own public call-site syntax must not change at all).
5. Live smoke check: `run_app_background`, open the Editor's Profiler
   panel via `gte_send_request`, confirm CPU scope timings still populate
   correctly (proves the clock functions still return real, correctly-
   scaled values, not just that it compiles).

## Files Touched

- NEW `src/Profiling/ProfilingClock.h`/`.cpp`
- `src/Profiling/ScopeTimer.h`
- `src/Profiling/JobScopeTimer.h`

## Definition of Done

- `ScopeTimer.h`/`JobScopeTimer.h` contain zero `#include` of any SDL
  header.
- Profiler panel confirmed still showing correct live timing data.
- `PHASE11_COMPLETION_REPORT.md` + git commit.

## Out of Scope

Do not touch `GTE_ENABLE_PROFILER` itself (independent switch, out of
scope for this whole campaign per `PHASE0`'s Non-Goals).
