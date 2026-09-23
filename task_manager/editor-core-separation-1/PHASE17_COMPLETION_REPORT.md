# PHASE17 — COMPLETION REPORT: Retire `Application`, Rename the Executable Target

## Parent
`PHASE0_MASTER_STRATEGY.md` (see "Locked Design Decision #3" — the rename IS
in scope), plus the original design doc
(`C:\Users\F5954\Documents\TAMANA\GreatTamanaEngin-Ideas\Editor_Core_Library_Separation_Design_2026-09-23.md`,
Section 6.2), both read in full before starting, plus `PHASE1_COMPLETION_REPORT.md`
through `PHASE16_COMPLETION_REPORT.md` (all sixteen prior completion reports
in this campaign folder) read in full for continuation clues. Also re-read
`README.md`/`AGENTS.md` (repo root) per task instructions.

## Status: DONE — depends on Phase 16, confirmed already landed (`EditorHost`
owns every automation bridge/`NetworkServer`, `Core.h`/`Core.cpp` untouched,
`Application` reduced to genuinely dead code, still compiling only because
this phase is the one that deletes it).

## Step 0 — Re-confirmed the real, current code shape before editing

Per Universal Rule 9, read (not assumed) every file this phase touches or
depends on, fresh, before writing anything:

- `src/main.cpp` — confirmed it already constructs ONLY `EditorHost` (Phase 15
  already did this); no `Application`-constructing path remained to delete.
- `src/Application/Application.h`/`.cpp` (post-Phase-16 state, 192/203 lines)
  — read both in full. Confirmed the class's own header comment already
  states outright: *"This class is now genuinely, completely UNUSED... kept
  around, still compiling, ONLY because Phase 17
  (PHASE17_APPLICATION_RETIREMENT_AND_EXECUTABLE_RENAME.md) is the phase that
  actually deletes it."*
- `search_in_dir` for `"Application.h"` across all of `src/` — confirmed
  `Application.cpp` was the ONLY file that included the real header; every
  other hit was a comment in an unrelated file (`AssetImportCommandBridge.h`,
  `EditorUiCommandBridge.h`, `EngineCommandBridge.h`, `FrameCaptureBridge.h`,
  `FrameDebuggerCommandBridge.h`, `Core/Core.h`, `Editor/EditorHost.h`,
  `Editor/EditorHostServices.h`, `Network/NetworkServer.h`) describing a
  historical shape/precedent, never a real `#include`.
- `search_in_dir` for `"class Application"` across all of `src/` — exactly
  ONE match, `Application.h`'s own class declaration.
- Root `CMakeLists.txt` — confirmed the exact current
  `add_executable(GreatTamanaEngine src/main.cpp)` line, every
  `gte_add_shader(GreatTamanaEngine ...)` call (29 shader registrations plus
  2 target-name mentions inside `if(GTE_ENABLE_PROJECT_PANEL)`/`if(TRUE)`
  blocks), the `target_link_libraries(GreatTamanaEngine ...)` line, the
  `sdl3_copy_runtime_dll(GreatTamanaEngine)` line, and the exact
  `src/Application/Application.h`/`.cpp` two-line entry inside `gte_editor`'s
  own `target_sources()` list (Phase 14 moved these two files here).
- `tests/CMakeLists.txt`, `BUILDING.md`, `TESTING.md` — confirmed every real
  prose mention of the executable's name (`GreatTamanaEngine`, distinct from
  `GreatTamanaEngineTests`, the unrelated test-suite target which is NOT
  renamed by this phase).

No path/shape surprises versus this phase's own plan text — every real file
matched exactly what the phase document described.

## The new executable name — chosen via `ask_questions`, user deferred

Per this phase's own explicit instruction ("Use `ask_questions` to decide the
exact new executable name if not already obvious from context... this is
ultimately a product-naming decision, not a pure architecture one"), I asked
the user to choose between `GreatTamanaEditor` (the design doc's own Section
6.2 suggestion, and PHASE17's own Step 3 literal example), `GTEditor`,
`GreatTamanaEngineEditor`, or keeping `GreatTamanaEngine` unchanged.

**The user was away and explicitly deferred the decision back to me** ("user
is not at office, leave the decision making up to you"). I chose
**`GreatTamanaEditor`** — the SAME name both the original design doc (Section
6.2: *"e.g. `GreatTamanaEditor`"*) and this phase's own strategy file (Step 3:
*"e.g. `GreatTamanaEditor`"*) already independently suggested, making it the
lowest-risk, most clearly campaign-endorsed choice rather than an invented
alternative. It also honestly reflects what this executable now actually is
(the authoring tool `EditorHost` drives — ImGui panels, gizmos, Frame
Debugger, every debug-tooling feature — never a shippable Player build,
which per the design doc's Section 8 is deliberately out of scope for this
whole campaign and would come from a completely separate, not-yet-built
pipeline).

## What I did

1. **Confirmed `main.cpp` already constructs ONLY `EditorHost`** — Phase 15
   already replaced the `Application`-constructing path outright (no dead
   `#if`/dual-path branch was left behind for this phase to remove), so
   Step 3.1's own literal instruction ("delete the Application-constructing
   path entirely") required zero further code change to `main.cpp`'s control
   flow. Updated `main.cpp`'s own header comment instead (it still described
   the now-obsolete "Application itself stays fully intact and unused" state)
   to record that Phase 17 deleted `Application.h`/`.cpp` outright and
   renamed the executable target — see "Files touched" below.
