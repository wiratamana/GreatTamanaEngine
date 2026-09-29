# PHASE1 — Scaffold Kind Enum + Editor UI Entry Points

Parent: `PHASE0_MASTER_STRATEGY.md` (read it first, in full, plus its own
Locked Decisions section — every rule there applies here too).
Design doc Step: Step 2 of
`DESIGN_REQUIREMENTS_ONSCREEN_COMPOSITING_BIGSTEP_02_EDITOR_INTEGRATION_2026-09-29.txt`.

## Step 1: The Goal

Add the fourth, new `AssetScaffoldKind` enumerator
(`ScreenPostProcessPass`) and make it visible/clickable in the Editor's UI —
with NO real scaffolding behavior behind it yet (that lands in PHASE6). After
this phase, a developer can right-click the active project row, open
"Create -> Screen Post-Process Pass...", see the correct window title, type a
name, and click "Create" — it will currently report success while writing
ZERO files (an honest, temporary, INTRA-CAMPAIGN state — never shipped or
released independently; every later phase in this same campaign closes this
gap before PHASE8's final verification). This mirrors
`editor-core-separation-23`'s own PHASE1, which similarly shipped a compiling,
low-risk "shape" change before any heavier logic landed behind it.

## Step 2: The Situation

- `src/Core/EditorCapabilities.h` line 360:
  `enum class AssetScaffoldKind { RenderPass, ComputeShader, ShaderPair };`
- `src/Editor/CreateAssetWindow.cpp` lines 10-19:
  ```cpp
  const char* TitleForKind(AssetScaffoldKind kind)
  {
      switch (kind) {
      case AssetScaffoldKind::RenderPass: return "Create New Render Pass";
      case AssetScaffoldKind::ComputeShader: return "Create New Compute Shader";
      case AssetScaffoldKind::ShaderPair: return "Create New Vertex/Fragment Shader Pair";
      }
      return "Create New Asset"; // unreachable - silences a "not all control paths return a value" warning.
  }
  ```
- `src/Editor/Panels/ProjectPanel.cpp` lines 253-266 (inside
  `RenderActiveProjectAssetsRow()`'s `ImGui::BeginMenu("Create")` block):
  ```cpp
  if (ImGui::BeginMenu("Create")) {
      if (ImGui::MenuItem("Render Pass...")) {
          ctx.createAssetWindowPendingKind = AssetScaffoldKind::RenderPass;
          ctx.createAssetWindowOpen = true;
      }
      if (ImGui::MenuItem("Compute Shader...")) {
          ctx.createAssetWindowPendingKind = AssetScaffoldKind::ComputeShader;
          ctx.createAssetWindowOpen = true;
      }
      if (ImGui::MenuItem("Vertex/Fragment Shader Pair...")) {
          ctx.createAssetWindowPendingKind = AssetScaffoldKind::ShaderPair;
          ctx.createAssetWindowOpen = true;
      }
      ImGui::EndMenu();
  }
  ```
- `src/Editor/EditorProjectLifecycleCapability.cpp`'s `BuildScaffoldFileSpecs()`
  (lines 250-267) and `ReminderMessageForKind()` (lines 269-280) are BOTH
  exhaustive `switch`es over `AssetScaffoldKind`, each with a trailing
  fallback `return {}`/`return ""` after the switch — confirmed: adding the
  new enumerator WITHOUT a case in either function compiles cleanly today
  (no `-Wswitch` in this build) and simply falls through to that trailing
  return. This is intentional and correct for this phase (see Step 3 below).

## Step 3: The Plan

### 3.1 — `src/Core/EditorCapabilities.h`

Change line 360 to:
```cpp
enum class AssetScaffoldKind { RenderPass, ComputeShader, ShaderPair, ScreenPostProcessPass };
```
Append it at the END of the enum (never insert in the middle) — this is an
`enum class` with no explicit numeric values assigned anywhere in this
codebase's own usage (confirmed: no serialization/persistence of this enum's
underlying integer value exists anywhere — it is only ever compared by name),
so ordering is a pure style/readability choice, not a compatibility hazard,
but appending at the end still matches this file's own existing "declare in
the same order the three original kinds were added" convention.

No doc-comment rewrite is needed on `IAssetScaffoldingCapability` itself — its
own doc comment already describes the CreateAssetScaffold() contract
generically ("Callable from ANY thread...") without enumerating specific
kinds by name.

### 3.2 — `src/Editor/CreateAssetWindow.cpp`

Add a REAL case to `TitleForKind()` (this one is called unconditionally for
every kind including the new one — a silent fallback to the generic "Create
New Asset" title here would be a real, user-visible regression):
```cpp
const char* TitleForKind(AssetScaffoldKind kind)
{
    switch (kind) {
    case AssetScaffoldKind::RenderPass: return "Create New Render Pass";
    case AssetScaffoldKind::ComputeShader: return "Create New Compute Shader";
    case AssetScaffoldKind::ShaderPair: return "Create New Vertex/Fragment Shader Pair";
    case AssetScaffoldKind::ScreenPostProcessPass: return "Create New Screen Post-Process Pass";
    }
    return "Create New Asset"; // unreachable - silences a "not all control paths return a value" warning.
}
```

### 3.3 — `src/Editor/Panels/ProjectPanel.cpp`

Add a 4th `ImGui::MenuItem`, alongside the existing three, inside the same
`ImGui::BeginMenu("Create")` block:
```cpp
if (ImGui::MenuItem("Screen Post-Process Pass...")) {
    ctx.createAssetWindowPendingKind = AssetScaffoldKind::ScreenPostProcessPass;
    ctx.createAssetWindowOpen = true;
}
```
Place it AFTER "Vertex/Fragment Shader Pair..." (matching the enum's own
append-at-the-end ordering from 3.1, and matching the source design
document's own presentation order).

### 3.4 — Deliberately UNTOUCHED in this phase

- `BuildScaffoldFileSpecs()`/`ReminderMessageForKind()`
  (`EditorProjectLifecycleCapability.cpp`) — no new case added. Confirm (by
  reading the file fresh, not from memory) that both still compile and both
  still have their own trailing fallback return reached safely for the new
  enumerator. This is intentional, not an oversight — PHASE6 special-cases
  `ScreenPostProcessPass` INSIDE `CreateAssetScaffold()` itself, BEFORE either
  helper is ever called for that kind, so neither helper is EVER actually
  invoked with the new enumerator in the final, complete feature. Do not
  "fix" this by adding cases here — that would just be unreachable dead code.
- `CreateAssetScaffold()`'s own body — untouched this phase. Clicking
  "Create" for a Screen Post-Process Pass right now routes into the EXISTING
  generic flow (`BuildScaffoldFileSpecs(ScreenPostProcessPass, name)` returns
  an empty vector via its trailing fallback, so the write loop iterates zero
  times, and the call reports `outcome.success = true` with
  `outcome.createdFiles` empty and `outcome.reminderMessage` empty) — a
  known, harmless, temporary no-op for the remainder of this campaign until
  PHASE6 lands. Do not treat this as a bug to fix in this phase.
- `POST /project_assembly/create_asset`'s `kindParam` chain
  (`NetworkServer.cpp`) — untouched this phase; PHASE7's job.

## Step 4: Verification (this phase only)

1. Incremental build (`cmake --build build`) — confirm it compiles cleanly,
   zero new warnings.
2. Live, manual UI check: `run_app_background` the built
   `GreatTamanaEditor.exe`, open (or create) a project so the "[Active
   Project] `<Name>`" row exists, right-click it, confirm "Screen
   Post-Process Pass..." now appears as a 4th "Create" submenu entry, click
   it, confirm the window title reads exactly "Create New Screen
   Post-Process Pass". Type a name and click "Create" — confirm it reports
   success with a message describing 0 files created (the expected,
   temporary no-op described in 3.4 above) rather than crashing or showing a
   wrong title. `stop_app_background` when done.
3. Confirm via `GET /get_logs` that nothing unexpected was logged (no new
   `GTE_LOG_ERROR`/`WARNING` lines attributable to this change).
4. No new Tier-1 test is required for this phase — it is a pure UI/enum
   shape change with no new logic branch of its own (the string-returning
   functions touched are already implicitly covered by this repo's existing
   manual-UI-verification convention for `CreateAssetWindow.cpp`, which has
   no dedicated automated test suite today either).

## Step 5: Completion

Write `PHASE1_COMPLETION_REPORT.md` in this same folder (what changed, the
live verification evidence, anything discovered). `git_add` + `git_commit`
covering the code change and the report. Do not run a full build/regression
here (Locked Decision 2, PHASE0).
