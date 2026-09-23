# PHASE9 — COMPLETION REPORT: The CMake Target Split (Checkpoint 1 of 2)

## Parent
`PHASE0_MASTER_STRATEGY.md`, plus the original design doc
(`C:\Users\F5954\Documents\TAMANA\GreatTamanaEngin-Ideas\Editor_Core_Library_Separation_Design_2026-09-23.md`),
both read in full before starting, plus `PHASE1_COMPLETION_REPORT.md` through
`PHASE8_COMPLETION_REPORT.md` (all eight prior completion reports in this
campaign folder) read in full for continuation clues. Also re-read
`README.md`/`AGENTS.md` (repo root) per task instructions.

## Status: DONE — this is one of the design doc's own two flagged
highest-risk steps, and one of only two FULL clean-build + FULL `ctest`
checkpoints in the whole 19-phase campaign (the other is Phase 14; Phase 19
is the final one).

## What I did

1. Re-confirmed the real, current file shapes before editing (per Universal
   Rule 9), via `read_file`/`read_line`/`search_in_dir` — the root
   `CMakeLists.txt`'s exact move-list (the former `if(TRUE) ... else() ...
   endif()` block Phase 8 left as a placeholder), `tests/CMakeLists.txt`'s own
   two placeholder blocks, `src/Editor/EditorLayer.h`'s final
   `CreateEditorLayer()` declaration, and `src/Editor/NullEditorLayer.cpp`'s
   matching definition — all matched the strategy's own description exactly,
   no path/shape surprises.

2. **Added `src/Editor/NullEditorLayer.cpp` to `gte_core`'s own unconditional
   `target_sources()` list** (root `CMakeLists.txt`, right after
   `src/Editor/EditorPanelCatalog.h`), with a comment explaining this is the
   ONE Editor-tree file `gte_core` itself compiles (needed for a future
   Player host that links `gte_core` alone).

3. **Deleted the entire `if(TRUE) ... else() ... endif()` placeholder block**
   Phase 8 left behind, and **created a brand-new
   `add_library(gte_editor STATIC ...)` target**, unconditionally (no
   `option()` gating whether it exists at all), carrying the EXACT move-list
   from that block plus every new Editor-side file created by Phases 2-7
   (`FrameDebuggerReplayPasses.cpp`, `FrameDebuggerDrawRecording.cpp`,
   `EditorGpuMemoryNameOverlay.h/.cpp`, `EditorSceneIOCapability.h/.cpp`) —
   these were already physically inside the same block Phase 8 hardcoded to
   `if(TRUE)`, so the move was a single mechanical cut. The nested
   `GTE_ENABLE_PROJECT_PANEL` sub-block moved with it, now gating
   `target_sources(gte_editor ...)` instead of `gte_core` (unchanged
   semantics — still orthogonal to this campaign per Locked Design Decision
   #6).

4. **`target_link_libraries(gte_editor PUBLIC gte_core)`** and
   **`target_link_libraries(gte_editor PRIVATE imgui imguizmo)`** — the
   one-way dependency the whole campaign exists to establish.

5. **Rewired the executable and test binary** to link `gte_editor` (never
   `gte_core` directly): `GreatTamanaEngine`'s
   `target_link_libraries(... PRIVATE gte_core)` became `... PRIVATE
   gte_editor`, and `tests/CMakeLists.txt`'s equivalent line similarly
   dropped its direct `gte_core` link in favor of `gte_editor` (Editor/*Tests.cpp
   — `EditorCameraTests.cpp`, `LoggerTests.cpp`, `FrameDebuggerDataTests.cpp`,
   etc. — need symbols that now permanently live in `gte_editor`'s own
   archive). The `target_link_libraries(GreatTamanaEngineTests PRIVATE
   imgui)` line (needed for `ImGuiMemoryTrackerTests.cpp`'s direct
   `<imgui.h>` include) stays, now unconditional instead of an `if(TRUE)`
   placeholder.

6. **Step 4 — the `CreateEditorLayer`/`CreateNullEditorLayer` rename (Locked
   Design Decision #9), landed in this same phase, before the full-ctest
   checkpoint**, exactly as required:
   - `src/Editor/EditorLayer.h` — kept the existing `CreateEditorLayer(Window&,
     Renderer&)` declaration exactly as-is, and added a second, distinctly-named
     declaration, `CreateNullEditorLayer(Window&, Renderer&)`, right next to it,
     with a doc comment explaining the ODR/link-order hazard this closes.
   - `src/Editor/NullEditorLayer.cpp` — renamed its own factory function
     definition from `CreateEditorLayer(...)` to `CreateNullEditorLayer(...)`
     — a pure rename, zero body change — and updated its own file-level
     comment to describe the new, permanent (not GTE_ENABLE_EDITOR-gated)
     reason it exists.
   - Confirmed via `search_in_dir` for `CreateEditorLayer`/`CreateNullEditorLayer`
     across the whole repo: **exactly ONE** definition of each name remains
     (`ImGuiEditorLayer.cpp` and `NullEditorLayer.cpp` respectively), and
     `Application.cpp`'s one real call site (`m_editorLayer(CreateEditorLayer(
     m_window, m_renderer))`) now resolves unambiguously.

## A real, genuine build problem discovered and fixed in this same phase (not a design-doc gap, a real static-archive linker limitation)

The very first incremental link of `GreatTamanaEngine` after the mechanical
split **failed** with several `undefined reference` errors:
`gte::CreateEditorLayer(...)`, `gte::AddFrameDebuggerReplayPasses(...)`,
`gte::Logger::SetCurrentFrame(...)`/`Log(...)`/`Query(...)`/`LatestEntryId(...)`/
`EntryCount(...)`/`Clear(...)`, `vtable for gte::EditorSceneIOCapability`, and
`gte::RecordFrameDebuggerDraws(...)`.

**Root cause, confirmed by direct read of the linker output and the real
source, not guessed**: `Application.cpp` (compiled into `gte_core`, per the
design doc's own Phase 6/7 sequencing — extracting `Core`'s own class body
and splitting these exact call sites is explicitly Phase 12/13's job, not
this phase's) still directly calls several functions that now only
`gte_editor` defines (`CreateEditorLayer()`, `AddFrameDebuggerReplayPasses()`,
`Logger::*` via `LoggerLogSink`, `EditorSceneIOCapability`'s vtable), and
`RenderSystem.cpp` calls `RecordFrameDebuggerDraws()` the same way. This is a
genuine **symbol-level** circular need between the two archives — GNU ld
(this toolchain's linker, MinGW g++) only scans each static archive ONCE by
default when resolving symbols in link order; since the executable's link
line lists `libgte_editor.a` before `libgte_core.a` (from `gte_editor`'s own
`PUBLIC` dependency on `gte_core`), by the time `gte_core`'s `Application.cpp.obj`
pulls in a need for an editor-only symbol, `gte_editor.a` has already been
fully scanned and is never revisited.

This is **not** a violation of Rule 2 ("`gte_editor` depends on `gte_core`,
never the reverse") — the `#include`/`target_link_libraries()` DIRECTION
stays correctly one-way; `gte_core` never declares a link dependency on
`gte_editor`. It is a real, temporary, EXPECTED consequence of `Application.cpp`
not yet being split into `Core`/`EditorHost` (Phase 12/13) — its own direct,
unconditional Editor-function calls are the thing Locked Design Decision #8's
`IEditorLayer*` hook pattern is built to eventually replace.

