# editor-core-separation-18 — PHASE2 COMPLETION REPORT
## On-Engine Project Workflow — BIG-STEP 4 of 5: "Create New Script/Shader Asset"

Status: **DONE**. Scoped compile check green; live, HTTP-driven, screenshot
verification performed against a real running `GreatTamanaEditor.exe`. This
is a single-phase implementation report (not a full campaign closeout — that
is PHASE4's own job).

## What was actually built

Exactly what `PHASE2_PROJECT_PANEL_SYNTHETIC_ROW_AND_CREATE_MENU.md` asked
for, no HTTP route/`CreateAssetWindow` touched (that's PHASE3's job):

- **`src/Editor/EditorContext.h`** — added the forward declaration
  `enum class AssetScaffoldKind;` (right after `namespace gte {`, before
  `kProjectAssetDragDropPayloadType`), plus `createAssetWindowOpen` (bool,
  default `false`) and `createAssetWindowPendingKind`
  (`static_cast<AssetScaffoldKind>(0)`, i.e. `RenderPass`), placed
  immediately after `openProjectWindowOpen` — the exact family/location the
  phase file specified.
- **`src/Editor/Panels/ProjectPanel.h`** — two new private methods
  (`RenderActiveProjectAssetsRow(EditorContext&)`,
  `RescanActiveProjectAssetsIfNeeded()`) and three new private members
  (`m_activeProjectAssetFileNames`, `m_activeProjectNameLastScanned`,
  `m_suppressPaneContextMenuThisFrame`), all exactly as specified.
- **`src/Editor/Panels/ProjectPanel.cpp`**:
  - New includes: `../ActiveProjectAssemblyState.h` and
    `../../Core/EditorCapabilities.h` (only this `.cpp` needs the real
    `AssetScaffoldKind` definition — `<algorithm>` was already included, so
    no new include was needed for `std::sort`).
  - `RescanActiveProjectAssetsIfNeeded()` — reads
    `ActiveProjectAssemblyState::Instance().GetActive()`, clears/rebuilds
    `m_activeProjectAssetFileNames` from a one-level, non-recursive
    `std::filesystem::directory_iterator` over `active.assetsDirectory`,
    sorted, and updates `m_activeProjectNameLastScanned`. Called from
    `EnsureRootAndMaybeRescan()` on the exact same 500ms throttle as
    `m_tree`/`m_assetDatabase`, added right after
    `ReconcileCurrentFolderAfterRescan()`.
  - `RenderActiveProjectAssetsRow(EditorContext&)` — the synthetic
    `"[Active Project] <Name>"` `TreeNodeEx`, its own
    `ImGui::BeginPopupContextItem("ProjectAssemblySourceContextMenu")` with a
    "Create" submenu (Render Pass.../Compute Shader.../Vertex/Fragment Shader
    Pair...) that writes `ctx.createAssetWindowPendingKind` +
    `ctx.createAssetWindowOpen = true`, and a plain `BulletText` loop over
    `m_activeProjectAssetFileNames` (display-only, LDD-CA3 — never
    `Selectable`/drag-drop-wired). Returns immediately (no row at all) when
    `m_activeProjectNameLastScanned` is empty.
  - `RenderLeftPane()` — now calls `RenderActiveProjectAssetsRow(ctx)` as the
    very first line, before the pre-existing `"Project"` root `TreeNodeEx`.
  - `RenderContextMenu()` — now returns immediately, before touching
    `ImGui::BeginPopupContextWindow(popupId)` at all, whenever
    `m_suppressPaneContextMenuThisFrame` is true.
  - `Build()` — resets `m_suppressPaneContextMenuThisFrame = false` as the
    very first statement, right after `EnsureRootAndMaybeRescan()`, before
    either pane renders.

## Deviations from the phase file — one genuine, positive one; nothing else

- **The phase file's own STEP 4, item 5 was slightly pessimistic — a real,
  already-existing HTTP route made a FULLER live verification possible than
  it anticipated.** The phase file said "Right-click that row live is not
  screenshot-drivable over HTTP... substitute a direct source-code read" for
  confirming the "[Active Project] `<Name>`" row itself renders correctly.
  That framing conflated two different things: (1) actually *right-clicking*
  the row (genuinely not HTTP-drivable — no click-simulation route exists
  anywhere in this codebase, confirmed by grepping every `server.Get(`/
  `server.Post(` call site) vs. (2) merely *seeing* the row on screen at all,
  which turned out to be fully screenshot-drivable once the right ImGui tab
  was in the foreground. The obstacle was never "no HTTP route can drive
  this" — it was that the "Project" panel is docked as one tab among many
  (`Memory`/`Profiler`/.../`Project`/`Demo Plugin Panel`/`Probe Panel`) and
  whichever tab was last focused in a PRIOR session (persisted in
  `build/imgui.ini`) is what a fresh launch shows first. A completely
  unrelated, already-shipped campaign (`network-impl-7`, "IMGUI Tab
  Activation Engine") had already built exactly the missing piece:
  `GET /activate_tab?name=<PanelName>` (`IEditorLayer::ActivateTab()`,
  `NetworkServer.cpp`). Calling `GET /activate_tab?name=Project` brought the
  "Project" tab to the foreground on demand, and both required screenshots
  (STEP 4 items 2 and 4) were then captured as REAL, on-screen, HTTP-driven
  proof — not substituted with a source-code read. This is strictly MORE
  verification than the phase file asked for, not less; it is recorded here
  because it corrects the phase file's own assumption for whoever reads this
  next (PHASE3/PHASE4 will hit the exact same "which tab is focused"
  question for `CreateAssetWindow`, though that one is a floating,
  non-docked window, so it may not even need this trick at all).
  - The right-click-to-open-the-Create-submenu step itself (genuinely not
    HTTP-drivable) was still verified the way the phase file specified: by
    direct source-code inspection confirming (a) the three `MenuItem` calls
    compile and are reachable (already proven by the successful scoped
    build) and (b) `m_suppressPaneContextMenuThisFrame`'s guard in
    `RenderContextMenu()` is a plain early-`return` gated on a bool that is
    reset to `false` at the very top of every `Build()` call and only ever
    set `true` from inside `RenderActiveProjectAssetsRow()`'s own popup
    block — so the pre-existing "New Folder"/"Delete Selected" menu logic in
    `RenderContextMenu()` is completely untouched code, and can only ever be
    suppressed on a frame where the NEW popup is actually open, never
    unconditionally.
  - No other deviations. Every field name, member location, and code shape
    in `EditorContext.h`/`ProjectPanel.h`/`ProjectPanel.cpp` matches the
    phase file's own STEP 3 code listing exactly.

## Verification performed (scoped, per this phase's own rules)

- **Scoped compile check**: `cmake --build build --target GreatTamanaEditor`
  — confirmed the real target name via the root `CMakeLists.txt`
  (`add_executable(GreatTamanaEditor src/main.cpp)`) first. Clean
  incremental build, zero errors, zero new warnings.
- **`ProjectPanelDataTests.cpp` regression check** (a DIFFERENT file from
  `ProjectPanel.cpp`, testing only `ProjectPanelData.h`'s free functions,
  untouched by this phase): `cmake --build build --target
  GreatTamanaEngineTests` linked cleanly, then `ctest -C Debug
  --output-on-failure -R ProjectPanelData` — **32/32 tests passed**, zero
  regressions.
- **Live, HTTP-driven, screenshot-based verification** (STEP 4 of the phase
  file), against a real running `GreatTamanaEditor.exe`
  (`run_app_background`/`gte_send_request`/`stop_app_background`):
  1. Launched the Editor. Used the pre-existing `GET
     /activate_tab?name=Project` route (see "Deviations" above) to bring the
     "Project" tab to the foreground, then `GET /get_swapchain` — confirmed,
     by real screenshot, that with NO active project set the "Project" panel
     shows its ordinary tree with **no** "[Active Project]" row anywhere
     (Definition of Done item 1, `PHASE0_MASTER_STRATEGY.md`).
  2. `POST /project_assembly/create_project?name=Phase2ScratchTest` (the
     existing BIG-STEP 2 route, unmodified by this phase) to get a real
     active project.
  3. `GET /get_swapchain` again — confirmed, by real screenshot, the new
     `"[Active Project] Phase2ScratchTest"` row now renders, showing
     `Phase2ScratchTestGame.cpp` as its one bulleted child (the file BIG-STEP
     2's own scaffold already writes into `Assets/`), exactly as the phase
     file's STEP 4 item 4 describes.
  4. Confirmed the right-click Create submenu itself via direct source read
     (see "Deviations" above) rather than a live click, per the phase file's
     own pre-authorized gap for right-click interactions.
  5. Cleaned up: stopped the Editor process, `remove_all`'d
     `Projects/Phase2ScratchTest/` (confirmed gone via `browse_dir`
     afterward — only the pre-existing `Projects/ProjectAssemblyProbe/`
     remains), and deleted the two small throwaway `hash_probe.cpp`/`.exe`
     scratch files used while investigating the tab-focus question before
     `/activate_tab` was found (see below).
- **`git_status`**: only this phase's own three real, intended files
  modified (`src/Editor/EditorContext.h`,
  `src/Editor/Panels/ProjectPanel.h`, `src/Editor/Panels/ProjectPanel.cpp`)
  pending, plus this report — no scratch project folder, no debug probe
  files, nothing left in the working tree.

## A worthwhile investigation dead-end, disclosed for completeness

Before finding the pre-existing `/activate_tab` route, this phase's own
verification work briefly went down a manual path: computing Dear ImGui's
internal per-tab `ImGuiID` (`ImHashStr("#TAB", 0, ImHashStr(windowName))`)
via a small throwaway program linked against the already-built
`libimgui.a`, in order to hand-edit `build/imgui.ini`'s `Selected=` field for
the bottom dock node. This DID work mechanically (confirmed: editing
`Selected=0x9C21DE82` — the real computed `TabId` for `"Project"` — did bring
that tab to the foreground on the next launch), but was abandoned in favor of
`/activate_tab` the moment it was found, since that route is the correct,
supported, already-shipped way to do this and needs no `imgui.ini` surgery at
all. Mentioned here only so a future phase doesn't have to rediscover
`/activate_tab` exists or waste time on the same manual-hash detour.

## New gaps found during this phase, for PHASE3/PHASE4 to know about

- **None structural.** Every finding/correction `PHASE0`/PHASE2's own file
  already documented (the `BeginPopupContextItem` vs.
  `BeginPopupContextWindow` mutual-exclusion hazard, the dead
  `else if (ImGui::IsPopupOpen(...))` branch correctly never written, LDD-CA3's
  display-only children) was confirmed accurate during implementation — none
  were found to be wrong or incomplete.
- **Worth flagging forward (not a gap, an opportunity)**: `GET
  /activate_tab?name=<PanelName>` (network-impl-7 campaign) fully solves the
  "which tab is focused" problem for any DOCKED panel's live screenshot
  verification. PHASE3's own `CreateAssetWindow` is a floating, on-demand
  window (like `NewProjectWindow`/`OpenProjectWindow` before it), so it is
  NOT a docked tab and this route does not apply to it directly — PHASE3/
  PHASE4 will likely still hit the same "no HTTP route opens a floating
  window" gap `editor-core-separation-16`/`-17` each already disclosed,
  unless a dedicated route mirroring the Frame Debugger's own `GET
  /frame_debugger/open` precedent is ever built generically for this family
  of windows (as `-17`'s own completion report already suggested).
- Nothing in this phase calls `CreateAssetScaffold()` yet (by design — PHASE2
  only ever sets `EditorContext` fields; PHASE3's `CreateAssetWindow` is what
  actually calls the capability) — `ctx.createAssetWindowOpen`/
  `ctx.createAssetWindowPendingKind` are written correctly by the new Create
  submenu but have no reader anywhere in the codebase until PHASE3 exists.
  This was expected and is not a gap in this phase's own work.

## Files changed this phase

- `src/Editor/EditorContext.h` (modified)
- `src/Editor/Panels/ProjectPanel.h` (modified)
- `src/Editor/Panels/ProjectPanel.cpp` (modified)
- `task_manager/editor-core-separation-18/PHASE2_COMPLETION_REPORT.md` (new,
  this file)

This closes PHASE2 of the `editor-core-separation-18` campaign. Next up:
`PHASE3_HTTP_ROUTE_AND_CREATE_ASSET_WINDOW_UI.md`.
