# PHASE4 COMPLETION REPORT — HTTP Routes + OpenProjectWindow UI + Menu Wiring
## editor-core-separation-17 (On-Engine Project Workflow, BIG-STEP 3 "Open Project")

Parent: `PHASE0_MASTER_STRATEGY.md`. Spec: `PHASE4_HTTP_ROUTES_AND_OPEN_PROJECT_WINDOW_UI.md`.
Continuation context confirmed before starting: `PHASE1_COMPLETION_REPORT.md`,
`PHASE2_COMPLETION_REPORT.md`, and `PHASE3_COMPLETION_REPORT.md` all read in
full. Before writing any Phase 4 code, `IProjectLifecycleCapability` (`src/Core/
EditorCapabilities.h` lines 263-332) was directly re-read and confirmed to
genuinely contain `OpenProjectOutcome`, `ProjectListEntry`,
`OpenProjectAssembly()`, `OpenProjectAssemblyOnMainThread()`, and
`ListProjectAssemblies()`, exactly as Phase 3 reports. Every other file this
phase touches or mirrors (`NetworkServer.cpp`'s real
`/project_assembly/create_project` route, `NewProjectWindow.h/.cpp`,
`EditorContext.h`, `ImGuiEditorLayer.cpp`, `DockLayout.cpp`, root
`CMakeLists.txt`) was re-read fresh, in full, immediately before editing to
confirm exact current line numbers/surrounding code — all matched the spec's
own description with zero drift.

---

## What was built

1. **`src/Network/NetworkServer.cpp`** — added two new routes immediately
   after the existing `POST /project_assembly/create_project` route (which
   ends at the real, current line 1361), copying its 503-on-null-capability
   structure verbatim, business logic swapped for the new capability calls:
   - `POST /project_assembly/open_project` — reads `name` from the query
     string (deliberately bypassing `ParseProjectNameQuery()`, same reasoning
     as `create_project`), 503 if `projectLifecycleCapability == nullptr`,
     calls `OpenProjectAssembly(name)` (the network-thread-safe one), 400 +
     `errorMessage` JSON on `!outcome.success`, else a JSON body with
     `status_message`/`load_attempted`/`load_succeeded`.
   - `GET /project_assembly/list_projects` — no query parameter at all, 503
     if the capability pointer is null, else calls `ListProjectAssemblies()`
     and returns a JSON array of `{"name": ..., "tier": ...}` objects.
   - No `NetworkServer.h`/constructor signature change was needed — confirmed
     by re-reading the constructor (lines 1439-1474): the existing 9th
     parameter (`projectLifecycleCapability`) is reused unchanged, exactly as
     the spec predicted.

2. **`src/Editor/OpenProjectWindow.h`** and **`.cpp`** — new files, copied
   essentially verbatim from the spec's own 3.2/3.3 code blocks (the spec
   itself already contains the final, intended text). Confirmed the
   **non-negotiable rule**: `Build()`'s "Open" button handler calls
   `capability->OpenProjectAssemblyOnMainThread(...)` — grepped this file
   after writing it and confirmed the literal string `OpenProjectAssembly(`
   (the network-thread-only one, WITHOUT the `OnMainThread` suffix) does not
   appear anywhere in this file at all; only the `...OnMainThread()` variant
   is called. `Open()` (rescan) calls `ListProjectAssemblies()` directly,
   in-process, matching the spec's own doc comment.

3. **Root `CMakeLists.txt`** — registered `src/Editor/OpenProjectWindow.h` and
   `.cpp` in the `gte_editor` target's source list, immediately after the
   existing `NewProjectWindow.h`/`.cpp` entries (same list, same relative-path
   style, own short comment block matching the surrounding convention).

4. **`src/Editor/EditorContext.h`** — added
   `bool openProjectWindowOpen = false;` immediately after the existing
   `newProjectWindowOpen` field, with a doc comment mirroring
   `newProjectWindowOpen`'s own convention.

5. **`src/Editor/ImGuiEditorLayer.cpp`** —
   - Added `#include "OpenProjectWindow.h"` immediately after the existing
     `#include "NewProjectWindow.h"`.
   - Added `OpenProjectWindow m_openProjectWindow;` member immediately after
     the existing `NewProjectWindow m_newProjectWindow;` member.
   - Added `m_openProjectWindow.Build(m_ctx, m_projectLifecycleCapability);`
     immediately after the existing
     `m_newProjectWindow.Build(m_ctx, m_projectLifecycleCapability);` call.

6. **`src/Editor/DockLayout.cpp`** — replaced the disabled placeholder
   `if (ImGui::MenuItem("Open Project...", nullptr, false, false)) {}` with
   the real handler:
   ```cpp
   if (ImGui::MenuItem("Open Project...")) {
       ctx.openProjectWindowOpen = true;
   }
   ```
   and updated the surrounding comment block so it now only describes
   `"Compile"` (BIG-STEP 5) as the remaining reserved/disabled placeholder —
   "Open Project" was dropped from that list, exactly as the spec's 3.6
   instructs.

