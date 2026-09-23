# PHASE6 — COMPLETION REPORT: Bucket B, Part 2a — Convert Scene Save/Load Call Sites

## Parent
`PHASE0_MASTER_STRATEGY.md`, plus the original design doc
(`C:\Users\F5954\Documents\TAMANA\GreatTamanaEngin-Ideas\Editor_Core_Library_Separation_Design_2026-09-23.md`),
both read in full before starting, plus `PHASE1_COMPLETION_REPORT.md` through
`PHASE5_COMPLETION_REPORT.md` (all five prior completion reports in this
campaign folder) read in full for continuation clues.

## Status: DONE

## What I did

1. Re-confirmed every real, current file path/shape before editing (per
   Universal Rule 9), via `read_file`/`search_in_dir`, rather than trusting
   the strategy doc's own inventory blindly:
   - `src/Core/EditorCapabilities.h` (PHASE5's own `ISceneIOCapability`
     declaration).
   - `src/Editor/SceneIO.h`/`.cpp` in full — the real implementation to wrap.
   - `src/Application/EngineCommandDispatch.h`/`.cpp` — confirmed the exact
     `#if GTE_ENABLE_EDITOR`/`#else` block and the exact hardcoded fallback
     string `"scene save/load requires the Editor module (GTE_ENABLE_EDITOR
     is OFF in this build)"`, duplicated identically in both the
     `SaveScene`/`LoadScene` cases.
   - `src/Network/NetworkServer.cpp`'s `/save_scene`/`/load_scene` route
     handlers (lines ~863-955) — confirmed PHASE5's own finding still holds:
     these routes carry **zero** `GTE_ENABLE_EDITOR` macro of their own; they
     only ever read the plain `SaveSceneOutcome`/`LoadSceneOutcome::
     editorAvailable`/`errorMessage`/`resolvedPath` fields
     `EngineCommandDispatch.cpp` already produces via `EngineCommandBridge`.
     **This phase's own Step 4 ("In NetworkServer.cpp: same treatment...")
     therefore needed zero code changes** — confirmed by direct re-read, not
     assumed from PHASE5's report alone.
   - `src/Application/Application.h`/`.cpp` — confirmed PHASE5's exact
     temporary wiring point (`m_sceneIOCapability` member,
     `SetSceneIOCapability()` setter) and the real constructor/call-site
     shapes needing updates.

2. **A genuine gap PHASE5's own interface design left open, found and fixed
   during this phase (not silently patched over)**: `EngineCommandDispatch.cpp`'s
   ON-branch resolves an EMPTY caller-supplied path via `SceneIO.h`'s own free
   `DefaultScenePath()` function before calling `SaveScene(game, path)`/
   `LoadScene(game, renderer, path)`. `DefaultScenePath()`'s real
   implementation (`Editor/SceneIO.cpp`) calls `ResolveProjectRootDirectory()`
   (`Editor/ProjectRootPath.h`), which itself depends on `SDL_GetBasePath()` —
   a genuinely Editor-only dependency, confirmed by reading both files in
   full. `ISceneIOCapability` (PHASE5) declared only `SaveScene()`/
   `LoadScene()`, both of which take an ALREADY-RESOLVED concrete path — there
   was no way for `EngineCommandDispatch.cpp` (Core-destined) to resolve an
   empty path to the engine's one hardcoded default location without either
   (a) still calling `SceneIO.h`'s free function directly (reintroducing the
   exact violation this phase exists to remove) or (b) extending the
   interface. **I extended `ISceneIOCapability` with a third method,
   `virtual std::filesystem::path DefaultScenePath() const = 0;`**, documented
   in `Core/EditorCapabilities.h` with the full reasoning above. This is a
   necessary, minimal completion of PHASE5's own interface, not a redesign or
   fragmentation of it (Locked Design Decision #8 governs `IEditorLayer`/
   `FrameDebuggerCaptureContext*`, not this brand-new PHASE5 interface, so
   there is no "never redesign" rule being violated here).

