# PHASE7 — COMPLETION REPORT: Bucket B, Part 2b — Convert Remaining Capability Call Sites

## Parent
`PHASE0_MASTER_STRATEGY.md`, plus the original design doc
(`C:\Users\F5954\Documents\TAMANA\GreatTamanaEngin-Ideas\Editor_Core_Library_Separation_Design_2026-09-23.md`),
both read in full before starting, plus `PHASE1_COMPLETION_REPORT.md` through
`PHASE6_COMPLETION_REPORT.md` (all six prior completion reports in this
campaign folder) read in full for continuation clues.

## Status: DONE — zero code changes required, fully confirmed by fresh reads

## Headline finding — PHASE5's own prediction holds, re-verified independently

`PHASE5_COMPLETION_REPORT.md` already concluded, from reading `Application.cpp`'s
real call sites, that `EditorUiCommandBridge.h`, `AssetImportCommandBridge.h`,
`GpuDrivenBatchTestSpawner.h`, and `Game.h` are **already** fully resolved —
each question they raise ("can I activate a tab", "can I import an asset",
"can I spawn a GPU-driven test batch") is already answered UNCONDITIONALLY,
at runtime, through the pre-existing, out-of-scope
`std::unique_ptr<IEditorLayer> m_editorLayer` pointer (never null — it holds
either the real `ImGuiEditorLayer` or the inert `NullEditorLayer`, selected at
link time), with `NullEditorLayer`'s own methods already providing a
complete, correct "not available in this build" answer for every one of them.
PHASE5 explicitly told PHASE7 to expect **zero** remaining
`#if GTE_ENABLE_EDITOR` at these call sites.

