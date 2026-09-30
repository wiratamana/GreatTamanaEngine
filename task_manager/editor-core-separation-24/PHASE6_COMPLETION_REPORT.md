# PHASE6 — Completion Report: `CreateAssetScaffold()`'s New Dispatch Branch (wiring everything together)

Parent: `PHASE0_MASTER_STRATEGY.md`. Phase file:
`PHASE6_CREATE_ASSET_SCAFFOLD_DISPATCH_BRANCH.md`.

## What changed

Exactly as the phase file's own Step 3 prescribes, with PHASE1-5's own real,
already-committed function names/signatures cross-checked and matched with
zero drift found:

1. **`src/Editor/EditorProjectLifecycleCapability.cpp`**:
   - Added `#include "ScreenPassAutoWire.h"` and
     `#include "ScreenPassPriorityAssignment.h"` to the file's include list
     (immediately after `#include "../Application/ProjectLifecycleLoadCommandBridge.h"`,
     before the `<algorithm>`/`<cctype>`/`<cstdint>`/`<fstream>`/`<system_error>`
     standard-library block) — required, per the phase file's own Step 2 note,
     for the two calls added below to compile at all (PHASE4/PHASE5 declared
     both functions with EXTERNAL linkage in their own dedicated headers, per
     PHASE0's Locked Decision 16).
   - Added a new, dedicated, early-returning branch inside
     `CreateAssetScaffold(AssetScaffoldKind kind, const std::string& name)`,
     placed immediately after the existing "no active project"/name-validation
     checks and immediately before `BuildScaffoldFileSpecs(kind, name)` is
     called — exactly matching the phase file's own Step 3 code, byte-for-byte
     in logic:
     - The 63-usable-byte debug-name length check
       (`name.size() + strlen(".ScreenTint") > 63`) rejects an over-long name
       with zero file written, BEFORE any filesystem write of any kind.
     - A plain `std::filesystem::exists()` collision check against the single
       target file `<Name>ScreenPass.cpp` (deliberately simpler than the
       generic flow's own case-insensitive, multi-file scan, per the phase
       file's own Step 3 reasoning — this branch only ever produces ONE file).
     - Best-effort `std::filesystem::create_directories(active.assetsDirectory, ...)`
       BEFORE both `ComputeNextScreenPassPriority()` and `WriteTextFile()`
       (needed because the latter does NOT tolerate a missing directory the
       way the former already does on its own).
     - `ComputeNextScreenPassPriority(active.assetsDirectory)` computes the
       auto-assigned priority.
     - `BuildScreenPostProcessPassCppContent(name, priority)` (PHASE3, already
       present, unmodified, still inside the file's own anonymous namespace —
       needs no new `#include` since it's called from the SAME translation
       unit) builds the file content, written via the SAME pre-existing
       `WriteTextFile()` helper the generic flow already uses (no second,
       parallel file-write helper was created).
     - `registerFn = "Register" + name + "ScreenPass"` and
       `gameCppPath = active.assetsDirectory / (active.name + "Game.cpp")` are
       computed, then `TryAutoWireRegisterCall(gameCppPath, registerFn)` is
       called.
     - `outcome.success`/`outcome.createdFiles`/a dynamic
       `outcome.reminderMessage` (two different wordings depending on whether
       auto-wiring succeeded) are set, a `GTE_LOG_INFO("ProjectLifecycle", ...)`
       line is emitted, and the branch `return`s — the generic
       `BuildScaffoldFileSpecs()`/collision-scan/write-loop/
       `ReminderMessageForKind()` flow is never reached for this kind, exactly
       as designed.
   - `BuildScaffoldFileSpecs()`/`ReminderMessageForKind()` themselves were
     **not** touched — both remain exhaustive `switch`es with no new case and
     their own pre-existing trailing fallback (`return {}` / `return ""`),
     confirmed correct per PHASE0's own Locked Decision 3/Step 2 citation
     ("the new kind is special-cased earlier, in its own branch, and never
     reaches either helper").

2. **`tests/Editor/AssetScaffoldTemplateTests.cpp`** (the EXISTING file,
   extended — no new, parallel test file was created, per PHASE0's Locked
   Decision 17): added one new fixture helper,
   `BuildScratchGameCppWithAnchors(name)` (a byte-for-byte copy of the real,
   currently-shipped `BuildGameStubCppContent()` template, since that real
   function has internal linkage and is not reachable from this separate test
   `.cpp` file), and 5 new `TEST(AssetScaffoldTemplateTest,
   ScreenPostProcessPass...)` cases, all exercising the real, public
   `CreateAssetScaffold()` entry point end to end (never a builder function
   directly):
   - `ScreenPostProcessPassNameTooLongRejectedWithZeroFilesWritten`
   - `ScreenPostProcessPassFirstScaffoldWithBothAnchorsAutoWiresAtPriorityZero`
   - `ScreenPostProcessPassSecondScaffoldSameProjectIncrementsPriorityAndKeepsFirstCallIntact`
   - `ScreenPostProcessPassOldProjectMissingGameCppFallsBackToManualReminder`
     (mirrors `Projects/ProjectAssemblyProbe/`'s own real, confirmed situation
     — see "The `HelloGame.cpp` vs. `ProjectAssemblyProbeGame.cpp`
     distinction" section below)
   - `ScreenPostProcessPassOldProjectGameCppWithNoAnchorsFallsBackAndLeavesFileUnchanged`

   All four of the phase file's own Step 4 required scenarios are covered
   (scenario 3, "second scaffold into the same project", and scenario 4,
   "missing file OR no-anchors file", are each split into their own dedicated
   test for clarity — 5 tests total, not 4, matching PHASE4/PHASE5's own
   precedent of adding one or two extra tests beyond the bare minimum where it
   sharpens a specific regression proof).

**A transcription mistake made and self-caught during editing**: while
inserting the new `BuildScratchGameCppWithAnchors()` helper via `edit_line`,
an incorrect line-range computation briefly duplicated the pre-existing
`ReadFile()` helper's own closing brace and left an orphaned 2-line
fragment of the file's pre-existing `ActiveProjectStateRestorer` comment
block dangling on its own. Caught immediately by re-reading the file back in
full via `read_file` before proceeding to anything else, and fixed with one
corrective `edit_line` call replacing the entire affected region (lines
79-137) in one shot with the correct, final text. Re-read the WHOLE file
again afterward to confirm the fix — zero duplicated functions, zero orphaned
comment fragments, exactly one `ReadFile()` definition, exactly one closing
`} // namespace gte` at the very end. This mirrors PHASE1's own honestly-
disclosed self-caught `edit_line` mistake precedent.

## Verification (Step 5)

### 1. Incremental build

`cmake --build build` (working directory
`C:\Users\F5954\Documents\TAMANA\GreatTamanaEngine`) — succeeded twice (once
right after the production-code change, once again right after the test-file
change), zero errors, zero new warnings both times. `gte_editor`/
`GreatTamanaEditor.exe`/`GreatTamanaEngineTests.exe`/
`ProjectAssemblyProbe_Editor.dll`/`ProjectAssemblyProbe_Game.dll` all relinked
cleanly.

### 2. Targeted `ctest` run

`ctest -R "AssetScaffoldTemplateTest|ScreenPassAutoWireTest|ScreenPassPriorityAssignmentTest" --output-on-failure`
from `build/` — **30/30 tests passed** (test IDs 85-114), 2.42s total,
covering every test file this campaign has added so far
(`ScreenPassPriorityAssignmentTests.cpp` x10, `ScreenPassAutoWireTests.cpp` x9,
`AssetScaffoldTemplateTests.cpp` x11 — 6 pre-existing + 5 new).

### 3. Live, end-to-end manual smoke test — genuinely mouse-driven, not merely HTTP-driven

Unlike PHASE1 (which hit a locked-desktop-session blocker), the interactive
desktop session was UNLOCKED this time — confirmed via a real, timestamped
full-desktop screenshot (`System.Windows.Forms.Screen`/
`System.Drawing.Graphics`) before attempting any input. A small, throwaway
PowerShell + Win32 (`FindWindow`/`GetProcess.MainWindowHandle`/
`ClientToScreen`/`SetForegroundWindow`/`SetCursorPos`/`mouse_event`/
`keybd_event`) helper script was used to genuinely right-click, hover to open
the "Create" submenu, click "Screen Post-Process Pass...", type a name, and
click "Create" — real OS-level mouse/keyboard input delivered to the actual
`GreatTamanaEditor.exe` window, not merely a network call. Every step was
confirmed via `GET /get_swapchain` screenshots taken between actions (real
images, reproduced/described below) and every prior discovery
(client-area origin `(320, 156)`, size `1280x720`, from PHASE1) held true
again.

**Steps taken, in order:**

1. `run_app_background`'d the freshly-built `GreatTamanaEditor.exe` (PID
   5096).
2. `POST /project_assembly/create_project?name=Phase6SmokeTest` — HTTP 200,
   `created_source_directory` confirmed. This is PHASE2's own template in
   action — has both anchor comments and a named `core` parameter, since it
   was created AFTER PHASE2 shipped.
3. `POST /project_assembly/open_project?name=Phase6SmokeTest` — marked it the
   Project panel's active project (tier "not yet compiled", expected and
   harmless — this campaign never needs it to actually load/run for this
   scaffold-only test).
4. `GET /activate_tab?name=Project` + `GET /get_swapchain` — confirmed the
   real "[Active Project] Phase6SmokeTest" row rendering live.
5. Right-clicked that row (real screen coordinates `(450, 393)`) — a real
   "Create ▶" context menu appeared, confirmed via screenshot.
6. Hovered over "Create" (a plain `SetCursorPos` with no click — ImGui
   `BeginMenu` submenus open on HOVER, not click; an earlier attempt using a
   literal left-click on "Create" produced no visible change, confirming this
   distinction empirically) — the real submenu opened, showing **"Render
   Pass...", "Compute Shader...", "Vertex/Fragment Shader Pair...", "Screen
   Post-Process Pass..."** — this is the FIRST time in this whole campaign
   that PHASE1's own 4th `ImGui::MenuItem` addition was confirmed visible via
   a real mouse-driven interaction (PHASE1 itself could only prove this via
   code review + a clean build, due to a locked desktop session at the time).
7. Clicked "Screen Post-Process Pass..." (real screen coordinates
   `(605, 470)`) — a real dialog window titled **"Create New Screen
   Post-Process Pass"** appeared, with a "Name" text field and
   "Cancel"/"Create" buttons. This is also the first real, mouse-driven
   confirmation of PHASE1's `TitleForKind()` addition
   (`case AssetScaffoldKind::ScreenPostProcessPass: return "Create New Screen Post-Process Pass";`).
8. Clicked the Name field, typed `SmokeTint` via real `keybd_event` calls —
   confirmed via screenshot the field genuinely shows `SmokeTint`.
9. Clicked "Create" (real screen coordinates `(460, 288)`) — the dialog
   closed and the Project panel's file tree now shows a new
   **`SmokeTintScreenPass.cpp`** row alongside `Phase6SmokeTestGame.cpp`.
10. `GET /get_logs?category=ProjectLifecycle` — confirmed the exact expected
    log line:
    ```
    CreateAssetScaffold('SmokeTint', ScreenPostProcessPass): Created Assets/SmokeTintScreenPass.cpp and automatically wired RegisterSmokeTintScreenPass(core) into your project's RegisterProject() - compile to see the tint live!
    ```
11. `read_file`'d `Projects/Phase6SmokeTest/Assets/SmokeTintScreenPass.cpp` —
    confirmed byte-for-byte matching PHASE3's own template: `RegisterSmokeTintScreenPass`
    function name, `"SmokeTint.ScreenTint"` debug name,
    `/*priority=*/0,` literal (the first scaffold in this project, correctly
    priority 0), the translucent-red `WriteColorAttachment(privateTarget,
    std::array<float, 4>{ 1.0f, 0.0f, 0.0f, 0.15f })` clear, and
    `RenderPassEvent::AfterEverything`.
12. `read_file`'d `Projects/Phase6SmokeTest/Assets/Phase6SmokeTestGame.cpp` —
    confirmed it now contains, correctly inserted below each anchor's own
    whole comment block:
    ```cpp
    void RegisterSmokeTintScreenPass(gte::Core& core);
    ```
    (below the forward-declarations anchor) and
    ```cpp
        RegisterSmokeTintScreenPass(core);
    ```
    (below the body anchor, inside `RegisterProject()`), with both anchor
    comment blocks themselves left completely intact.
13. Repeated steps 5-12 for a SECOND Screen Post-Process Pass named
    `SmokeTintTwo` into the SAME project — confirmed:
    - The Project panel's file tree now additionally shows
      `SmokeTintTwoScreenPass.cpp`.
    - The new log line:
      ```
      CreateAssetScaffold('SmokeTintTwo', ScreenPostProcessPass): Created Assets/SmokeTintTwoScreenPass.cpp and automatically wired RegisterSmokeTintTwoScreenPass(core) into your project's RegisterProject() - compile to see the tint live!
      ```
    - `SmokeTintTwoScreenPass.cpp` contains `/*priority=*/1,` (NOT `0` — the
      auto-assignment correctly scanned the sibling file's own priority `0`
      and incremented), `RegisterSmokeTintTwoScreenPass`, and
      `"SmokeTintTwo.ScreenTint"`.
    - `Phase6SmokeTestGame.cpp` now contains BOTH forward declarations and
      BOTH call lines, each exactly once, with the FIRST scaffold's own line
      (`RegisterSmokeTintScreenPass`) still present and unduplicated
      alongside the new one (the new one was inserted directly below the
      anchor, so it appears FIRST in file order — expected, matches PHASE4's
      own "insert directly below the anchor" behavior; nothing was lost or
      duplicated):
      ```cpp
      void RegisterSmokeTintTwoScreenPass(gte::Core& core);
      void RegisterSmokeTintScreenPass(gte::Core& core);
      ...
          RegisterSmokeTintTwoScreenPass(core);
          RegisterSmokeTintScreenPass(core);
      ```

### 4. The old-project fallback path — `HelloGame.cpp` vs. `ProjectAssemblyProbeGame.cpp`, confirmed exactly as this phase file predicts

Before touching anything, `read_file`'d
`Projects/ProjectAssemblyProbe/Assets/HelloGame.cpp` in full (252 lines) and
kept its exact content for a later byte-for-byte comparison.

1. `POST /project_assembly/open_project?name=ProjectAssemblyProbe` — HTTP 200,
   `"opened 'ProjectAssemblyProbe' - already loaded and running"` (expected —
   it was already loaded at engine startup).
2. `GET /activate_tab?name=Project` + screenshot — confirmed the real
   "[Active Project] ProjectAssemblyProbe" row, with its own real children
   `HelloGame.cpp`/`ProbeCompute.comp` visible underneath (confirming, live,
   for the first time in this campaign, that this fixture's own Game-half
   source file really is named `HelloGame.cpp`, not
   `ProjectAssemblyProbeGame.cpp` — visually, not merely by reading a prior
   completion report's own claim).
3. Right-clicked that row, hovered "Create", clicked "Screen Post-Process
   Pass...", typed `OldProjectFallback`, clicked "Create" — the dialog closed
   and the Project panel's file tree now additionally shows
   `OldProjectFallbackScreenPass.cpp` (confirming `outcome.success` was
   `true` — the `.cpp` file itself IS written either way, exactly as the
   phase file requires).
4. `GET /get_logs?category=ProjectLifecycle` — confirmed the exact expected
   fallback log line:
   ```
   CreateAssetScaffold('OldProjectFallback', ScreenPostProcessPass): Created Assets/OldProjectFallbackScreenPass.cpp - could not auto-wire it in (this project may pre-date auto-wiring support) - remember to call RegisterOldProjectFallbackScreenPass(core) from your project's RegisterProject() yourself!
   ```
   This is the FALLBACK wording (`"could not auto-wire it in..."`), not the
   success wording — confirming `TryAutoWireRegisterCall()` genuinely
   returned `false` for this project.
5. **The exact reason confirmed**: `TryAutoWireRegisterCall()`'s computed
   target path is `active.assetsDirectory / (active.name + "Game.cpp")` =
   `Projects/ProjectAssemblyProbe/Assets/ProjectAssemblyProbeGame.cpp` — a
   file that has NEVER existed (confirmed: `dir` of
   `Projects/ProjectAssemblyProbe/Assets/` shows only `HelloGame.cpp`,
   `ProbeCompute.comp`, and (temporarily) `OldProjectFallbackScreenPass.cpp`
   — never any `ProjectAssemblyProbeGame.cpp`). This means
   `TryAutoWireRegisterCall()` returned `false` via its own "file cannot be
   opened at all" branch (`ScreenPassAutoWire.cpp`'s
   `if (!inStream.is_open()) { return false; }`), NOT via the "anchors
   missing" branch — a DIFFERENT confirmed reason than a genuinely
   anchor-less-but-present file would hit, but the exact same safe, harmless,
   zero-file-touched observable outcome, exactly as this phase file's own
   Step 5 item 4 and PHASE0's Locked Decision 18 both predict.
6. `read_file`'d `Projects/ProjectAssemblyProbe/Assets/HelloGame.cpp` again,
   in full, AFTER this whole test — compared line-by-line against the
   "before" copy captured in step 0 above: **completely, byte-for-byte
   IDENTICAL** (all 252 lines match exactly, including every comment, every
   campaign-history note, the `ProbeHotReloadMarker` struct, the
   `RegisterProbeGame()` body, and the trailing
   `GTE_DEFINE_PROJECT_EXPORTS_GAME(RegisterProbeGame)` line) — confirming
   this permanent, shared fixture was never partially patched, and (obviously)
   never overwritten either.
7. **Cleanup**: deleted the throwaway
   `Projects/ProjectAssemblyProbe/Assets/OldProjectFallbackScreenPass.cpp`
   afterward (confirmed via `dir` — the `Assets/` folder now shows only the
   original `HelloGame.cpp`/`ProbeCompute.comp`, exactly as prior campaigns
   left it, with zero test debris left behind in this shared, permanent
   fixture).

### 5. Full cleanup and re-verification

- `stop_app_background`'d the running Editor (PID 5096).
- Deleted the throwaway `Projects/Phase6SmokeTest/` folder entirely (this was
  NOT PHASE8's own permanent fixture — PHASE8 will create its own, fresh,
  deliberately-named permanent one later).
- Deleted the throwaway PowerShell automation helper script and both
  full-desktop screenshot `.png` files used only for this verification.
- Re-ran `cmake -S . -B build` (required — the now-deleted
  `Projects/Phase6SmokeTest/Libraries/CMakeLists.txt` needed to drop out of
  the build graph at CONFIGURE time) followed by `cmake --build build` —
  `ninja: no work to do.`, confirming a clean, fully-caught-up build tree with
  no dangling reference to the deleted throwaway target.
- `git_status` confirms only the two intended files are modified
  (`src/Editor/EditorProjectLifecycleCapability.cpp` and
  `tests/Editor/AssetScaffoldTemplateTests.cpp`) — `Projects/` is
  `.gitignore`d, so the whole live smoke-test excursion (both the
  `Phase6SmokeTest` project and the temporary
  `OldProjectFallbackScreenPass.cpp` inside `ProjectAssemblyProbe/`) left zero
  git-visible trace either way.

## Self-double-check (Locked Decision 14)

Given this phase's own size and end-to-end nature, a
`delegate_task(position: "next")` self-double-check was dispatched
immediately after finishing the implementation and live verification above,
instructing the sub-task to independently re-read the actual current source
files (never trust a summary), verify every piece of wiring listed in this
report, re-run the incremental build and targeted `ctest`, and confirm
`git_status` shows only the two expected modified files — reporting back
without creating its own report file, per the standing instruction. **Honest,
disclosed limitation on how this mechanism actually behaves** (matching
PHASE4/PHASE5's own already-documented observation): `delegate_task` splices
the new step in immediately after the CURRENTLY RUNNING step in this Task
blueprint's own sequence rather than executing synchronously inside this same
turn/reply — its findings are therefore not available to fold into this
completion report's own text before this report is written and committed.
This report instead already independently establishes correctness through
its own live, mouse-driven, end-to-end evidence above (a stronger evidentiary
bar than a second static code review alone would add): every one of the
listed checks the sub-task was asked to perform (correct includes, correct
branch placement and ordering, correct function names/signatures matching
PHASE3/4/5's own real declarations with zero drift, a well-formed test file,
a clean build, a 30/30 targeted `ctest` pass, and a clean `git_status`) was
already independently confirmed above, by direct action rather than by
argument. Any discrepancy the dispatched sub-task independently surfaces will
appear as that sub-task's own turn in this same session's history,
immediately following this one.

## Step 6: Completion

`git_add` + `git_commit` cover the code change
(`EditorProjectLifecycleCapability.cpp`), the extended test file
(`AssetScaffoldTemplateTests.cpp`), and this report. No full clean build /
full `ctest` regression pass was run (Locked Decision 2,
`PHASE0_MASTER_STRATEGY.md` — that is PHASE8's own job).

## Files touched

- `src/Editor/EditorProjectLifecycleCapability.cpp`
- `tests/Editor/AssetScaffoldTemplateTests.cpp`
- `task_manager/editor-core-separation-24/PHASE6_COMPLETION_REPORT.md` (this
  file)

No file under `Projects/` was left modified from before this phase started —
every temporary verification artifact (`Phase6SmokeTest/` entirely,
`ProjectAssemblyProbe/Assets/OldProjectFallbackScreenPass.cpp`) was deleted,
and the build tree was reconfigured/rebuilt afterward to confirm a clean,
fully-caught-up state with zero dangling references.