3. **NEW `src/Editor/EditorSceneIOCapability.h`/`.cpp`** — the real,
   `gte_editor`-owned adapter implementing `ISceneIOCapability`, delegating to
   `SceneIO.h`'s real `SaveScene()`/`LoadScene()`/`DefaultScenePath()` free
   functions. The **header** carries zero dependency on `SceneIO.h`/
   `ProjectRootPath.h` (only the `.cpp` does) — mirroring PHASE2's own
   `FrameDebuggerReplayPasses.cpp`/`FrameDebuggerDrawRecording.cpp` precedent
   of "the real body needing the complete type lives in a `gte_editor`-only
   `.cpp`, never in a header a `gte_core`-destined file might include" — so
   `Application.h`/`.cpp` could, in principle, include this header
   unconditionally with zero macro guard at the declaration level. Each
   method preserves the EXACT SAME fallback error-message text
   `EngineCommandDispatch.cpp`'s old ON-branch used to set directly
   (`"failed to write scene file (I/O error) - see engine log"` /
   `"failed to load scene file - it may not exist, or failed to parse (see
   engine log)"`) — a behavior-preserving refactor, confirmed correct live
   (see smoke check below).

4. **`src/Application/EngineCommandDispatch.h`** — added a new
   `ISceneIOCapability* sceneIOCapability` parameter to
   `ExecuteEngineCommand()`'s signature (after `renderer`, before `request`),
   documented with the exact "nullptr means not registered" semantics. Added
   `#include "../Core/EditorCapabilities.h"` (a small, always-compiled Core
   header — no macro guard needed for this include itself).

5. **`src/Application/EngineCommandDispatch.cpp`** — removed the `#if
   GTE_ENABLE_EDITOR` / `#include "../Editor/SceneIO.h"` / `#endif` block at
   the top entirely. Both `SaveScene`/`LoadScene` cases now read:
   ```cpp
   if (sceneIOCapability != nullptr) {
       const std::filesystem::path path = request.saveScene.path.empty()
           ? sceneIOCapability->DefaultScenePath()
           : std::filesystem::path(request.saveScene.path);
       result.saveScene.success = sceneIOCapability->SaveScene(game, path, result.saveScene.errorMessage);
       result.saveScene.resolvedPath = path.string();
   } else {
       result.saveScene.editorAvailable = false;
       result.saveScene.errorMessage =
           "scene save/load requires the Editor module (GTE_ENABLE_EDITOR is OFF in this build)";
   }
   ```
   (LoadScene mirrors this exactly, substituting `renderer` into the
   `LoadScene()` call.) The `#else` branch's exact original fallback message
   string is preserved verbatim, per this phase's own explicit instruction —
   a runtime null-check replaces the compile-time `#if`, nothing else about
   the observable behavior changes.