This phase re-confirmed that finding from scratch, independently, per
Universal Rule 9 ("never edit blind against a guessed path... re-confirm
every real file path/current code shape"), rather than trusting PHASE5's
report alone:

1. Read all four header files named in this phase's own Step 2, in full,
   fresh: `src/Application/EditorUiCommandBridge.h`,
   `src/Application/AssetImportCommandBridge.h`,
   `src/Editor/GpuDrivenBatchTestSpawner.h`, `src/Game/Game.h`. **Zero**
   `#if`/`#ifdef GTE_ENABLE_EDITOR` preprocessor directive exists in any of
   them — the only mentions of the macro name are inside prose doc comments
   (e.g. `AssetImportCommandBridge.h`'s own
   `ImportExternalFileOutcome::projectAvailable` doc comment: *"the Editor's
   'Project' panel does not exist in this build (GTE_ENABLE_EDITOR or
   GTE_ENABLE_PROJECT_PANEL is OFF)"*), never a real compile-time branch.
2. Also searched (`search_in_dir`, not just the headers) the four matching
   `.cpp` files this phase's own "Files Touched" section flagged as
   "+ `.cpp` if needed": `EditorUiCommandBridge.cpp`,
   `AssetImportCommandBridge.cpp`, `GpuDrivenBatchTestSpawner.cpp`,
   `Game.cpp` — **zero** matches for `GTE_ENABLE_EDITOR` in any of the four.
3. Found and re-read the exact real CORE-side caller for all three
   capabilities — `src/Application/Application.cpp`'s `Run()` loop
   (confirmed at the exact real line numbers, not guessed):
   - `ActivateTab`/`SpawnGpuDrivenTestBatch` (lines ~1257-1278): drains
     `m_uiCommandBridge`, branches on `uiRequest->kind`, calls
     `m_editorLayer->SpawnGpuDrivenTestBatch(...)` or
     `m_editorLayer->ActivateTab(...)` — **no `#if` anywhere on this call
     site**, confirmed by direct read of the real, current file.
   - `ImportExternalFile` (lines ~1348-1373): drains
     `m_assetImportCommandBridge`, calls
     `m_editorLayer->ImportExternalAssetIntoProject(...)` — **no `#if`
     anywhere on this call site** either.
   - `Game.h`'s only Editor-adjacent element is the already-out-of-scope
     `FrameDebuggerCaptureContext*` forward declaration (Locked Design
     Decision #8) — confirmed no separate "not available" fallback exists in
     `Game.h`/`Game.cpp` at all.
4. Also re-read `src/Network/NetworkServer.cpp`'s three matching route
   handlers (`POST /spawn_gpu_driven_test_batch`, `POST /import_asset`) —
   confirmed **zero** compile-time `#if GTE_ENABLE_EDITOR` in either; both
   already branch purely on plain runtime data
   (`outcome.success`/`outcome.projectAvailable`, and — see the "Discovered
   risk" section below — one runtime substring check against the fallback
   error-message TEXT for the GPU-driven-batch route specifically).

**Conclusion: this phase requires literally zero source-code changes.** Every
capability PHASE7's own strategy file anticipated needing a new
`IEditorUiCapability`/`IAssetImportCapability`/`IGpuDrivenBatchTestCapability`
adapter for was already answered by the pre-existing `IEditorLayer*` hook
before this campaign even started — confirmed twice now (PHASE5's own
`Application.cpp`-focused read, and this phase's own header+`.cpp`+call-site
re-read), by two independent passes over the real code, not a single guess
repeated.

## `git_status` confirms zero drift

`git_status` was run before writing this report and shows **"nothing to
commit, working tree clean"** — no file was modified by this phase, exactly
matching the "zero code changes required" conclusion above.

## Full-repo `search_in_dir` sweep — the exact remaining `GTE_ENABLE_EDITOR` inventory for PHASE8

Per this phase's own Definition of Done ("A full-repo `search_in_dir` for
`GTE_ENABLE_EDITOR` shows ONLY Bucket C/CMake-option matches remaining
(documented exact count in the completion report for Phase 8 to consume as
its own starting inventory)"), I ran `search_in_dir` for `GTE_ENABLE_EDITOR`
across all of `src/`, all of `tests/`, and the root `CMakeLists.txt`
(`tests/CMakeLists.txt` matches surfaced through the `tests/` sweep). Every
match was individually classified as either a REAL preprocessor
directive/CMake conditional (a genuine remaining "site"), or a comment/
doc-string/runtime-string mention with zero compile-time-branching effect.

### A) REAL C++ preprocessor directives remaining (the only ones that matter for PHASE8's "does GTE_ENABLE_EDITOR exist as a `#if` anywhere" goal)

**Bucket C, exactly where PHASE0/PHASE8's own inventory expected them — inside `src/Editor/`:**

- `src/Editor/Logger.h` — 1 group: `#if GTE_ENABLE_EDITOR` (line 20),
  `#else // !GTE_ENABLE_EDITOR` (line 114), `#endif // GTE_ENABLE_EDITOR`
  (line 139).
- `src/Editor/Logger.cpp` — 1 pair: `#if GTE_ENABLE_EDITOR` (line 2),
  `#endif // GTE_ENABLE_EDITOR` (line 155).

**NOT in Bucket C's own file list, NOT under `src/Editor/`, flagged here as a genuine gap for PHASE8 to absorb (per this phase's own Step 6: "If anything else still shows up... flag it in the completion report for Phase 8 to absorb"):**

- `src/Application/Application.cpp` — 3 real, currently-live `#if GTE_ENABLE_EDITOR ... #endif` blocks, every one of them **already fully, correctly wired at the logic level** by PHASE3/PHASE4/PHASE6 (this is NOT unclassified/undesigned code — it is Bucket A/B wiring whose LOGIC is already done, with only the physical macro wrapper left to remove):
  1. Lines 16-18: `#include "../Editor/EditorSceneIOCapability.h"` (PHASE6).
  2. Lines 291-302: `SdlMemoryTracker::Install()` call, inside
     `SdlContext::SdlContext()` (pre-existing, PHASE4 confirmed this
     particular call site is Bucket B, not Bucket A, and explicitly left it
     alone as "out of scope for Phase 4").
  3. Lines 380-383: `static EditorSceneIOCapability s_editorSceneIOCapability;
     SetSceneIOCapability(&s_editorSceneIOCapability);` (PHASE6).
  Every one of these three sites' own in-place comment already says outright
  that it is a deliberate, temporary state — e.g. line 14's comment: *"Phase
  8/9 delete GTE_ENABLE_EDITOR outright and Phase 16 moves this whole wiring
  concern into EditorHost instead"* — so this is a confirmed, expected,
  already-self-documented gap, not a surprise this strategy's inventory
  missed. **PHASE8's own Step 2 file list (`Logger.h/.cpp`,
  `EditorPanelCatalog.h`, `ProjectRootPath.h`, `ProfilerPanelData.h`,
  `ImGuiEditorLayer.cpp`, `Panels/LogPanel.cpp`, `Panels/ProjectPanel.h`,
  `EditorLayer.h`) does NOT mention `Application.cpp` at all — PHASE8 must
  also delete/unconditionally-inline these three specific blocks in
  `Application.cpp`, or its own Step 6 final check ("only remaining
  occurrence... is the CMakeLists.txt `if(TRUE)` placeholder") will fail.**

**Test-side mirror of the SAME Logger real/stub split, NOT in PHASE8's own Step 2 file list either — flagged as a second gap:**

- `tests/Network/LogEndpointsEndToEndTests.cpp` — 1 pair:
  `#if GTE_ENABLE_EDITOR` (line 50), `#endif // GTE_ENABLE_EDITOR` (line 343).
- `tests/Network/NetworkRoutesTests.cpp` — 1 pair:
  `#if GTE_ENABLE_EDITOR` (line 1245), `#endif // GTE_ENABLE_EDITOR`
  (line 1328).
  Both wrap tests that only make sense when `Logger`'s REAL (non-stub)
  behavior is compiled in — a Bucket-C-shaped situation, just living under
  `tests/` instead of `src/Editor/`. PHASE8's own text does say "Files
  Touched" may include `tests/CMakeLists.txt` "if it also references
  GTE_ENABLE_EDITOR" (confirmed below that it does), but never mentions these
  two specific `.cpp` test files needing their OWN `#if`/`#endif` removed —
  flagging this explicitly so PHASE8 does not stop at the CMake list and miss
  these two C++ source-level directives.

**Total real C++ `#if`/`#else`/`#endif` GTE_ENABLE_EDITOR directive LINES
remaining in the whole repo today: 13** (3 in `Application.cpp`, 3 in
`Logger.h`, 2 in `Logger.cpp`, 2 in `LogEndpointsEndToEndTests.cpp`, 2 in
`NetworkRoutesTests.cpp`, 1 already counted... — precise per-line list above;
count of distinct `#if`/`#else`/`#endif` tokens: 3+3+2+2+2 = 12 physical
directive lines across 5 files, forming 5 logical `#if...#endif` regions).

### B) REAL CMake conditionals/definitions remaining (explicitly Phase 8/9's own job, never Phase 7's)

- Root `CMakeLists.txt`:
  - `option(GTE_ENABLE_EDITOR "Build with in-engine Editor/Debug UI (Dear ImGui)" ON)` — line 26.
  - `target_compile_definitions(gte_core PUBLIC GTE_ENABLE_EDITOR=$<BOOL:${GTE_ENABLE_EDITOR}>)` — line 766.
  - Six `if(GTE_ENABLE_EDITOR)` conditional blocks — lines 198, 613, 920, 932,
    948, 1041 (the main Editor-source-list block, plus five smaller
    shader/staging blocks).
- `tests/CMakeLists.txt`:
  - Two `if(GTE_ENABLE_EDITOR)` conditional blocks — lines 2168, 2227
    (Editor-only test sources, Editor-only `imgui` link).

All of the above are **exactly** what PHASE0/PHASE8 already expect to handle
— no surprise here.

### C) Comment/doc-string/prose occurrences — NOT live branches, but literally still contain the string

Every other match across both sweeps (~104 lines in ~50 files under `src/`,
~54 lines in ~30 files under `tests/`) is a plain-English comment or doc
string that mentions the macro's NAME as text — e.g. "Only built when
GTE_ENABLE_EDITOR is ON, since ...", historical notes about what a file used
to do behind the macro, or (see the "Discovered risk" section immediately
below) a literal, load-bearing runtime string. **None of these are
preprocessor directives and none change what code compiles** — but PHASE8's
own stated Definition of Done says *"Zero occurrence of the string
GTE_ENABLE_EDITOR anywhere in src/ or tests/"*, taken completely literally.
Flagging this loudly for PHASE8, exactly as PHASE6 flagged its own analogous
"the fallback string itself contains the substring GTE_ENABLE_EDITOR" nuance:
satisfying that Definition of Done LITERALLY requires touching on the order
of 150+ comment lines across ~80 files — a large, mechanical, low-risk prose
pass, distinct from (and much larger than) the 5 real code regions in
Section A above. This is PHASE8's own scope to size correctly, not something
this phase can or should pre-emptively edit (out of this phase's own declared
"Files Touched" list).

## Discovered risk — a REAL, LIVE runtime coupling to the literal string "GTE_ENABLE_EDITOR", not just a comment (flagged loudly for PHASE8, per Universal Rule 9)

`src/Network/NetworkServer.cpp`'s `POST /spawn_gpu_driven_test_batch` route
handler (confirmed by direct read, line 376) contains:

```cpp
res.status = (outcome.errorMessage.find("GTE_ENABLE_EDITOR") != std::string::npos) ? 503 : 400;
```

This is a **runtime substring search against the literal text
"GTE_ENABLE_EDITOR"**, used to decide whether a failed spawn request is
"structurally unavailable in this build" (503) versus "a caller mistake"
(400). It depends entirely on `src/Editor/NullEditorLayer.cpp`'s own
`SpawnGpuDrivenTestBatch()` fallback message literally containing that exact
substring: `"GPU-driven test batch spawning is not available in this build
(GTE_ENABLE_EDITOR is OFF)"` (confirmed by direct read).

**This is a real, live, behavior-affecting coupling — not a cosmetic
comment** — and it directly collides with PHASE8's own literal "zero
occurrence of the string GTE_ENABLE_EDITOR anywhere in src/" Definition of
Done: if PHASE8's prose-cleanup pass (Section C above) rewrites
`NullEditorLayer.cpp`'s fallback message to no longer contain the literal
macro name (which it must, to satisfy its own stated goal), THIS
`NetworkServer.cpp` check will silently start returning **400 instead of
503** for every GPU-driven-batch spawn attempt against a capability-less
build, with no compiler error or warning to catch the regression. By
contrast, the two structurally analogous checks in this same file
(`/save_scene`/`/load_scene`'s `outcome.editorAvailable`,
`/import_asset`'s `outcome.projectAvailable`) already use a real, dedicated
BOOL FIELD instead of a substring search, and are therefore completely
unaffected by any wording change to their own fallback messages.

**Recommendation for PHASE8 (not implemented here — outside this phase's own
scope and "Files Touched" list)**: before/while doing the prose cleanup pass,
either (a) give `SpawnGpuDrivenTestBatchOutcome`
(`src/Application/EditorUiCommandBridge.h`) a real, dedicated
`bool editorAvailable` field mirroring `SaveSceneOutcome`/
`ImportExternalFileOutcome`'s own precedent, with `NullEditorLayer.cpp`
setting it explicitly instead of relying on message-text sniffing, and update
`NetworkServer.cpp` to branch on that field instead of a substring search; or
(b) if keeping the substring-search shape for this one route is deliberately
accepted, at minimum keep the literal substring `"GTE_ENABLE_EDITOR"` itself
present, unchanged, in that one specific error message string as a documented,
intentional exception to the "zero occurrence" goal. Not deciding this here —
flagging it as a real, concrete choice PHASE8 (or a future
`ask_questions` call inside it) needs to make deliberately, not silently.

## Compile-check / smoke-check results

**Incremental compile check only, per campaign policy.**

- `cmake --build build --target gte_core` — `ninja: no work to do.` (expected
  — zero source files were modified this phase).
- `cmake --build build --target GreatTamanaEngine` — `ninja: no work to do.`
  (expected, same reason).
- Live smoke check: `run_app_background`'d `build/GreatTamanaEngine.exe`
  (PID 14076), then via `gte_send_request`:
  - `GET /list_tabs` — `200`,
    `{"tabs":["Hierarchy","Inspector","Scene","Game","Memory","Profiler","Render Graph","Jobs","Atmosphere","Log","Project"]}`.
  - `GET /activate_tab?name=Profiler` — `200`,
    `{"activated_tab":"Profiler","success":true}`.
  - **The GPU-driven test batch spawn endpoint** — confirmed its real,
    current name directly from `NetworkServer.cpp`'s own route table before
    calling it: `POST /spawn_gpu_driven_test_batch` (matches
    `AGENTS.md`'s/`README.md`'s own documented name, not a guess) —
    `POST /spawn_gpu_driven_test_batch` with body `{"instanceCount":6}` —
    `200`, `{"instance_count":6,"success":true}`.
  - `GET /get_logs?min_level=Warning&limit=50` — `200`, `count: 0`, no new
    warnings/errors from any of the above.
  - `GET /get_swapchain` — screenshot confirmed: 6 new
    `GpuDrivenTestBatch`/`GpuDrivenTestBatch (1..5)` entities visible in the
    Hierarchy panel, correctly rendered as a row of quads in both the Scene
    and Game panels, and the "Profiler" tab genuinely brought to the front
    (visible, active, showing live CPU Frame Time/Scopes data) — direct
    visual proof all three exercised endpoints are fully functional end-to-
    end, unaffected by (and requiring no help from) this phase's own
    "zero code change" conclusion.
  - `stop_app_background`'d the process (PID 14076) when done.

No `bug_report` was filed — no tool malfunctioned during this phase.

## Definition of Done — checklist

- [x] A full-repo `search_in_dir` for `GTE_ENABLE_EDITOR` performed across
      `src/`, `tests/`, and the root/`tests/` `CMakeLists.txt` files — every
      match individually classified (real directive vs. CMake conditional vs.
      comment/doc-string) — see the dedicated "Full-repo `search_in_dir`
      sweep" section above, which IS the exact, documented remaining-site
      inventory PHASE8 needs as its own starting point.
- [x] All exercised endpoints (`GET /activate_tab`, `GET /list_tabs`,
      `POST /spawn_gpu_driven_test_batch`) confirmed still working live, via
      both JSON response bodies and a visual `GET /get_swapchain` screenshot.
- [x] `PHASE7_COMPLETION_REPORT.md` written (this file).
- [x] Git commit (see commit that follows this report).

## Out of Scope (confirmed, unchanged)

- Did NOT delete the `GTE_ENABLE_EDITOR` CMake option or touch any Bucket C
  dead branch (`Logger.h/.cpp`, etc.) — that is PHASE8's job, which this
  report hands its exact starting inventory to.
- Did NOT build any new `IEditorUiCapability`/`IAssetImportCapability`/
  `IGpuDrivenBatchTestCapability` adapter — confirmed unnecessary, per the
  "Headline finding" section above (re-verifying PHASE5's own conclusion
  independently, not merely trusting it).
- Did NOT touch `Application.cpp`'s own three still-macro-gated wiring sites,
  `Logger.h/.cpp`'s Bucket C branches, or the two `tests/Network/*.cpp` test
  files' own `#if` blocks — all four are explicitly flagged above as
  PHASE8's job (with two of them being a genuine gap in PHASE8's own current
  file-list inventory that this report surfaces before PHASE8 starts, per
  Universal Rule 9).
- Did NOT attempt to fix the `NetworkServer.cpp` substring-search hazard
  described above — flagged as a real, concrete, undecided choice for PHASE8
  to make deliberately (or to raise via its own `ask_questions` call),
  not silently patched over here.

## Files touched

- NONE (zero source files modified — confirmed via `git_status`: "nothing to
  commit, working tree clean").
- NEW: `task_manager/editor-core-separation-1/PHASE7_COMPLETION_REPORT.md`
  (this file).