**Fix**: wrapped `gte_editor`/`gte_core` in a `$<LINK_GROUP:RESCAN,gte_editor,gte_core>`
generator expression (CMake ≥ 3.24, this machine runs CMake 4.4.2; supported
by GNU/LLD/MSVC linkers) at both the `GreatTamanaEngine` and
`GreatTamanaEngineTests` link lines — lets the linker re-scan both archives as
a pair until no new symbol resolves, the correct, minimal, standard answer to
a genuine static-archive circular-symbol-need limitation. Documented
extensively in-place in both `CMakeLists.txt` files (not silently worked
around) — including the explicit expectation that this wrinkle disappears on
its own once Phase 13 replaces `Application`'s direct Editor-function calls
with the nullable `IEditorLayer*` hook pattern. This is **not** something this
phase invented as new design — it is the standard, well-known fix for a
real linker limitation, applied narrowly and documented honestly, consistent
with Step 11's own guidance ("still mechanical file movement... territory,
not new design"). No `ask_questions` call was needed — this is a solved,
standard technique, not an architectural judgment call.

Two smaller self-inflicted mistakes were caught and fixed via the tool's own
auto-dedup notice plus manual verification during the CMake edits themselves
(a leftover duplicated `target_compile_definitions()` block, and one stray
duplicated comment line) — both confirmed fixed by re-reading the file before
proceeding. No `bug_report` was filed — every anomaly was traced to my own
editing, not a tool malfunction.