2. **Deleted `src/Application/Application.h` and `src/Application/Application.cpp`
   outright** (`run_shell del`, confirmed via a fresh `browse_dir` on
   `src/Application/` immediately after — the directory now contains exactly
   the 20 remaining, still-live files: the five automation-bridge header/
   `.cpp` pairs, `AtmospherePassSequence.h/.cpp`, `EngineCommandDispatch.h/.cpp`,
   `EventTranslator.h/.cpp`, `MemorySnapshotBuilder.h`, `RenderPasses.h/.cpp`,
   `RenderPassViewData.h`).
3. **Root `CMakeLists.txt`** — removed the `src/Application/Application.h`/
   `src/Application/Application.cpp` two-line entry from `gte_editor`'s own
   `target_sources()` list, replacing the surrounding PHASE14-era bullet
   comment describing why the file lived there with a short note recording
   that PHASE17 deleted it outright once `EditorHost` fully replaced it.
   Renamed `add_executable(GreatTamanaEngine ...)` →
   `add_executable(GreatTamanaEditor ...)`, the executable's own
   `target_link_libraries(... "$<LINK_GROUP:RESCAN,gte_editor,gte_core>")`
   line, all 29 `gte_add_shader(GreatTamanaEngine ...)` calls, and
   `sdl3_copy_runtime_dll(GreatTamanaEngine)` — every one of these is a real
   CMake target-name argument, not prose, and had to match the renamed
   target exactly for the build to keep working. Also updated every
   PROSE comment that named the executable for accuracy (the
   `gte_core`/`gte_editor` library-comment block, the `GTE_ENABLE_PROJECT_PANEL`/
   `GTE_ENABLE_PROFILER`/`GTE_ENABLE_JOB_SYSTEM`/`GTE_ENABLE_NETWORK` compile-
   definition comments, the SDL3 linkage comment describing `gte_editor`'s
   public headers) — several of these also named `Application.h`/
   `Application.cpp`/`SdlContext (inside Application.cpp)` as still-existing
   files; updated those to `EditorHost.h`/`EditorHost.cpp` (the file that
   actually owns this logic today, since Phase 14/15/16).
4. **Corrected a stale prediction in the executable's own `$<LINK_GROUP:RESCAN,...>`
   rationale comment** (a real, deliberate, non-mechanical fix, not just a
   name substitution): Phase 9's own comment predicted *"this wrinkle is
   EXPECTED to disappear on its own once Phase 13 replaces Application's
   direct Editor-function calls..."* — this turned out to be only PARTIALLY
   true. `Application.cpp`'s own contribution to the wrinkle is gone now that
   the file is deleted, but `RenderSystem.cpp`'s call to
   `RecordFrameDebuggerDraws()` and `NetworkServer.cpp`'s calls to
   `Logger::Query()`/`Logger::Clear()` (both still physically compiled into
   `gte_core`, calling real `gte_editor`-only symbols) are genuine,
   PERMANENT `gte_core -> gte_editor`-only-symbol dependencies with no
   further phase in this campaign's roadmap planned to remove them —
   confirmed by direct code reading, not assumed. Added an honest "UPDATE,
   PHASE17" paragraph to the comment rather than silently rewriting history
   or leaving a now-false prediction in place. Applied the identical,
   smaller-scale correction to `tests/CMakeLists.txt`'s own matching
   `$<LINK_GROUP:RESCAN,...>` rationale comment for `GreatTamanaEngineTests`
   (which needed the same fix, since it names the same two files and the
   same "until Phase 13" prediction).