6. **`src/Application/Application.cpp`** — three changes:
   - Added a temporary, `#if GTE_ENABLE_EDITOR`-guarded
     `#include "../Editor/EditorSceneIOCapability.h"` right after the
     existing (already unconditional) `Editor/Logger.h` include, with a
     comment explaining why this one stays macro-guarded for now (mirrors
     `SdlContext::SdlContext()`'s own pre-existing `SdlMemoryTracker::
     Install()` precedent — `Application.cpp` still compiles in BOTH
     `GTE_ENABLE_EDITOR` configurations today; Phase 8/9 delete the macro
     outright and Phase 16 moves this whole wiring concern into `EditorHost`).
   - Added the real wiring, right after the existing `InstallLogSink()` call
     in the constructor body:
     ```cpp
     #if GTE_ENABLE_EDITOR
         static EditorSceneIOCapability s_editorSceneIOCapability;
         SetSceneIOCapability(&s_editorSceneIOCapability);
     #endif
     ```
     A function-local `static` (whole-process-lifetime, matching
     `LoggerLogSink::Instance()`'s own Meyers-singleton precedent and this
     same file's own `GpuDrivenBatchNamePool()` function-local-static
     precedent) — `EditorSceneIOCapability` is pure delegation with no state
     of its own, so a single shared instance is correct and sufficient.
   - Updated the one real call site,
     `ExecuteEngineCommand(m_game, m_renderer, *request)` →
     `ExecuteEngineCommand(m_game, m_renderer, m_sceneIOCapability, *request)`.

7. **`CMakeLists.txt`** — registered the two new files
   (`src/Editor/EditorSceneIOCapability.h`/`.cpp`) in the still-conditional
   (for now — Phase 9's job) `if(GTE_ENABLE_EDITOR)` Editor source list,
   right after `src/Editor/SceneIO.cpp`.

## The Step 7 "zero results expected" nuance — resolved by direct reading, no `ask_questions` needed

The phase's own Step 7 says: *"Confirm via `search_in_dir` for
`GTE_ENABLE_EDITOR` scoped to these two files — zero results expected after
this phase."* Taken 100% literally this is unsatisfiable together with Step
3's own explicit instruction to *"Keep the exact same OFF-branch fallback
message/behavior"* — that fallback message's own TEXT is the literal English
sentence `"...requires the Editor module (GTE_ENABLE_EDITOR is OFF in this
build)"`, which necessarily contains the substring `GTE_ENABLE_EDITOR` as
plain data, not as a preprocessor directive. I re-ran the search after
finishing and confirmed: **zero real `#if`/`#ifdef GTE_ENABLE_EDITOR`
preprocessor branches remain in either file** — every remaining hit in
`EngineCommandDispatch.h`/`.cpp` is either (a) a doc comment describing what
used to happen, in the past tense, or (b) the literal, deliberately-preserved
fallback string. This is the same category of nuance PHASE3 and PHASE4 each
found and documented explicitly in their own reports rather than silently
declaring the letter of Step 7 satisfied or unsatisfied without comment. I
consider Step 7's real intent (no more compile-time macro branching at these
call sites) fully satisfied.

## Compile-check / smoke-check results

**Incremental compile check only, per campaign policy** — no full clean
build, no full `ctest` regression pass (not required until Phase 9/14/19).

- `cmake -S . -B build` — succeeded (only the pre-existing, unrelated
  `third_party/ktx` `git describe` warning, same as every prior phase's own
  report).
- `cmake --build build --target gte_core` — **succeeded cleanly** (4 build
  steps: `EditorSceneIOCapability.cpp.obj`, `EngineCommandDispatch.cpp.obj`,
  `Application.cpp.obj` recompiled, `libgte_core.a` relinked).
- `cmake --build build --target GreatTamanaEngine` — **succeeded cleanly**,
  full executable relinked.