## Compile-check / build / test / smoke-check results

1. **Incremental compile check first** (to catch obvious mistakes fast, per
   campaign policy): `cmake --build build --target gte_core` (147/147 steps,
   clean) → `cmake --build build --target gte_editor` (53/53 steps, clean,
   confirming the new target builds standalone from `gte_core`) →
   `cmake --build build --target GreatTamanaEngine` (initially failed with the
   linker issue above; after the `$<LINK_GROUP:RESCAN,...>` fix, succeeded
   cleanly) → `cmake --build build --target GreatTamanaEngineTests` (succeeded
   cleanly after the identical fix was applied to `tests/CMakeLists.txt`).
   A quick filtered `ctest -R "LoggerTest|SdlLinkageRegression"` (13/13
   passed) confirmed the incremental build was healthy before proceeding to
   the mandatory full checkpoint.

2. **FULL CLEAN REBUILD** (`cmake --build build --clean-first`, this repo's
   own established convention per `frame-debugger-1`/`atmosphere-scattering-1`'s
   own completion reports) — **493/493 steps, zero errors**. Confirms
   `gte_core.a`, `gte_editor.a`, `GreatTamanaEngine.exe`, and
   `GreatTamanaEngineTests.exe` all build correctly from a completely fresh
   object-file state, not just incrementally.