5. **`tests/CMakeLists.txt`** — updated every prose mention of the renamed
   executable (the SDL3.dll-staging comment, the `$<LINK_GROUP:RESCAN,...>`
   rationale comment naming "the root CMakeLists.txt's own GreatTamanaEngine
   target"). Did **not** rename `GreatTamanaEngineTests` itself — that is a
   separate, unrelated target this phase's own scope never mentions renaming,
   confirmed by re-reading `PHASE17_APPLICATION_RETIREMENT_AND_EXECUTABLE_RENAME.md`'s
   own text (it only ever says "the executable" singular, meaning the one
   `Application`/`EditorHost` composition root produces).
6. **`BUILDING.md`** — updated the one build-output path mention
   (`build\Debug\GreatTamanaEngine.exe` → `build\Debug\GreatTamanaEditor.exe`).
7. **`TESTING.md`** — updated the one prose mention of "the real executable
   (`GreatTamanaEngine`)" to name `GreatTamanaEditor`, with a short note that
   it was renamed from `GreatTamanaEngine` by this phase.
8. **`README.md`** — confirmed via `search_in_dir` that its only mention of
   the string `"GreatTamanaEngine"` is the document's own title
   (`# GreatTamanaEngine`, describing the REPOSITORY/engine as a whole, the
   same name `project(GreatTamanaEngine LANGUAGES CXX C)` in the root
   `CMakeLists.txt` still correctly uses) — no executable-name mention exists
   in this file, so no change was needed here. This is a deliberate,
   confirmed no-op, not an oversight: the repository/engine's own name and
   the authoring-tool executable's name are two different concepts, and only
   the latter is in this phase's scope (the design doc's own Section 1
   confirms this distinction — `gte_core.a`/`gte_editor.a` are the engine;
   the executable built from this repo is the authoring tool).
9. **Did NOT rename `project(GreatTamanaEngine LANGUAGES CXX C)`** (root
   `CMakeLists.txt` line 11) or the line-7 prose string
   `"GreatTamanaEngine's CMake build only supports Windows right now."` — both
   name the overall repository/CMake project, not the executable target this
   phase renames. Confirmed via a fresh `search_in_dir` that these are the
   ONLY two remaining `GreatTamanaEngine` mentions in the root `CMakeLists.txt`
   after every real target-name/prose fix above, alongside the unrelated,
   correctly-untouched `GreatTamanaEngineTests` mentions.

## Genuine ambiguity found — resolved via `ask_questions`

The exact new executable name (see dedicated section above) — the only
architecturally-significant open question this phase's own strategy document
explicitly flagged as needing a human/product decision rather than a pure
architecture call.

## Compile-check / smoke-check results

**Incremental compile check only, per campaign policy** — no full clean
build, no full `ctest` regression pass (not required until Phase 19).

- `cmake -S . -B build` — succeeded (only the pre-existing, unrelated
  `third_party/ktx` `git describe` warning, same as every prior phase's own
  report). This reconfigure was REQUIRED (not just a formality) since the
  `target_sources()` file list changed (two files removed) and the
  executable target itself was renamed.
- `cmake --build build --target gte_core` — `ninja: no work to do.` (correct
  and expected — `Application.h`/`.cpp` never lived in `gte_core`'s own
  source list; nothing in this phase touches `gte_core` at all).
- `cmake --build build --target gte_editor` — succeeded cleanly (one
  relink step — `Application.cpp.obj`/`Application.h`'s translation unit
  simply no longer exists in the archive; no other file needed recompiling).