---

## Build verification actually performed

- **Fast incremental build**, `cmake --build build --target GreatTamanaEditor`
  (working directory `C:\Users\F5954\Documents\TAMANA\GreatTamanaEngine`) —
  re-ran CMake's own configure step (new source file added to
  `CMakeLists.txt`), then compiled `OpenProjectWindow.cpp`,
  `NetworkServer.cpp`, `DockLayout.cpp`, and `ImGuiEditorLayer.cpp`, linked
  `libgte_core.a`/`libgte_editor.a`, and produced `GreatTamanaEditor.exe`
  cleanly. **No errors, no warnings** from any of this phase's new/changed
  code.
- No full clean rebuild was performed. No `ctest` run was performed (per this
  phase's own explicit build/test discipline — Phase 5's job only).

---

## Live smoke test actually performed, with real results

1. **`run_app_background`** launched
   `C:\Users\F5954\Documents\TAMANA\GreatTamanaEngine\build\GreatTamanaEditor.exe`
   (PID 1944, working directory `build\`).
2. **`gte_send_request GET /project_assembly/list_projects`** — real result:
   ```
   HTTP 200 — Content-Type: application/json
   [{"name":"ProjectAssemblyProbe","tier":"AlreadyLoaded"}]
   ```
   This is a genuine, live JSON array containing `"ProjectAssemblyProbe"`,
   exactly matching this phase's own Definition of Done requirement. (The
   `"AlreadyLoaded"` tier — rather than `"Compiled"` — is expected and
   consistent with Phase 3's own documented finding: `Core::LoadProjectAssemblies()`
   unconditionally loads every already-built `.dll` at startup, so any
   already-compiled project is always `AlreadyLoaded` by the time the HTTP
   server is reachable.)
3. **Screenshot** — figured out which capture endpoint renders the Editor's
   own ImGui UI (not the game viewport) by trying `GET /get_swapchain`
   first: it returned a real PNG showing the actual docked ImGui layout
   (Hierarchy/Scene/Inspector panels, menu bar reading "File Window
   Project", bottom tab strip, etc.) — confirmed this is the correct
   endpoint for a UI screenshot in this session (`/get_game_view` would only
   show the 3D viewport content, not the menu bar). The screenshot shows the
   menu bar with a **"Project"** menu present, in its normal, default
   (closed/collapsed) state.
4. **`gte_send_request GET /get_logs`** — 36 log entries, all plugin-load/
   project-assembly-load informational messages from normal startup; **zero
   errors, zero warnings from this phase's own code**.
5. **`stop_app_background(pid: 1944)`** — called at the end, confirmed
   successful.

### The one honest, disclosed gap: could not screenshot the EXPANDED "Project" menu dropdown itself

The spec's own Definition of Done (3.7) asks for a screenshot confirming the
"Project" menu shows "Open Project..." **enabled (not grayed out)**. Doing
that mechanically requires the dropdown to actually be *open* (expanded) at
the moment of the screenshot — ImGui menus only render their child items
while the mouse has clicked/opened them.

I checked `NetworkServer.cpp` for every registered route (`grep`'d every
`server.Get(`/`server.Post(` call site) looking for any existing mouse/input-
simulation endpoint. None exists. The closest thing, `GET /activate_tab`
(`EditorUiCommandBridge`), only activates an already-*docked* panel tab by
name — it has no concept of a top menu-bar dropdown at all, and would not
help here even if repurposed. There is genuinely no HTTP-reachable way in
this codebase, today, to programmatically click open an ImGui menu bar
dropdown and then screenshot it mid-open.

This is the exact same class of gap the task's own instructions anticipated
and pre-authorized as an accepted, disclosed limitation (mirroring
`editor-core-separation-16`'s own campaign report gap around opening a
floating window over HTTP) — **not a blocker**. As the substitute
verification the task itself specifies for this exact situation, I did a
careful, line-by-line code review instead:

- Re-read `DockLayout.cpp`'s real, current, POST-edit content (lines 175-198)
  directly: the `"Open Project..."` `ImGui::MenuItem` call now reads
  `ImGui::MenuItem("Open Project...")` — the 2-argument form, with **no**
  `false` disabled-flag argument anywhere in the call — while the still-
  reserved `"Compile"` item immediately below it still reads
  `ImGui::MenuItem("Compile", nullptr, false, false)`, i.e. still genuinely
  disabled. Since `ImGui::MenuItem`'s 4th parameter (`enabled`) defaults to
  `true` when omitted, this call is now mechanically, unambiguously enabled
  — this is a plain fact about the call's own arguments, verifiable by
  reading the source, independent of any screenshot.
- Confirmed the handler body genuinely sets `ctx.openProjectWindowOpen = true;`
  on click, which `ImGuiEditorLayer.cpp`'s new
  `m_openProjectWindow.Build(m_ctx, m_projectLifecycleCapability);` call
  consumes every frame (this call was itself confirmed present via a clean
  compile of `ImGuiEditorLayer.cpp` — a stale/missing member or call would
  not have compiled).

I judge this combination — (a) the live JSON smoke test on the new HTTP
routes, (b) a real screenshot proving the Editor boots with an intact,
functioning ImGui menu bar and did not crash/regress from this phase's
changes, and (c) the direct, mechanical source read proving the `MenuItem`
call's own enabled-by-default argument shape — to be honest, sufficient
evidence for this phase's real, working state, short of an expanded-dropdown
screenshot that this session's tooling cannot produce.

---

## Deviations from `PHASE4_HTTP_ROUTES_AND_OPEN_PROJECT_WINDOW_UI.md`

**None in code shape.** Every route body, `OpenProjectWindow.h/.cpp`'s full
content, the `EditorContext.h` field, the `ImGuiEditorLayer.cpp` include/
member/`Build()` call, and the `DockLayout.cpp` menu-item replacement all
match the spec's own 3.1-3.6 text verbatim. The only non-functional
additions:
- Slightly reworded the `DockLayout.cpp` comment block above the menu items
  (the spec's 3.6 asked to "drop 'Open Project' from the reserved list" —
  done — the exact wording chosen differs cosmetically from a literal
  guess at the spec author's own phrasing, but the substance — only
  "Compile" remains reserved/disabled — is identical).
- The one real, disclosed gap described above: could not produce a
  screenshot of the *expanded* "Project" menu dropdown, because no
  HTTP-reachable input-simulation endpoint exists in this codebase today.
  This mirrors the exact class of gap the task's own instructions
  pre-authorized as acceptable, and was substituted with a direct,
  mechanical source-code read confirming the `MenuItem` call's own
  enabled-by-default argument shape instead.

---

## Definition-of-done checklist (mirrors PHASE4's own 3.7)

- [x] Both new HTTP routes compile and are registered.
- [x] `OpenProjectWindow.h/.cpp` compile, are wired into
      `ImGuiEditorLayer.cpp`, and the "Project > Open Project..." menu item
      is enabled (confirmed by direct source read: the `MenuItem` call has
      no disabling 4th argument) and sets `ctx.openProjectWindowOpen = true`.
- [x] Fast incremental compile check of `GreatTamanaEditor` succeeds (no
      full clean build performed).
- [x] Live smoke check performed: `GET /project_assembly/list_projects`
      returned `[{"name":"ProjectAssemblyProbe","tier":"AlreadyLoaded"}]` — a
      real JSON array containing `"ProjectAssemblyProbe"`. A real screenshot
      of the running Editor's own ImGui UI (via `/get_swapchain`, confirmed
      to be the UI-rendering endpoint, not the game-viewport one) was taken,
      showing the intact "Project" menu in the menu bar. Opening the
      dropdown itself to screenshot "Open Project..." specifically as
      enabled could not be done mechanically (no input-simulation HTTP
      route exists) — disclosed as an accepted gap above, substituted with a
      direct source-code review.
- [x] `git_add` + `git_commit` + this `PHASE4_COMPLETION_REPORT.md`.
- [x] `stop_app_background` called on the launched instance (PID 1944).

---

## Non-goals confirmed untouched (per spec's 3.8)

- No full regression suite run (`ctest`) — Phase 5's job.
- BIG-STEP 5's "Compile" menu item — still the disabled placeholder,
  untouched.

## Handoff to PHASE5

`POST /project_assembly/open_project`, `GET /project_assembly/list_projects`,
and the real, wired, menu-reachable `OpenProjectWindow` are now live, compiled
code. `GreatTamanaEditor.exe` boots cleanly with these changes, the new list
route already returns real data reflecting the actual on-disk
`Projects/ProjectAssemblyProbe` state. Phase 5 can now write its own
end-to-end test file, run the full clean build + full `ctest` regression
suite, and perform its own live HTTP-driven verification of every tier plus
the already-loaded no-double-load guarantee. One open item for Phase 5 (or a
later campaign) to be aware of: there is still no HTTP-reachable way to
simulate a mouse click into an ImGui menu bar or a specific window's buttons
— every UI verification this whole 17-campaign series can do over HTTP is
limited to (a) reading rendered pixels via `/get_swapchain`/`/get_game_view`
and (b) a narrow, pre-existing `/activate_tab` for already-docked panels.
Actually clicking "Open" inside `OpenProjectWindow` itself, end-to-end, over
HTTP, is not currently possible with this codebase's own tooling.
