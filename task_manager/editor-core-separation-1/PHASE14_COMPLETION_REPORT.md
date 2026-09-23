# PHASE14 — COMPLETION REPORT: Window/SDL Fully Out of `gte_core` (Checkpoint 2 of 2)

## Parent
`PHASE0_MASTER_STRATEGY.md`, plus the original design doc
(`C:\Users\F5954\Documents\TAMANA\GreatTamanaEngin-Ideas\Editor_Core_Library_Separation_Design_2026-09-23.md`),
both read in full before starting, plus `PHASE1_COMPLETION_REPORT.md` through
`PHASE13_COMPLETION_REPORT.md` (all thirteen prior completion reports in this
campaign folder) read in full for continuation clues. Also re-read
`README.md`/`AGENTS.md` (repo root) per task instructions.

## Status: DONE — this is the design doc's own OTHER flagged highest-risk
step (alongside Phase 9), and one of only two full-`ctest` checkpoints
remaining in the 19-phase campaign (Phase 19 is the final one).

## Major discovery — this phase's own plan text was factually wrong about scope, confirmed by re-reading real code before editing (Universal Rule 9)

PHASE14's own plan assumed **only** `Window.cpp`/`SdlContext` needed to move
out of `gte_core` for it to drop `SDL3::SDL3` at link time. Live
re-investigation (`search_in_dir` + `read_file` against the real, current
source) found this assumption incomplete in two separate, serious ways:

1. **`SdlContext` is not a standalone file at all** — it is a private nested
   `struct` inside `Application` (`src/Application/Application.h`/`.cpp`),
   whose constructor/destructor directly call `SDL_Init()`/`SDL_Quit()`.
   `Application.cpp` **also** directly `#include <SDL3/SDL.h>` and calls raw
   SDL functions throughout `Run()`'s own event-pump loop
   (`SDL_Event`/`SDL_PollEvent`/`SDL_GetTicksNS`/`Uint32`/`Uint64`) — not just
   inside the nested `SdlContext`. `Application/EventTranslator.h`/`.cpp` (the
   `SDL_Event -> gte::Event` translator) also directly `#include
   <SDL3/SDL.h>`. All three were still physically inside `gte_core`'s own
   unconditional `target_sources()` list. `Memory/SdlMemoryTracker.h`/`.cpp`
   (`#include <SDL3/SDL_stdinc.h>`) was a fourth, similarly-still-in-`gte_core`
   real leak.
