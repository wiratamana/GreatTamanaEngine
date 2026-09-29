# PHASE1 — Completion Report: Scaffold Kind Enum + Editor UI Entry Points

Parent: `PHASE0_MASTER_STRATEGY.md`. Phase file: `PHASE1_SCAFFOLD_KIND_ENUM_AND_EDITOR_UI_ENTRY_POINTS.md`.

## What changed

All three edits exactly match the phase file's own Step 3 (3.1/3.2/3.3), verified
against the real file contents immediately before editing (no drift from PHASE0's
own Step 2 "Situation" citations was found).

1. **`src/Core/EditorCapabilities.h`** — `AssetScaffoldKind` gained a 4th
   enumerator, appended at the end:
   ```cpp
   enum class AssetScaffoldKind { RenderPass, ComputeShader, ShaderPair, ScreenPostProcessPass };
   ```

2. **`src/Editor/CreateAssetWindow.cpp`** — `TitleForKind()` gained a real,
   non-fallback case:
   ```cpp
   case AssetScaffoldKind::ScreenPostProcessPass: return "Create New Screen Post-Process Pass";
   ```

3. **`src/Editor/Panels/ProjectPanel.cpp`** — `RenderActiveProjectAssetsRow()`'s
   "Create" submenu gained a 4th `ImGui::MenuItem`, placed after
   "Vertex/Fragment Shader Pair...":
   ```cpp
   if (ImGui::MenuItem("Screen Post-Process Pass...")) {
       ctx.createAssetWindowPendingKind = AssetScaffoldKind::ScreenPostProcessPass;
       ctx.createAssetWindowOpen = true;
   }
   ```

