# editor-core-separation-19 — PHASE1 COMPLETION REPORT
## On-Engine Project Workflow — BIG-STEP 5 of 5: "Compile" Menu Item

Status: **DONE**. Scoped compile check green. This is a single-phase
implementation report (not a full campaign closeout — that is PHASE2's own
job).

## What was actually built

Exactly what `PHASE1_CAPABILITY_WIRING_AND_COMPILE_MENU_ITEM.md` asked for,
edit-for-edit, in the exact order/locations it specified:

- **`src/Core/Plugins/ProjectAssemblyBuildRunner.h`** — new declaration
  `bool IsProjectAssemblyBuildInFlight(const std::string& projectName);`,
  added immediately after `TriggerProjectAssemblyCompile()`'s own
  declaration.
- **`src/Core/Plugins/ProjectAssemblyBuildRunner.cpp`** — the implementation,
  placed immediately after `TriggerProjectAssemblyCompile()`'s own
  definition, OUTSIDE the file's anonymous namespace (so it is callable from
  other translation units) but still inside the same `.cpp` (so
  `g_inFlightMutex`/`g_inFlightProjects` are directly visible). A plain
  4-line, read-only, lock-guarded set-membership check — mutates nothing.
- **`src/Core/EditorCapabilities.h`** — `IHotReloadDebugCapability` gains a
  new pure virtual `bool IsCompileInFlight(const std::string& projectName)
  const = 0;`, inserted immediately after `TriggerCompileOnly()`'s own
  declaration, before `TriggerHotReload()`.
- **`src/Editor/EditorHotReloadDebugCapability.h`** — new override
  declaration `bool IsCompileInFlight(const std::string& projectName) const
  override;`, immediately after `TriggerCompileOnly()`'s own declaration.