3. **FULL `ctest -C Debug --output-on-failure` regression pass** (Checkpoint 1
   of 2 — the design doc's own highest-risk step) —
   **1771 tests, 100% passing, 1 pre-existing environment-gated skip**
   (`PmxLoaderRealModelSmokeTest.LoadsAnMmdModelIfPresentOnThisMachine`, gated
   on a real, non-vendored MMD model file not present on this machine —
   unrelated to this phase, same skip every prior campaign has documented).

   **Before/after test counts**: this phase is pure mechanical CMake-target
   file movement plus one function rename — **zero test files were added,
   removed, or modified** (confirmed via `git_status`: only `CMakeLists.txt`,
   `tests/CMakeLists.txt`, `src/Editor/EditorLayer.h`, and
   `src/Editor/NullEditorLayer.cpp` changed). The same 1771 tests that pass
   AFTER this phase's changes would therefore also have been the exact set
   discovered BEFORE this phase's changes, had a full `ctest` been run then
   (it was not — per campaign policy, every phase before this one used only
   incremental/targeted `ctest` runs, never a full pass; this is genuinely
   the FIRST full run since this campaign began). For historical context,
   the last full-suite baseline recorded anywhere in this repo before this
   campaign started is `render-pass-7`'s own 1753 tests (see `README.md`) —
   the difference (1753 → 1771, +18) is the net effect of Phase 1's new
   `SdlLinkageRegressionTests.cpp` (4 tests) and Phase 3's new
   `LogSinkTests.cpp` (8 tests) and Phase 4's net new
   `GpuMemoryTrackerTest`/`EditorGpuMemoryNameOverlayTest` observer-hook tests,
   none of which this phase touched or needed to re-verify — they were simply
   never run through a FULL suite pass until now. **Before this phase: 1771
   tests (identical set, unverified via full run until now). After this
   phase: 1771 tests, 100% passing (1771/1772 total including the 1
   documented skip).**

4. **Live smoke check** (Step 12, mandatory for this phase): `run_app_background`'d
   the freshly-clean-rebuilt `build/GreatTamanaEngine.exe`, then via
   `gte_send_request`:
   - `GET /get_swapchain` — screenshot confirmed the **REAL, docked ImGui
     Editor UI** (Hierarchy / Scene / Game / Inspector panels, plus the
     Memory/Profiler/Render Graph/Atmosphere/Jobs/Log/Project tab bar, the
     Pause/Step toolbar) — **not a blank/no-op window**, the exact concrete
     symptom the ODR fix (Step 4) exists to prevent. Rendering (the sky
     gradient in both Scene and Game panels) is visually unchanged from
     every prior phase's own documented screenshot.
   - `GET /get_logs?min_level=Warning&limit=50` — `count: 0`, no new
     warnings/errors from boot.
   - `stop_app_background`'d the process cleanly when done.

No `bug_report` was filed for this phase — the one confirmed build issue (the
static-archive link-order problem) was a real, standard, well-understood
linker limitation with a real, standard fix (`$<LINK_GROUP:RESCAN,...>`), not
a malfunctioning tool.

## Definition of Done — checklist

- [x] `gte_core` and `gte_editor` exist as two real, separate CMake targets.
- [x] Exactly ONE definition of `gte::CreateEditorLayer()` exists in the
      entire repository (`ImGuiEditorLayer.cpp`), and exactly ONE definition
      of `gte::CreateNullEditorLayer()` exists (`NullEditorLayer.cpp`) —
      confirmed via `search_in_dir`.
- [x] Full clean rebuild succeeds (493/493 steps, zero errors).
- [x] Full `ctest` regression pass succeeds (1771 tests, 100% passing, 1
      pre-existing environment-gated skip — before/after counts documented
      above, honestly noting this is the first full run this campaign has
      ever done).
- [x] Live boot + Game View screenshot confirms unchanged rendering, AND
      confirms the REAL ImGui Editor UI is what actually shows on screen.
- [x] `PHASE9_COMPLETION_REPORT.md` written (this file).
- [ ] git commit — done immediately after this report (see commit that
      follows).

## Out of Scope (confirmed, unchanged)

- Did NOT touch `Window`/SDL inversion (Phases 10-11) — `gte_core` still
  legitimately links `SDL3::SDL3` and compiles `Window.cpp`/`Application.cpp`
  today; expected and handled later.
- Did NOT extract `Core`'s class body (Phases 12-13) — `Application.cpp`
  remains exactly where it was, still the real composition root, still
  directly calling Editor functions (the exact reason this phase needed the
  `LINK_GROUP` fix above) — Phase 13 is what removes that need for good.
- Did NOT touch `GTE_ENABLE_PROJECT_PANEL` semantics — moved verbatim
  alongside its own file list, from `gte_core`'s `target_sources()` to
  `gte_editor`'s, with zero behavior change.
- Did NOT rename the executable target (`GreatTamanaEngine` stays as-is —
  Phase 17's job, once `EditorHost` fully replaces `Application`).
- Did NOT touch the CI-only standalone-core probe (Phase 18's job) — this
  phase's own full-`ctest` checkpoint is a DIFFERENT verification mechanism
  (the whole test suite, always linking `gte_editor` alongside `gte_core`,
  per Section 7.3 of the design doc — the standalone probe is the only place
  `gte_editor` is ever intentionally NOT configured).
- The pre-existing `build-editor-off/` directory (a `GTE_ENABLE_EDITOR=OFF`
  cache from before Phase 8 deleted that option) is now permanently obsolete
  — `GTE_ENABLE_EDITOR` no longer exists anywhere in this codebase (Phase 8),
  so that directory's own cached configuration cannot be reconfigured at all
  anymore. This was already true after Phase 8; this phase did not create or
  worsen it, and per Rule 4 ("no CMake option anywhere skips `gte_editor`")
  there is deliberately no replacement for it.

## Files touched

- MODIFIED: `CMakeLists.txt` (major restructuring — `NullEditorLayer.cpp`
  added to `gte_core`'s unconditional source list; the old `if(TRUE) ...
  else() ... endif()` placeholder block replaced with a real
  `add_library(gte_editor STATIC ...)` target + its own
  `GTE_ENABLE_PROJECT_PANEL` sub-block + `target_link_libraries()` calls;
  `GreatTamanaEngine`'s own link line switched to `gte_editor` via
  `$<LINK_GROUP:RESCAN,gte_editor,gte_core>`)
- MODIFIED: `tests/CMakeLists.txt` (`GreatTamanaEngineTests`'s own link line
  switched to `gte_editor` via the identical `$<LINK_GROUP:RESCAN,...>`
  wrapper; the `imgui` link and the Editor-test-source `if(TRUE)` blocks'
  comments updated to describe their new, permanent status)
- MODIFIED: `src/Editor/EditorLayer.h` (added the `CreateNullEditorLayer()`
  declaration alongside the unchanged `CreateEditorLayer()` one)
- MODIFIED: `src/Editor/NullEditorLayer.cpp` (renamed its own factory
  function definition to `CreateNullEditorLayer()`; updated file-level
  comment)
- NEW: `task_manager/editor-core-separation-1/PHASE9_COMPLETION_REPORT.md`
  (this file)