**Deliberately untouched, confirmed correct per Step 3.4**: `BuildScaffoldFileSpecs()`/
`ReminderMessageForKind()` in `EditorProjectLifecycleCapability.cpp` (re-read fresh,
lines 250-267/269-280) — both remain exhaustive `switch`es with no new case and their
own trailing fallback (`return {}` / `return ""`) unchanged; `CreateAssetScaffold()`'s
own body; and `NetworkServer.cpp`'s `POST /project_assembly/create_asset` `kindParam`
chain (PHASE7's job). None of these were modified this phase.

## A self-caught editing mistake, fixed before verification

While applying edit #1, an `edit_line` call targeting the wrong 0-based line index
(the tool's own line numbering is 0-based; I initially miscounted and hit a comment
line one above the real enum declaration) briefly duplicated the enum line and
clobbered a trailing comment line (`// interface answers 503.`). Caught immediately
via `git diff` before doing anything else, and corrected with a follow-up `edit_line`
call that restored the original comment line and put the new enum declaration on the
correct line. A similar mis-target happened once during edit #3 (`ProjectPanel.cpp`),
where the auto-dedup safety net (correctly, as designed) removed what it detected as
a duplicate boundary line, but that removal happened to delete a REAL, needed line
from the pre-existing `ShaderPair` `MenuItem` block two iterations in a row. Rather
than keep patching line-by-line, I re-read the surrounding block fresh and replaced
the entire `if (ImGui::BeginMenu("Create")) { ... }` block (lines 253-270) in one
shot, then confirmed via `git diff` that the final diff contains ONLY the intended
new 4-line `MenuItem` addition with zero collateral change to the three pre-existing
`MenuItem` blocks. Final `git diff` for both files, reproduced below, confirms both
edits ended up exactly correct with no leftover damage:

```diff
--- a/src/Core/EditorCapabilities.h
+++ b/src/Core/EditorCapabilities.h
@@ -358,7 +358,7 @@ public:
 // every capability interface above: gte_core-tier code (NetworkServer.cpp)
 // holds only a nullable pointer; nullptr means every route backed by this
 // interface answers 503.
-enum class AssetScaffoldKind { RenderPass, ComputeShader, ShaderPair };
+enum class AssetScaffoldKind { RenderPass, ComputeShader, ShaderPair, ScreenPostProcessPass };

 class IAssetScaffoldingCapability {
 public:
```

```diff
--- a/src/Editor/CreateAssetWindow.cpp
+++ b/src/Editor/CreateAssetWindow.cpp
@@ -15,6 +15,7 @@ const char* TitleForKind(AssetScaffoldKind kind)
     case AssetScaffoldKind::RenderPass: return "Create New Render Pass";
     case AssetScaffoldKind::ComputeShader: return "Create New Compute Shader";
     case AssetScaffoldKind::ShaderPair: return "Create New Vertex/Fragment Shader Pair";
+    case AssetScaffoldKind::ScreenPostProcessPass: return "Create New Screen Post-Process Pass";
     }
     return "Create New Asset"; // unreachable - silences a "not all control paths return a value" warning.
 }
```

```diff
--- a/src/Editor/Panels/ProjectPanel.cpp
+++ b/src/Editor/Panels/ProjectPanel.cpp
@@ -264,6 +264,10 @@ void ProjectPanel::RenderActiveProjectAssetsRow(EditorContext& ctx)
                 ctx.createAssetWindowPendingKind = AssetScaffoldKind::ShaderPair;
                 ctx.createAssetWindowOpen = true;
             }
+            if (ImGui::MenuItem("Screen Post-Process Pass...")) {
+                ctx.createAssetWindowPendingKind = AssetScaffoldKind::ScreenPostProcessPass;
+                ctx.createAssetWindowOpen = true;
+            }
             ImGui::EndMenu();
         }
         ImGui::EndPopup();
```

## Verification (Step 4)

1. **Incremental build** — `cmake --build build`: succeeded, zero errors, zero new
   warnings. `gte_core`/`gte_editor`/`GreatTamanaEditor.exe`/`GreatTamanaEngineTests.exe`
   all relinked cleanly (34/34 build steps).

2. **Live HTTP-driven UI verification — PARTIAL, with an honestly-documented gap.**
   `run_app_background`'d the freshly-built `GreatTamanaEditor.exe`, confirmed via
   `GET /project_assembly/list_projects` that `ProjectAssemblyProbe` was already
   loaded, called `POST /project_assembly/open_project?name=ProjectAssemblyProbe`
   to make it the Project panel's "active" project (a separate concept from
   "loaded"), then `GET /activate_tab?name=Project` + `GET /get_swapchain`
   confirmed the real, live "[Active Project] ProjectAssemblyProbe" row now
   renders in the Project panel exactly as `RenderActiveProjectAssetsRow()`
   describes — this part is genuine, real, screenshot-confirmed evidence, not
   assumed.

   **What could NOT be completed**: actually right-clicking that row, opening the
   "Create" submenu, and clicking "Screen Post-Process Pass..." requires a real OS
   mouse click delivered to the actual `GreatTamanaEditor.exe` window. I attempted
   this via a `PowerShell`+Win32 (`SetForegroundWindow`/`SetCursorPos`/`mouse_event`)
   script targeting the window's real screen coordinates (confirmed via
   `GetWindowRect`/`GetClientRect`/`ClientToScreen` — client area origin
   `(320, 156)`, size `1280x720`, matching `GET /get_swapchain`'s own image
   dimensions 1:1). The click produced no visible effect. Investigating why, a real
   full-desktop screenshot (`System.Windows.Forms.Screen`/`System.Drawing.Graphics`)
   revealed **this machine's interactive desktop session is currently LOCKED**
   (the Windows lock screen, in Japanese: "ロックを解除するには Ctrl + Alt + Del
   キーを押してください") — confirmed twice, several minutes apart, with the clock
   readout advancing between the two captures, proving it is a live, current lock
   state, not a stale cached image. A locked session does not deliver
   `SendInput`/`mouse_event`-injected input to the underlying application window at
   all (it goes to the lock screen's own `LogonUI.exe` instead) — this is a Windows
   security boundary, not a bug in my script or in the engine.

   Per the standing project rule, I used `ask_questions` to ask how to proceed
   (unlock the machine and retry / accept a code-review-plus-build-only verification
   with an honest limitation note / build a temporary debug HTTP hook instead of
   real mouse simulation) — **the question window timed out after 30 minutes with
   no response (user absent)**. Proceeding per the tool's own fallback instruction
   ("proceed as best you can without this input"): I did NOT add any temporary debug
   HTTP hook (that would be new, permanent-shaped production code invented outside
   this phase's own written Plan, exactly what Locked Decision 2/3.4 caution
   against), and I did NOT keep retrying a mouse click that structurally cannot
   reach a locked session. Instead, this phase's actual correctness is established
   by: (a) a byte-for-byte diff match against the phase file's own prescribed Step 3
   code, re-confirmed above: `TitleForKind()`'s new case is a plain, unconditional
   string return with no branching logic to get wrong, and the new `MenuItem` is a
   trivial copy of the three pre-existing ones with only the enum value/label
   swapped; (b) the zero-error/zero-warning clean incremental build proving the enum
   value and both new switch/if call sites compile and link correctly against every
   existing call site of `AssetScaffoldKind` in the codebase; (c) live confirmation
   that the Project panel's own active-project row mechanism (the thing the new
   menu item is attached to) is genuinely working end-to-end today, not merely
   assumed. **What remains genuinely unverified by an actual human/mouse click**:
   the literal on-screen appearance/click-through of the new "Screen Post-Process
   Pass..." menu item and the resulting window title text, purely because no
   available tool in this environment can deliver real input to a locked Windows
   session. This is a known, disclosed gap for this phase only — PHASE6/PHASE7/
   PHASE8 all build real, testable logic behind this same UI, and PHASE8's own
   final live verification (Locked Decision 2 — the ONE phase allowed a full
   regression pass) is the right place to re-attempt a genuine mouse-driven check
   if the desktop is unlocked by then.

3. **`GET /get_logs?min_level=Warning`** — reviewed all entries produced during this
   session; every one is a pre-existing startup warning (demo render-feature-plugin
   priority collisions, GPU-timing-slot-budget exhaustion for various already-known
   pass names) from frame 0-1, none attributable to this phase's change, and none
   newly appearing that weren't already an expected, documented pre-existing
   condition of this build.

4. **No new Tier-1 test** — confirmed correct per the phase file's own Step 4 item 4:
   this is a pure UI/enum shape change with no new logic branch of its own.

## Discovered, not a bug in this phase's own scope

The Project panel's "[Active Project] `<Name>`" row does not appear merely because a
Project Assembly's `.dll`s are loaded at engine startup — it requires a SEPARATE
"active project" state (`ActiveProjectAssemblyState`), set via
`POST /project_assembly/open_project?name=<X>`. This is pre-existing,
`editor-core-separation-18`-era behavior, not something this phase touches or needs
to fix — noted here only because it was momentarily surprising during verification
(the row was absent from the very first screenshot until `open_project` was called).

## Files touched

- `src/Core/EditorCapabilities.h`
- `src/Editor/CreateAssetWindow.cpp`
- `src/Editor/Panels/ProjectPanel.cpp`
- `task_manager/editor-core-separation-24/PHASE1_COMPLETION_REPORT.md` (this file)

No full clean build / full `ctest` regression pass was run (Locked Decision 2 — that
is PHASE8's own job).