- **`src/Editor/EditorHotReloadDebugCapability.cpp`** — the override body,
  a thin one-line wrapper calling `IsProjectAssemblyBuildInFlight()` (no new
  `#include` needed — `ProjectAssemblyBuildRunner.h` was already included
  for `TriggerCompileOnly()`'s own body).
- **`src/Editor/EditorLayer.h`** — TWO edits, in the exact order the phase
  file demanded:
  1. **A brand-new forward declaration**, `class IHotReloadDebugCapability;`,
     added immediately after `class IAssetScaffoldingCapability;` — this
     type genuinely did NOT exist anywhere in this header before this
     phase, confirmed by direct inspection before writing any code, exactly
     as the phase file's own explicit warning said. Skipping this would
     have broken the build the moment Edit 2 (below) referenced the
     type.
  2. The new pure virtual setter, `virtual void
     SetHotReloadDebugCapability(IHotReloadDebugCapability* capability) =
     0;`, added immediately after `SetAssetScaffoldingCapability()`.
- **`src/Editor/NullEditorLayer.cpp`** — the no-op override,
  `void SetHotReloadDebugCapability(IHotReloadDebugCapability*
  /*capability*/) override { }`, mirroring
  `SetAssetScaffoldingCapability()`'s own precedent immediately above it.
- **`src/Editor/ImGuiEditorLayer.cpp`** — three edits: (1) new member
  `IHotReloadDebugCapability* m_hotReloadDebugCapability = nullptr;`
  immediately after `m_assetScaffoldingCapability`; (2) the real override
  storing the pointer; (3) the single real call site of
  `BuildDockspaceAndMenuBar()` (confirmed, via `search_in_dir`, to be the
  ONLY real call site anywhere in the codebase) updated to pass
  `m_hotReloadDebugCapability` as its new 4th argument.
- **`src/Editor/EditorHost.cpp`** — one new line,
  `m_editorLayer->SetHotReloadDebugCapability(&s_editorHotReloadDebugCapability);`,
  added immediately after the existing
  `SetAssetScaffoldingCapability(&s_editorProjectLifecycleCapability);` call
  — reuses the SAME `s_editorHotReloadDebugCapability` static already passed
  into `NetworkServer`'s 8th constructor argument.
- **`src/Editor/DockLayout.h`** — new forward declaration
  `class IHotReloadDebugCapability;` and `BuildDockspaceAndMenuBar()`'s
  signature extended with a new 4th parameter,
  `IHotReloadDebugCapability* hotReloadDebugCapability`.
- **`src/Editor/DockLayout.cpp`** — the real "Project > Compile" menu item,
  done as the phase file's own mandated TWO SEPARATE, NON-ADJACENT edits:
  - **Edit A**: deleted the stale, 9-line
    "editor-core-separation-16 campaign - reserved, disabled placeholder..."
    comment block (the original lines 179-187) and inserted nothing in its
    place.
  - **Edit B**: replaced ONLY the original placeholder line,
    `if (ImGui::MenuItem("Compile", nullptr, false, false)) {}`, with the
    real block: reads `ActiveProjectAssemblyState::Instance().GetActive()`,
    computes `buildInFlight`/`compileEnabled`/`compileLabel` (with the
    `" (compiling...)"` suffix, never disabling the item while a build
    runs — Step 3.4's deliberate, disclosed design choice), and on click
    calls `hotReloadDebugCapability->TriggerCompileOnly(active.name)`,
    setting `ctx.projectWorkflowStatusMessage`/`...IsError`/`...SetTime`
    exactly like `NewProjectWindow.cpp`/`OpenProjectWindow.cpp` already do.
  - The real, working "Open Project..." menu item block (its own
    "editor-core-separation-17 campaign... 'Open Project...' is now real"
    comment, the real `if (ImGui::MenuItem("Open Project..."))` body, and
    the pre-existing `ImGui::Separator();`) sits, byte-for-byte unchanged,
    immediately between Edit A and Edit B — verified after both edits
    landed (see "Verification performed" below).
  - Added the two new includes (`"../Core/EditorCapabilities.h"` and
    `"ActiveProjectAssemblyState.h"`), and updated the function
    DEFINITION's own signature to match the header exactly.
  - Did NOT add a second `ImGui::Separator();` — the existing one (right
    after "Open Project...") already sits immediately before the new
    Compile block.

## Deviations from the phase file — none load-bearing

**Zero substantive deviations.** Every file, every line-number citation, and
every edit instruction in `PHASE1_CAPABILITY_WIRING_AND_COMPILE_MENU_ITEM.md`
matched the real, current source tree exactly at the moment each edit was
made — re-confirmed by direct `read_file`/`read_line` immediately before each
edit, never assumed from the phase file's own line numbers alone (line
numbers shift after each edit; each subsequent edit was re-located by its own
exact text, exactly as the phase file itself warned to do for Edit B in
Section 3.11). The two specific hazards this phase file's own v2 revision
called out — (a) `EditorLayer.h` needing a brand-new
`IHotReloadDebugCapability` forward declaration that did NOT already exist,
and (b) `DockLayout.cpp`'s edit needing to be split into two non-adjacent
pieces so the real "Open Project..." menu item is never touched — were both
exactly as described, and both were handled exactly as instructed.

One tooling note, not a phase-file deviation: this campaign's own `edit_line`
tool call for `DockLayout.h`'s Edit worked correctly for the header text it
was told to touch, but the caller (this session) initially miscounted the
`length` parameter and left the OLD, un-extended
`BuildDockspaceAndMenuBar(EditorContext& ctx, Game& game, Renderer&
renderer);` declaration duplicated a few lines below the new one — caught
immediately by re-reading the file after the edit (a duplicate signature for
the same free function would have failed to compile as a redeclaration
mismatch), and fixed with one more `edit_line` call deleting the stray
duplicate line before ever attempting the scoped compile check. This was an
implementation-side self-correction, not a tool malfunction — `edit_line`
did exactly what it was told; the line count passed to it was simply
under-estimated by the caller.

## Verification performed (scoped, per this phase's own rules)

- **Scoped compile check**: `cmake --build build --target
  GreatTamanaEditor` — clean incremental build, **zero errors, zero new
  warnings**. All 28 build steps succeeded, ending in a real
  `GreatTamanaEditor.exe` link.
- Re-`search_in_dir`'d `: public IEditorLayer` across `src/` before AND after
  writing any code — confirmed exactly two implementers
  (`ImGuiEditorLayer`, `NullEditorLayer`), both edited.
- Re-`search_in_dir`'d `BuildDockspaceAndMenuBar` across `src/` — confirmed
  exactly one real call site (`ImGuiEditorLayer.cpp` line 529, now passing
  the new 4th argument); every other match is a comment/mention.
- Re-`search_in_dir`'d `IHotReloadDebugCapability` inside `EditorLayer.h`
  after Edit 1 — 3 hits (the new forward declaration, the doc-comment
  mention, and the new pure virtual), confirming Edit 1 was not skipped.
- Re-read the "Open Project..." block in `DockLayout.cpp` after both Edit A
  and Edit B landed — its own comment, the real
  `if (ImGui::MenuItem("Open Project...")) { ctx.openProjectWindowOpen =
  true; }` body, and the pre-existing `ImGui::Separator();` are all present,
  byte-for-byte unchanged, immediately before the new Compile block —
  confirming Edit A/B were never accidentally merged into one over-broad
  deletion.
- Did **not** run a full clean build, the full `ctest` regression suite, or
  any live/visual/HTTP-driven verification — all explicitly reserved for
  PHASE2, per this campaign's own workflow rules and the task's own explicit
  instructions.

## New gaps found during this phase, for PHASE2 to know about

- None beyond what `PHASE0_MASTER_STRATEGY.md`/`PHASE1_...md` already
  disclosed (the missing `IHotReloadDebugCapability` forward declaration in
  `EditorLayer.h`, and the non-contiguous `DockLayout.cpp` edit hazard) —
  both were confirmed accurate during implementation, neither was found to
  be wrong or incomplete.
- Nothing yet exercises `IsCompileInFlight()`/the new "Compile" menu item
  through an automated test or a live, running Editor — that is entirely
  PHASE2's own job (new Tier-1 test file for
  `IsProjectAssemblyBuildInFlight()`, plus live HTTP/screenshot
  verification of the real menu item's enabled/disabled/label states).

## Files changed this phase

- `src/Core/Plugins/ProjectAssemblyBuildRunner.h` (modified)
- `src/Core/Plugins/ProjectAssemblyBuildRunner.cpp` (modified)
- `src/Core/EditorCapabilities.h` (modified)
- `src/Editor/EditorHotReloadDebugCapability.h` (modified)
- `src/Editor/EditorHotReloadDebugCapability.cpp` (modified)
- `src/Editor/EditorLayer.h` (modified)
- `src/Editor/NullEditorLayer.cpp` (modified)
- `src/Editor/ImGuiEditorLayer.cpp` (modified)
- `src/Editor/EditorHost.cpp` (modified)
- `src/Editor/DockLayout.h` (modified)
- `src/Editor/DockLayout.cpp` (modified)
- `task_manager/editor-core-separation-19/PHASE1_COMPLETION_REPORT.md` (new,
  this file)

This closes PHASE1 of the `editor-core-separation-19` campaign. Next up:
`PHASE2_TESTS_LIVE_VERIFICATION_AND_CAMPAIGN_CLOSEOUT.md` (new Tier-1 test
file, full live HTTP/screenshot verification, full clean build, full
`ctest` regression, `CAMPAIGN_COMPLETION_REPORT.md`).