- **Extra, beyond-minimum verification (mirroring PHASE2-5's own precedent)**:
  re-ran `cmake -S . -B build-editor-off` then
  `cmake --build build-editor-off --target gte_core` — **succeeded cleanly**
  (confirms the new `Core/EditorCapabilities.h` third interface method and
  every touched file still compile fine in the `GTE_ENABLE_EDITOR=OFF`
  configuration too, since the macro-guarded include/wiring in
  `Application.cpp` is skipped entirely in that config). Then
  `cmake --build build-editor-off --target GreatTamanaEngine` — failed at the
  final link step with **the exact same two, and only two,** `undefined
  reference` errors PHASE2 already documented and the user already signed off
  on as a deliberate, temporary, pre-existing regression
  (`gte::AddFrameDebuggerReplayPasses(...)`/`gte::RecordFrameDebuggerDraws(...)`)
  — confirming this phase introduces **zero new** OFF-mode breakage.
- Live smoke check: `run_app_background`'d `build/GreatTamanaEngine.exe`,
  then via `gte_send_request`:
  - `GET /get_swapchain` — screenshot confirmed the Editor renders normally
    (Hierarchy/Scene/Game/Inspector/Project panels all visible and correct).
  - `POST /save_scene` (`{"path":""}`) — `200`,
    `"resolved_path":"...\\build\\Project\\TestScene.gtscene"`,
    `"success":true` — proves the new `DefaultScenePath()` capability method
    resolves the default path correctly through the whole new pointer-based
    pipeline.
  - `POST /load_scene` (`{"path":""}`) — `200`, same resolved path,
    `"success":true`.
  - `POST /save_scene` with an explicit custom path — `200`, `"success":true`,
    resolved path echoed back exactly as supplied — proves the
    non-empty-path branch (no `DefaultScenePath()` call) still works.
  - `POST /load_scene` on that same custom path — `200`, `"success":true`.
  - `POST /load_scene` on a deliberately non-existent path — **`400`**,
    `{"error":"failed to load scene file - it may not exist, or failed to
    parse (see engine log)","success":false}` — confirms
    `EditorSceneIOCapability::LoadScene()`'s fallback error message is
    byte-for-byte the same text the old inline ON-branch used to set, and
    that `NetworkServer.cpp`'s own untouched 400-on-failure status-code logic
    still works correctly through the new capability-pointer path.
  - `GET /get_logs?min_level=Warning&limit=50` — `count: 0`, no new
    warnings/errors from any of the above.
  - `stop_app_background`'d the process when done.

No `bug_report` was filed — no tool malfunctioned during this phase.

## Definition of Done — checklist

- [x] Zero `GTE_ENABLE_EDITOR` COMPILE-TIME branch (`#if`/`#ifdef`) remains in
      `EngineCommandDispatch.cpp`/`.h` related to scene IO (confirmed via
      `search_in_dir` — remaining hits are comments/the deliberately-preserved
      fallback string only, see the dedicated section above).
  `NetworkServer.cpp` needed no changes at all (PHASE5's own finding,
      re-confirmed by a fresh direct read this phase).
- [x] `/save_scene`/`/load_scene` confirmed working live — both the
      default-path and explicit-path branches, plus the failure-path 400
      response, all exercised and correct.
- [x] `PHASE6_COMPLETION_REPORT.md` written (this file).
- [ ] git commit — done immediately after this report (see commit that
      follows).

## Out of Scope (confirmed, unchanged)

- Did not touch `EditorUiCommandBridge.h`, `AssetImportCommandBridge.h`,
  `GpuDrivenBatchTestSpawner.h`, or `Game.h` — PHASE5's own finding already
  confirmed these three don't need a new capability interface at all (they
  already route unconditionally through the pre-existing `IEditorLayer*`
  hook); Phase 7 re-confirms and closes this out formally.
- Did not touch `IEditorLayer`/`FrameDebuggerCaptureContext*` themselves.
- No CMake target surgery (`gte_editor` target, etc.) — still Phase 9's job.
  The new `EditorSceneIOCapability.h`/`.cpp` files still live inside the
  pre-existing, still-macro-gated `if(GTE_ENABLE_EDITOR)` block in the ONE
  `gte_core` target, exactly like every prior phase's own new Editor-side
  files.

## Files touched

- MODIFIED: `src/Core/EditorCapabilities.h` (added `DefaultScenePath()` to
  `ISceneIOCapability`, with full reasoning documented in-place)
- NEW: `src/Editor/EditorSceneIOCapability.h`/`.cpp`
- MODIFIED: `src/Application/EngineCommandDispatch.h` (new
  `sceneIOCapability` parameter + doc comments)
- MODIFIED: `src/Application/EngineCommandDispatch.cpp` (runtime null-check
  replaces the compile-time `#if` for both `SaveScene`/`LoadScene` cases)
- MODIFIED: `src/Application/Application.cpp` (macro-guarded include, wiring
  in the constructor, updated `ExecuteEngineCommand()` call site)
- MODIFIED: `CMakeLists.txt` (registered the two new files)
- NEW: `task_manager/editor-core-separation-1/PHASE6_COMPLETION_REPORT.md`
  (this file)