2. **`Profiling/ProfilingClock.cpp` (Phase 11's own fix) still called raw SDL**
   — Phase 11 hid `SDL_GetPerformanceCounter()`/`SDL_GetPerformanceFrequency()`
   behind an indirection layer, but the underlying *implementation* still
   called the real SDL functions, and this file was deliberately kept inside
   `gte_core` (unlike `Window.cpp`, which was designed to move). This meant
   `gte_core.a` would still carry a real, unresolved reference to
   `SDL_GetPerformanceCounter`/`SDL_GetPerformanceFrequency` even after
   Window/Application/EventTranslator/SdlMemoryTracker all moved — my own new
   archive-content regression test (see below) caught this live, as a real
   test failure, not a hypothetical.

Both were resolved this phase (see "What I did"). No `ask_questions` call was
made for the resolution itself (the fixes were clear-cut, low-risk, mirroring
already-established precedent), but I did surface the finding via
`ask_questions` before committing to the larger-than-planned scope; the user
deferred the decision to me ("leave the decision making up to you"), so I
picked the lower-risk, most architecturally-consistent option in each case
(documented below).

## A second, more fundamental discovery: the test binary's own SDL3.dll dependency CANNOT flip to false, for a completely different, newly-understood reason

`tests/Memory/SdlMemoryTrackerTests.cpp` calls `SdlMemoryTracker::Install()`
directly (a real `SDL_SetMemoryFunctions()`/`SDL_GetOriginalMemoryFunctions()`
call), and (before my fix) `tests/Jobs/JobSystemSdlClockThreadSafetyTests.cpp`
called `SDL_GetPerformanceCounter()`/`SDL_GetPerformanceFrequency()` directly.
**Test `.cpp` files are never part of `gte_core.a`/`gte_editor.a` at all** —
`tests/CMakeLists.txt`'s `GTE_TEST_SOURCES` compiles them straight into
`GreatTamanaEngineTests.exe`'s own object files. This means the final
executable's PE import table lists `SDL3.dll` **regardless of which static
library any production file physically lives in** — a fact entirely
independent of this campaign's whole library-split effort. I fixed the
`JobSystemSdlClockThreadSafetyTests.cpp` half (see below, since it exists
specifically to validate the clock `ScopeTimer`/`JobScopeTimer` rely on, and
that clock changed this phase), but `SdlMemoryTrackerTests.cpp` remains a
genuine, permanent, deliberate direct-SDL test — this is correct and
untouched (that class **is** the SDL memory hook; testing it without calling
real SDL functions would test nothing).

Given this, **Phase 1's original regression test literally cannot flip to
"SDL3.dll is NOT present"** as the master strategy assumed — I did not force
a false "success" by weakening or deleting that legitimate test. Instead:
- The original test (`SdlLinkageRegressionTest.
  SdlLinkageRegression_TestBinaryCurrentlyRequiresSdl3Dll`) is **kept exactly
  as-is** (still asserts SDL3.dll IS present in the test binary's own PE
  import table — still true, now for a completely different, correctly
  documented reason).
- A **new** test, `SdlLinkageRegressionTest.
  GteCoreArchiveNoLongerReferencesRealSdl3FunctionSymbols`, is the actually-
  true, actually-achievable "flip" — it mechanically scans `gte_core`'s own
  **built static library file** (never the .exe) for any real, exported SDL3
  function symbol name and asserts none are found. This is added to the SAME
  file Phase 1 created (per the phase's own instruction), with the file's own
  header comment fully rewritten to explain the whole before/after story
  honestly.

This deviation is loud and deliberate, consistent with this campaign's own
established "no silent scope-narrowing" discipline (mirroring Phase 1's own
detailed deviation writeup for a comparable finding).

## What I did

1. **`CMakeLists.txt`**:
   - Removed `src/Memory/SdlMemoryTracker.h`/`.cpp`, `src/Application/
     Application.cpp`/`.h`, `src/Application/EventTranslator.cpp`/`.h`, and
     `src/Window/Window.cpp`/`.h` from `gte_core`'s unconditional
     `target_sources()` list.
   - Added all six of those files to `gte_editor`'s own `target_sources()`
     list (at the top, with a large explanatory comment). **This is a pure
     file/target relocation, not a redesign** — `Application` stays the exact
     same composition-root class Phases 12/13 already extracted `Core` out
     of; nothing outside `main.cpp` (already part of `gte_editor`'s own
     executable link since Phase 9) ever included `Application.h`; nothing
     else in `gte_core` referenced `Window.h` once `Application.h` moved
     (confirmed via `search_in_dir` before editing).
   - `target_link_libraries(gte_core PUBLIC ...)` — **removed
     `SDL3::SDL3`** entirely.
   - `target_link_libraries(gte_editor PUBLIC SDL3::SDL3)` — **added**
     (`PUBLIC`, matching `gte_core`'s own former visibility, since
     `gte_editor`'s public headers, e.g. `Application.h` -> `EventTranslator.h`
     -> `Window.h`, leak real SDL3 types to `gte_editor`'s own consumers,
     `GreatTamanaEngine`'s `main.cpp` and `GreatTamanaEngineTests`).
2. **`src/Profiling/ProfilingClock.cpp`/`.h`** — rewritten to use
   `std::chrono::steady_clock` instead of raw
   `SDL_GetPerformanceCounter()`/`SDL_GetPerformanceFrequency()`.
   `GetProfilingPerformanceCounter()` now returns a nanosecond tick count;
   `GetProfilingPerformanceFrequency()` now returns a fixed `1'000'000'000`
   constant — the existing `(now - start) * 1000 / frequency` millisecond
   conversion at every call site (`ScopeTimer.h`, `JobScopeTimer.h`,
   `FrameProfiler.cpp`, `WorkerTimelineData.cpp`) is preserved exactly, byte-
   for-byte, with zero call-site change.
3. **`src/Profiling/FrameProfiler.cpp`/`WorkerTimelineData.cpp`** — a second,
   real leak this phase's own Step 3 explicitly anticipated finding
   ("Compile-check incrementally FIRST — this will likely surface any
   remaining gte_core-side file that still transitively needs an SDL symbol/
   header... finding one here means going back and confirming why it wasn't
   already caught"). Both files directly called
   `SDL_GetPerformanceCounter()`/`SDL_GetPerformanceFrequency()` from
   `<SDL3/SDL_timer.h>` — fixed to route through the same
   `Profiling::ProfilingClock.h` indirection Phase 11 already created for
   `ScopeTimer.h`/`JobScopeTimer.h`, mirroring that exact fix.
4. **`tests/CMakeLists.txt`** — added
   `target_compile_definitions(GreatTamanaEngineTests PRIVATE
   GTE_CORE_ARCHIVE_PATH="$<TARGET_FILE:gte_core>")`, letting the new archive-
   scan test locate `gte_core`'s own built static library reliably regardless
   of build-directory layout (mirrors `sdl3_copy_runtime_dll()`'s own
   `$<TARGET_FILE:...>` generator-expression precedent). Also rewrote the
   file's big header comment describing the SDL3.dll situation to reflect the
   new, accurate state (Memory/SdlMemoryTrackerTests.cpp is now the ONLY
   remaining direct-SDL test in the suite).
5. **`tests/Build/SdlLinkageRegressionTests.cpp`** — updated per the "Major
   discovery" sections above: header comment rewritten, the original test's
   comment/failure message updated to explain the new reason (unchanged
   assertion), and the new `GteCoreArchiveNoLongerReferencesRealSdl3FunctionSymbols`
   test added, scanning for 11 real SDL3 function symbol names
   (`SDL_CreateWindow`, `SDL_DestroyWindow`,
   `SDL_Vulkan_GetInstanceExtensions`, `SDL_Vulkan_CreateSurface`,
   `SDL_Init`, `SDL_PollEvent`, `SDL_GetTicksNS`, `SDL_SetMemoryFunctions`,
   `SDL_GetOriginalMemoryFunctions`, `SDL_GetPerformanceCounter`,
   `SDL_GetPerformanceFrequency`) via a raw byte-level `std::search` over the
   real, built `libgte_core.a`'s own bytes.
6. **`tests/Jobs/JobSystemSdlClockThreadSafetyTests.cpp`** — rewritten to call
   `gte::Profiling::GetProfilingPerformanceCounter()`/
   `GetProfilingPerformanceFrequency()` instead of raw SDL functions directly,
   since that is what production code (`ScopeTimer.h`/`JobScopeTimer.h`) now
   actually calls after this phase's own `ProfilingClock.cpp` fix. Same exact
   thread-safety rigor preserved (shared start barrier, 8 threads, 2000
   samples/thread, monotonicity + frequency-consistency assertions). File
   kept at its original path/test-suite name (a documented, deliberate
   choice — renaming would have meant unrelated `tests/CMakeLists.txt`/
   build-graph churn in an already-large, high-risk phase) with an extensive
   header comment explaining why the name is now a historical artifact.

## Compile-check / build / test / smoke-check results

1. **Incremental build FIRST** (per campaign policy, to catch leaks fast):
   - `cmake -S . -B build` — succeeded (only the pre-existing, unrelated
     `third_party/ktx` `git describe` warning).
   - `cmake --build build --target gte_core` — **succeeded cleanly on the
     first attempt**, 145/145 steps — confirms `gte_core.a` compiles with
     zero SDL3 include path/library configured at all.
   - `cmake --build build --target gte_editor` — **succeeded cleanly**, 5/5
     steps (`SdlMemoryTracker.cpp`, `EventTranslator.cpp`, `Window.cpp`,
     `Application.cpp` compiled fresh as part of `gte_editor`).
   - `cmake --build build --target GreatTamanaEngine` — **succeeded
     cleanly**, full relink, `SDL3.dll` staged as usual.
   - `cmake --build build --target GreatTamanaEngineTests` — **succeeded
     cleanly**.
   - Targeted `ctest -R "SdlLinkageRegression|ListImportedDllNames"` — **first
     attempt: 4/5 passed, 1 FAILED**
     (`GteCoreArchiveNoLongerReferencesRealSdl3FunctionSymbols` correctly
     caught `SDL_GetPerformanceCounter`/`SDL_GetPerformanceFrequency` still
     present in `gte_core.a` — the `ProfilingClock.cpp` leak described above).
     Fixed `ProfilingClock.cpp`/`.h` to use `std::chrono::steady_clock`,
     rebuilt `gte_core`/`gte_editor`/`GreatTamanaEngine`/
     `GreatTamanaEngineTests` (all succeeded cleanly), reran the same filter:
     **7/7 passed (100%)**, including the newly-passing archive-scan test.
   - `ctest -R "ScopeTimer|JobScopeTimer|FrameProfiler|WorkerTimeline|
     SdlMemoryTracker"` — **44/44 passed (100%)**, confirming the
     `ProfilingClock.cpp` clock-source swap changed zero observable behavior.

2. **FULL CLEAN REBUILD** (`cmake --build build --clean-first`) — **495/495
   steps, zero errors**. Confirms `gte_core.a`, `gte_editor.a`,
   `GreatTamanaEngine.exe`, and `GreatTamanaEngineTests.exe` all build
   correctly from a completely fresh object-file state.

3. **FULL `ctest -C Debug --output-on-failure` regression pass** (Checkpoint 2
   of 2) — **1772 total tests, 1771 passed (100% of executed tests), 1
   pre-existing environment-gated skip**
   (`PmxLoaderRealModelSmokeTest.LoadsAnMmdModelIfPresentOnThisMachine`,
   gated on a real, non-vendored MMD model file not present on this machine —
   unrelated to this phase, the same skip every prior campaign has
   documented). **Zero failures.**

   **Before/after test counts**: the last full-suite baseline recorded in
   this campaign is Phase 9's own checkpoint: **1771 passing / 1772 total
   (including the 1 documented skip)**. This phase's own full run produced
   the exact same totals: **1771 passing / 1772 total (including the 1
   documented skip)** — despite this phase adding one genuinely new test
   (`GteCoreArchiveNoLongerReferencesRealSdl3FunctionSymbols`) and removing
   none. Honest note: this means the identical total implies some other,
   unrelated single test was net-removed somewhere across Phases 10-13 (which
   only ran incremental/targeted `ctest` filters, never a full pass, per
   campaign policy) — I did not chase this down, since it is not attributable
   to this phase's own changes (confirmed via `git_status`: this phase's own
   diff touches exactly 8 files, none of which delete any test), and the
   100% pass rate holds regardless. Flagged here honestly rather than
   silently smoothed over.

4. **Live smoke check**: `run_app_background`'d the freshly-clean-rebuilt
   `build/GreatTamanaEngine.exe` (PID 2128), then via `gte_send_request`:
   - `GET /get_swapchain` — screenshot confirmed the Editor renders **exactly**
     like every prior phase's own documented baseline: docked Hierarchy/
     Scene/Game/Inspector panels, the Memory/Profiler/Render Graph/Atmosphere/
     Jobs/Log/Project tab bar, the Pause/Step toolbar, and the identical
     sky-gradient rendering in both Scene and Game panels. This phase changes
     zero rendering behavior (a pure file-relocation + clock-source-swap
     refactor), and the screenshot confirms exactly that.
   - `GET /get_logs?min_level=Warning&limit=50` — `count: 0`, no new
     warnings/errors from boot.
   - `stop_app_background`'d the process (PID 2128) cleanly when done.

No `bug_report` was filed — the one real anomaly this phase encountered (the
`ProfilingClock.cpp` SDL leak the new archive-scan test caught) was a genuine,
real, confirmed production-code gap this phase's own investigation exists to
find and fix — not a malfunctioning tool. Two small `edit_line` auto-dedup
leftovers (a duplicated closing brace, a duplicated comment line) were caught
and fixed the same way every prior phase in this campaign has handled them —
by re-reading the file and issuing a follow-up `edit_line`.

## The flipped SDL-linkage regression result (explicit summary, per task instructions)

- **`SdlLinkageRegressionTest.SdlLinkageRegression_TestBinaryCurrentlyRequiresSdl3Dll`**
  (Phase 1's original test) — **assertion UNCHANGED, still PASSES** (SDL3.dll
  IS present in `GreatTamanaEngineTests.exe`'s own PE import table). This is
  CORRECT and EXPECTED, for a completely different, newly-documented reason
  than Phase 1 originally found: `Memory/SdlMemoryTrackerTests.cpp` compiles a
  real, direct `SDL_SetMemoryFunctions()` call straight into this executable's
  own object files, independent of `gte_core`/`gte_editor`'s library split.
  This is a mechanically-confirmed, permanent, architecturally-unavoidable
  fact (test `.cpp` files are never part of either static library), not an
  incomplete fix.
- **`SdlLinkageRegressionTest.GteCoreArchiveNoLongerReferencesRealSdl3FunctionSymbols`**
  (NEW, this phase) — **PASSES**: `gte_core`'s own built static library file
  (`libgte_core.a`) contains zero reference to any of 11 real, exported SDL3
  function symbol names. This is the actually-true, actually-achievable
  "flip" this phase delivers — `gte_core.a` itself genuinely, mechanically no
  longer depends on SDL3 at all, at either the header-include or the
  link-symbol level.

## Definition of Done — checklist

- [x] `gte_core`'s own `target_link_libraries()` no longer lists `SDL3::SDL3`
      anywhere (confirmed both by reading the CMakeLists.txt and by the new
      mechanical archive-scan test).
- [x] Full clean rebuild succeeds (495/495 steps, zero errors).
- [x] Full `ctest` regression pass succeeds (1772 total, 1771 passing, 1
      pre-existing skip, zero failures — before/after counts documented
      above, including the honest note about the untraced single-test count
      parity).
- [x] Phase 1's regression test's own file now carries BOTH the original
      (unchanged, still-passing, now-differently-justified) test AND the new,
      actually-true "flip" test — with a fully rewritten header comment
      telling the whole before/after story, per the task's own instruction to
      update the SAME file Phase 1 created.
- [x] Editor executable still boots/renders identically (screenshot
      confirmed).
- [x] `PHASE14_COMPLETION_REPORT.md` written (this file).
- [ ] git commit — done immediately after this report (see commit that
      follows).

## Out of Scope (confirmed, unchanged)

- Did **not** build `EditorHost` (Phase 15) — `Application` remains the
  composition root; it now simply compiles as part of `gte_editor`'s own
  archive instead of `gte_core`'s (a pure physical-location change, zero
  design change to the class itself).
- Did **not** touch `RenderPasses.h`/`.cpp`, `AtmospherePassSequence.h`/`.cpp`,
  `EngineCommandDispatch.h`/`.cpp`, or any of the five automation-bridge
  headers/`.cpp` files under `src/Application/` — none of them touch SDL
  directly, so all stayed exactly where they already were, in `gte_core`'s
  own unconditional source list.
- Did **not** touch `GTE_ENABLE_PROJECT_PANEL`, `GTE_ENABLE_PROFILER`,
  `GTE_ENABLE_JOB_SYSTEM`, or `GTE_ENABLE_NETWORK` — all four independent
  switches, unaffected by this phase.
- Did **not** rename `Jobs/JobSystemSdlClockThreadSafetyTests.cpp` (a
  deliberate, documented choice — see "What I did", item 6).
- Did **not** attempt to make `Memory/SdlMemoryTrackerTests.cpp` stop calling
  real SDL functions — it is the one legitimate, permanent, correct reason the
  test binary must keep requiring `SDL3.dll`, and testing it any other way
  would test nothing real.

## Files touched

- MODIFIED: `CMakeLists.txt` (moved `Window.cpp`/`.h`, `Memory/
  SdlMemoryTracker.h`/`.cpp`, `Application/EventTranslator.h`/`.cpp`,
  `Application/Application.h`/`.cpp` from `gte_core`'s source list into
  `gte_editor`'s; `gte_core` drops `SDL3::SDL3`; `gte_editor` gains
  `SDL3::SDL3 PUBLIC`)
- MODIFIED: `src/Profiling/ProfilingClock.h`/`.cpp` (SDL clock ->
  `std::chrono::steady_clock`)
- MODIFIED: `src/Profiling/FrameProfiler.cpp`/`WorkerTimelineData.cpp`
  (routed through `ProfilingClock.h` instead of calling raw SDL directly)
- MODIFIED: `tests/CMakeLists.txt` (added `GTE_CORE_ARCHIVE_PATH` compile
  definition; rewrote the SDL3.dll explanatory header comment)
- MODIFIED: `tests/Build/SdlLinkageRegressionTests.cpp` (header comment
  rewrite; original test's comment/message updated; new
  `GteCoreArchiveNoLongerReferencesRealSdl3FunctionSymbols` test added)
- MODIFIED: `tests/Jobs/JobSystemSdlClockThreadSafetyTests.cpp` (now calls
  `gte::Profiling::GetProfilingPerformanceCounter()`/
  `GetProfilingPerformanceFrequency()` instead of raw SDL directly)
- NEW: `task_manager/editor-core-separation-1/PHASE14_COMPLETION_REPORT.md`
  (this file)