- `cmake --build build --target GreatTamanaEditor` — **succeeded cleanly**
  (`main.cpp.obj` recompiled — its own header comment changed — full
  executable relinked under the NEW target name, every `.spv` shader +
  `SDL3.dll` staged next to the renamed `GreatTamanaEditor.exe` exactly as
  before, confirmed by the build log literally saying "Staging ... next to
  GreatTamanaEditor" for all 29 shaders).
- `cmake --build build --target GreatTamanaEngineTests` — succeeded cleanly
  (one relink step only — no test source references `Application.h`/`.cpp`
  at all, confirmed before editing).
- Targeted `ctest -C Debug -R "SdlLinkageRegression|LoggerTest|EditorUiCommandBridge"`
  — **21/21 passed (100%)**, including both `SdlLinkageRegressionTest` cases
  (the test binary still correctly requires `SDL3.dll` for the documented,
  permanent `SdlMemoryTrackerTests.cpp` reason; `gte_core.a` still correctly
  carries zero real SDL3 function symbol) — confirms this phase introduced
  no regression in either module.
- **Live smoke check**: `run_app_background`'d the freshly-built
  `build\GreatTamanaEditor.exe` (PID 6400 — confirming the renamed `.exe`
  itself launches, not just that CMake accepted the new target name), then
  via `gte_send_request`:
  - `GET /get_swapchain` — screenshot confirmed the Editor renders exactly
    like every prior phase's own documented baseline: docked Hierarchy/Scene/
    Game/Inspector panels, the Pause/Step toolbar, the "Project" panel showing
    real project files, identical sky-gradient Atmosphere rendering in both
    Scene and Game panels.
  - `GET /get_logs?limit=20` — returned exactly 2 real startup log entries:
    `{"category":"Network", "message":"listening on 127.0.0.1:8080"}` and
    `{"category":"EditorHost", "message":"EditorHost constructed: SdlContext
    -> Window -> Core -> CreateEditorLayer() -> Core::SetEditorLayerHook()
    all completed, with a genuinely non-null IEditorLayer*, every automation
    bridge attached, and NetworkServer started."}` — direct, positive proof
    the renamed executable's real composition root is `EditorHost`, not the
    now-deleted `Application` (no `"category":"Application"` log line exists
    anywhere in this session, unlike every prior phase's own screenshot
    before Phase 16).
  - `GET /get_logs?min_level=Warning&limit=50` — `count: 0`, no new
    warnings/errors from boot.
  - `stop_app_background`'d the process (PID 6400) cleanly when done.

No `bug_report` was filed — no tool malfunctioned during this phase; the two
self-inflicted `edit_line` boundary mistakes (a duplicated stale line at
`CMakeLists.txt`'s SDL3-linkage comment, caused by an under-counted `length`
on one edit; a duplicated stale header-comment line in `main.cpp`) were both
caught immediately by re-reading the file with `read_line`/the tool's own
post-edit context, and fixed with one more `edit_line` call each — the exact
same self-correction pattern every prior phase in this campaign has already
documented for comparable slips.

## Definition of Done — checklist

- [x] `Application.h`/`.cpp` no longer exist (`run_shell del`, confirmed via
      `browse_dir`). Zero remaining reference to `class Application` anywhere
      in `src/` (confirmed via `search_in_dir`, before AND after deletion).
- [x] The executable builds and boots under its new name, `GreatTamanaEditor`
      (confirmed via a live `run_app_background` + `gte_send_request` smoke
      check, PID 6400, real screenshot + real log entries).
- [x] `PHASE17_COMPLETION_REPORT.md` written (this file), recording the exact
      new name chosen (`GreatTamanaEditor`) and why (the design doc's own
      Section 6.2 suggestion, echoed verbatim by this phase's own strategy
      file — the lowest-risk, most campaign-endorsed choice — user explicitly
      deferred the decision to me via `ask_questions`).
- [ ] git commit — done immediately after this report (see commit that
      follows).

## Out of Scope (confirmed, unchanged)

- Did **not** touch the Player Build Pipeline (still explicitly out of scope
  for this entire campaign, design doc Section 8).
- Did **not** rename `GreatTamanaEngineTests` (the unit test suite's own
  executable target) — this phase's own scope only covers the ONE real
  authoring-tool executable `Application`/`EditorHost` produces.
- Did **not** rename `project(GreatTamanaEngine LANGUAGES CXX C)` (the root
  CMake project name) — a different concept from the executable target,
  correctly left alone (see "What I did", item 9).
- Did **not** run a full clean build or full `ctest` regression pass — not
  one of this campaign's three flagged checkpoints (Phase 9, Phase 14,
  Phase 19). **Incremental compile check only, per campaign policy.**
- Did **not** exhaustively rewrite every historical/comment-only mention of
  `Application.h`/`Application.cpp` across the codebase (e.g. the five
  automation-bridge headers' own "see Application.h" doc-comment asides,
  `Core.h`'s "relocated verbatim from Application.h" provenance notes,
  `Network/NetworkServer.h`'s historical reference) — these are historical
  provenance notes describing where code used to live/what shape it mirrored,
  exactly the same class of deliberate, documented scope limit Phase 3/6/8
  each already established in this same campaign ("no more real
  `#include`/class-reference remains" is the actual, checked invariant; a
  large body of purely-historical comment text is left untouched by
  deliberate, precedented choice).

## Files touched

- DELETE: `src/Application/Application.h`, `src/Application/Application.cpp`
- MODIFIED: `src/main.cpp` (header comment rewrite only — the construction
  logic already only built `EditorHost`, since Phase 15)
- MODIFIED: `CMakeLists.txt` (removed the two deleted files from `gte_editor`'s
  source list; renamed the executable target `GreatTamanaEngine` ->
  `GreatTamanaEditor` everywhere it is referenced as a real CMake target
  argument — `add_executable`/`target_link_libraries`/29x `gte_add_shader`/
  `sdl3_copy_runtime_dll`; corrected several stale prose comments naming
  `Application.h`/`Application.cpp`/the old executable name; added an honest
  "UPDATE, PHASE17" correction to the `$<LINK_GROUP:RESCAN,...>` rationale
  comment)
- MODIFIED: `tests/CMakeLists.txt` (prose-only: updated the SDL3.dll-staging
  comment and the matching `$<LINK_GROUP:RESCAN,...>` rationale comment to
  name the renamed executable and the corrected, permanent reason the link
  group is still needed; `GreatTamanaEngineTests` itself is NOT renamed)
- MODIFIED: `BUILDING.md` (one build-output path mention)
- MODIFIED: `TESTING.md` (one prose mention of the executable's name)
- NEW: `task_manager/editor-core-separation-1/PHASE17_COMPLETION_REPORT.md`
  (this file)
