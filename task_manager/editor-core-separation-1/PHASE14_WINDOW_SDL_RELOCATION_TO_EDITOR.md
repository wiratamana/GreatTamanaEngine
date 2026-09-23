# PHASE14 — Window/SDL Fully Out of `gte_core` (Checkpoint: FULL ctest required)

## Parent
`PHASE0_MASTER_STRATEGY.md`. Depends on Phases 10, 12, 13. **This is the
design doc's OTHER flagged highest-risk step, alongside Phase 9.**

## Step 1: The Goal

`Window.cpp`/`SdlContext` physically move to compile as part of
`gte_editor`'s own sources. `gte_core`'s `target_link_libraries()` drops
`SDL3::SDL3` entirely. `gte_core` builds standalone with no SDL3 include
path/library configured at all. Phase 1's regression baseline test flips
from "SDL3.dll IS required" to "SDL3.dll is NOT required" for the test
binary.

## Step 2: The Situation / The Problem

Read `src/Window/Window.h`/`.cpp` and wherever `SdlContext` is defined (find
via `search_in_dir` for `class SdlContext`) in full, current state. Confirm
today's `target_link_libraries(gte_core PUBLIC SDL3::SDL3)` line in
`CMakeLists.txt` (design doc Section 7.4 claims this exact shape — verify).

## Step 3: The Plan

1. Move `src/Window/Window.cpp` and the `SdlContext` file(s) out of
   `gte_core`'s `target_sources()` list and into `gte_editor`'s (in
   `CMakeLists.txt`). `Window.h` (the header, with its already-opaque
   `SDL_Window*`/Vulkan-handle-typedef trick, per Phase 10's confirmation)
   can stay declared wherever makes sense — but its `.cpp` (the only file
   that actually `#include`s real SDL headers) must physically compile as
   part of `gte_editor`.
2. Change `target_link_libraries(gte_core ...)` to drop `SDL3::SDL3`
   entirely. Add `target_link_libraries(gte_editor PUBLIC SDL3::SDL3)`
   instead (or `PRIVATE`, matching whatever visibility the rest of
   `gte_editor`'s own dependencies use — check `imgui`/`imguizmo`'s
   existing visibility for consistency).
3. Compile-check incrementally FIRST — this will likely surface any
   remaining `gte_core`-side file that still transitively needs an SDL
   symbol/header (e.g. if Phase 11's `ProfilingClock.cpp` fix or Phase 10's
   `ISurfaceProvider` work missed something). Fix any such leak found here
   directly — this is exactly the kind of leak Phases 10-11 exist to
   prevent, so finding one here means going back and confirming why it
   wasn't already caught.
4. Once incremental build is clean: run the SAME "one of two full
   checkpoints" requirement — FULL clean rebuild + FULL `ctest` regression
   pass. Record exact before/after test counts.
5. Re-run Phase 1's SDL-linkage regression test/probe SPECIFICALLY. It
   should now report the OPPOSITE of what it did in Phase 1 — the test
   binary no longer requires `SDL3.dll` to be staged next to it. Update
   that test's own assertion/expectation to match this NEW, fixed state
   (flip it from "confirms the bad state exists" to "confirms the bad
   state is now GONE") — do this in the SAME test file Phase 1 created,
   updating its own header comment to describe the "after" state instead of
   deleting/replacing it wholesale, so its git history tells the full
   before/after story.
6. Live smoke check: `run_app_background` the Editor executable itself
   (which still links `gte_editor` -> `gte_core`, so it still has SDL and
   still boots a real window) and confirm it boots/renders identically.
   Separately, confirm (via the full `ctest` run in step 4, plus a manual
   check if useful) that `GreatTamanaEngineTests.exe` itself no longer
   requires `SDL3.dll` present in its own directory to launch.

## Files Touched

- `CMakeLists.txt` (move `Window.cpp`/`SdlContext` source entries, flip
  `SDL3::SDL3` link visibility)
- `tests/Build/SdlLinkageRegressionTests.cpp` (from Phase 1 — update its
  expectation to the new, fixed state)
- Any incidental core-side file found leaking an SDL symbol in Step 3

## Definition of Done

- `gte_core`'s own `target_link_libraries()` no longer lists `SDL3::SDL3`
  anywhere.
- Full clean rebuild + full `ctest` pass succeed (Checkpoint 2).
- Phase 1's regression test now confirms the FIXED state, not the broken
  one.
- Editor executable still boots/renders identically.
- `PHASE14_COMPLETION_REPORT.md` (with before/after test counts AND the
  flipped SDL-linkage regression result) + git commit.

## Out of Scope

Do not build `EditorHost` yet (Phase 15) — `Application` is still the
composition root, just no longer the one compiling `Window.cpp` inside
`gte_core`'s own archive (it now reaches it via `gte_editor`'s transitive
link, since the executable itself already links `gte_editor` since Phase 9).
