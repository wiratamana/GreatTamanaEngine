# PHASE0 — MASTER STRATEGY: Editor "Frame Debugger" window — GUI SCAFFOLDING ONLY

Campaign folder: `task_manager/frame-debugger-2/`
Branch: `feature/frame-debugger-impl`
Depends on: `task_manager/frame-debugger-1/` (already landed — gave the engine `gte::Time`/`gte::EngineContext` and the Pause/Resume/Step toolbar this campaign's "Enable" checkbox will hook into).

This is the **orchestrator** document. It does not itself contain
implementation instructions — each child phase (`PHASE1`..`PHASE7`) is a
self-contained, independently implementable, independently compilable
chunk. Read this file first, then work the phases **in numeric order**
(each one assumes every previous one already landed).

## Phase list

| Phase | File | One-line summary |
|---|---|---|
| 1 | `PHASE1_FRAME_DEBUGGER_DATA_MODEL.md` | New, ImGui-free, Tier-1-testable `src/Editor/FrameDebuggerData.h/.cpp` — every struct the panel will ever display, plus a deliberately-empty `BuildPlaceholderFrameDebuggerSnapshot()` and pure stepper-label/clamp helpers. Zero ImGui, zero call sites into the rest of the Editor yet. |
| 2 | `PHASE2_PANEL_SHELL_AND_WINDOW_MENU_ENTRY.md` | The window itself comes into existence: `src/Editor/Panels/FrameDebuggerPanel.h/.cpp` (a small stateful class, opened/closed via a brand-new "Window" top-level menu in `DockLayout.cpp`), wired into `ImGuiEditorLayer`. Shows only a "Coming soon" placeholder body — no toolbar/tree/inspector yet. |
| 3 | `PHASE3_TOOLBAR_AND_FRAME_STEPPER.md` | The "Enable" checkbox (wired to auto-engage the existing Pause/Resume toolbar — see Locked Design Decision #3), the cosmetic disabled "Editor" combo, and the disabled frame-stepper row ("0 of 0"), built on top of PHASE1's pure helpers. |
| 4 | `PHASE4_EVENT_TREE_PANE_AND_SPLITTER.md` | The left-hand draggable-splitter event tree pane (copies `ProjectPanel.cpp`'s splitter pattern) — shows "No frame captured yet." (per the user's own explicit decision — no fake rows), plus a fully-written (currently unreachable) recursive tree-row renderer ready for real data. |
| 5 | `PHASE5_INSPECTOR_PANE_FRAME_LEVEL_CHROME.md` | The right-hand pane's frame-level (not event-level) static chrome: RenderTarget selector, Channels toggle row, Levels slider, texture-preview placeholder box — always visible once Enabled, independent of any event selection. |
| 6 | `PHASE6_EVENT_DETAILS_SECTION_AND_PROPERTY_FORMATTING.md` | The event-level bottom section: Shader/Pass/Blend/Z-state/Stencil property rows, the Preview/ShaderProperties tab bar, and Textures/Vectors/Matrices subsections — fully implemented against a `FrameDebuggerEventDetails`, falling back to "No event selected." (always true this campaign). **Flagged as the heaviest single phase — see "Note on review depth" below.** |
| 7 | `PHASE7_INTEGRATION_BUILD_WIRING_AND_DOCS.md` | `CMakeLists.txt`/`tests/CMakeLists.txt` final confirmation, `EditorPanelCatalog.h` non-inclusion (documented, precedented), `AGENTS.md`/`docs/`/`TODO.md`/`README.md` updates, full clean build (both Editor configs) + full `ctest` regression + live runtime smoke test, completion report. |

---

## Step 1: The Goal (Where are we going?)

Give the GreatTamanaEngine Editor a new **"Frame Debugger"** window,
visually modeled on the user-supplied reference screenshot (Unity's own
Frame Debugger): a closeable, on-demand floating window — opened from a
brand-new **Window** menu — containing:

- A toolbar row: an **"Enable"** checkbox, and a cosmetic (disabled)
  **"Editor"** mode combo.
- A frame-stepper row: a numeric "N of M" readout (disabled — there is
  never more than one "frame" worth of nothing to step through yet).
- A left-hand, draggable-splitter **event tree** pane (hierarchical
  draw-call/pass list, Unity-style) — showing a plain **"No frame
  captured yet."** message (never fake/mock rows — see Step 3.3, Locked
  Design Decision #2).
- A right-hand **inspector** pane: a RenderTarget selector, Channels
  (All/R/G/B/A) toggle row, a Levels slider, a texture-preview
  placeholder box, and (below that) an event-details section — Shader/
  Pass/Blend/Z-state/Stencil rows, a Preview/ShaderProperties tab bar,
  and Textures/Vectors/Matrices subsections — all fully built against a
  real data struct, but showing **"No event selected."** in practice this
  campaign, since nothing can ever be selected yet.

**This campaign is explicitly, deliberately GUI-ONLY.** No real frame
capture, no real render-graph/draw-call introspection, no real shader
reflection is implemented here — see "Non-Goals" below. Every value shown
is either a disabled control, a placeholder message, or an inert default.
The entire point of this campaign is to build the **scaffolding** — the
window, its widgets, and (critically) the exact seams a *future* campaign
will plug real data into — so that a later "wire up the real frame
capture" campaign is a pure data-plumbing exercise that never has to
touch a single ImGui call in this window again.

## Step 2: The Situation (Where are we now?)

Investigated directly in the current `feature/frame-debugger-impl`
checkout:

- **`task_manager/frame-debugger-1/` already landed** (see its own
  `CAMPAIGN_COMPLETION_REPORT.md`): the engine has a real `gte::Time`/
  `gte::EngineContext`, and the Editor already has a Pause/Resume + Step
  toolbar (`src/Editor/PlaybackControls.h/.cpp`,
  `EditorContext::playbackPaused`/`stepOneFrameRequested`). That
  campaign's own stated non-goal list explicitly named "the actual
  frame-debugger feature itself" as future work — this campaign is that
  future work's **first half** (GUI only; the *second* half — real
  capture — is explicitly still out of scope, see Non-Goals).
- **`src/Editor/` structure** (see `AGENTS.md`, "Editor Module
  Structure"): everything except `EditorLayer.h`/`NullEditorLayer.cpp`
  (and the documented `EditorPanelCatalog.h` exception) compiles only
  under `GTE_ENABLE_EDITOR`. Panels are either (a) stateless free
  functions taking `EditorContext&` (`Panels/HierarchyPanel.cpp`, etc.)
  or (b) small stateful classes for panels with genuine cross-frame state
  (`ProfilerPanel`, `RenderGraphPanel`, `BoneViewerWindow`) — **never**
  through a shared `IEditorPanel` interface. The Frame Debugger panel
  needs its own state (Enabled toggle, selected event index, splitter
  width) — it is the fourth precedent for the stateful-class exception.
- **`BoneViewerWindow`** (`src/Editor/BoneViewerWindow.h/.cpp`) is the
  established precedent for an **on-demand floating window** (as opposed
  to a permanently-docked tab like Hierarchy/Inspector/Scene/Game/Memory/
  Profiler/Render Graph/Jobs/Atmosphere): `Build()` is a complete no-op
  unless a `bool` open-flag is true, and the window is opened by some
  *other* panel calling an `Open(...)` method on the instance directly
  (Inspector's "Open Bone Viewer" button, which is handed a reference to
  `m_boneViewer` through its own function parameters —
  `ImGuiEditorLayer.cpp`'s `BuildInspectorPanel(...)` call).
  **This campaign's own trigger is different**: a brand-new **"Window"**
  top-level menu item inside `DockLayout.cpp`'s
  `BuildDockspaceAndMenuBar(EditorContext& ctx, Game&, Renderer&)` — a
  free function with **no reference to a `FrameDebuggerPanel` instance at
  all**. The correct mechanism here is therefore a plain `bool` living
  in `EditorContext` itself (`frameDebuggerWindowOpen`), flipped directly
  by a checkable `ImGui::MenuItem(...)` call, and read by
  `FrameDebuggerPanel::Build(EditorContext&)` — **not** `BoneViewerWindow`'s
  own private-member-plus-`Open()`-method shape. `ImGui::Begin(name,
  &ctx.frameDebuggerWindowOpen)` is passed that exact same bool as its
  `p_open` parameter, so the window's own titlebar `[x]` close button
  keeps that one shared bool in sync automatically, in both directions,
  for free.
- **The confirmed reference precedent for "an on-demand window that is
  NOT part of the default dock layout is NOT added to
  `EditorPanelCatalog.h`"**: `BoneViewerWindow`'s window name never
  appears in `kKnownEditorPanelNames` (`src/Editor/EditorPanelCatalog.h`)
  — that catalog exists purely to keep `DockLayout.cpp`'s own
  *permanently-docked default layout* names in sync with the Network
  layer's `GET /activate_tab`/`GET /list_tabs` routes (see
  `docs/conventions/networking.md`). "Frame Debugger" follows the exact
  same "on-demand floating window, not in the catalog" precedent as
  `BoneViewerWindow` — **PHASE7 documents this explicitly so nobody later
  "fixes" this as an apparent omission.**
- **The exact draggable-splitter pattern to copy** already exists twice
  in this codebase: `src/Editor/Panels/ProjectPanel.cpp` (`kSplitterWidth
  = 6.0f`, a persisted `m_leftPaneWidth` float member, clamped every frame
  against the pane's current available width, and a thin
  `ImGui::Button("##ProjectPaneSplitter", ImVec2(kSplitterWidth,
  paneAreaHeight))` whose `IsItemActive()` drag adds `ImGui::GetIO().
  MouseDelta.x` to the width — see that file's lines ~381–410) and
  `src/Editor/Panels/InspectorPanel.cpp` (a *vertical* variant, same
  shape, `ctx.inspectorPreviewHeight`). PHASE4 copies the `ProjectPanel.cpp`
  (horizontal) variant verbatim, adapted to this panel's own naming.
- **The exact "Pause"-style stateful-panel-with-frozen-state precedent**
  already exists twice: `src/Editor/Panels/RenderGraphPanel.h/.cpp` and
  `src/Editor/Panels/ProfilerPanel.h/.cpp` — both a small class (not a
  free function), both explicitly pre-approved by `AGENTS.md`'s "Editor
  Module Structure" section for exactly this reason. `FrameDebuggerPanel`
  is the same shape, but (per the user's own answer, see Step 3.2) it
  does **not** need its own frozen-snapshot Pause mechanism this
  campaign — there is no live-changing data yet to freeze (YAGNI; a
  future campaign adds that once real, frame-to-frame-changing capture
  data exists).
- **`EditorContext.h`** (`src/Editor/EditorContext.h`) is the existing,
  plain, shared-state struct every panel/dock-layout function reads and
  writes — the natural, already-established place for the one new field
  this campaign needs at that level (`frameDebuggerWindowOpen`; see PHASE2).
- **`DockLayout.cpp`'s `BuildDockspaceAndMenuBar()`** currently has
  exactly one top-level menu, `"File"` (`Save Scene`/`Open Scene`/`Exit`)
  — see lines ~128–151. This campaign adds a **second** top-level menu,
  `"Window"`, immediately after it, inside the same
  `if (ImGui::BeginMenuBar())` block (PHASE2).
- **`ImGuiEditorLayer.cpp`** (`src/Editor/ImGuiEditorLayer.cpp`) is the
  Editor's composition root — `BuildUI()` calls every panel builder
  explicitly, by name, in a fixed order (see its own file comment). This
  campaign adds one new member (`FrameDebuggerPanel m_frameDebuggerPanel;`)
  and one new call (`m_frameDebuggerPanel.Build(m_ctx);`), mirroring
  `m_jobsPanel.Build(m_ctx, game);`'s own shape exactly (PHASE2).
- **`CMakeLists.txt`'s `target_sources(gte_core PRIVATE ...)`** (inside
  `if(GTE_ENABLE_EDITOR)`, lines ~566–624) explicitly lists every Editor
  source file — no globbing. **`tests/CMakeLists.txt`'s `GTE_TEST_SOURCES`**
  (inside its own `if(GTE_ENABLE_EDITOR)` block, lines ~1901–1911) does the
  same for Tier-1 Editor test files (`MemoryPanelDataTests.cpp`,
  `ProfilerPanelDataTests.cpp`, `JobsPanelDataTests.cpp`, ...) — this
  campaign's new `FrameDebuggerData.h/.cpp` and
  `tests/Editor/FrameDebuggerDataTests.cpp` follow that **exact same**
  precedent (gated behind `GTE_ENABLE_EDITOR`, **not** the unconditional
  list `EditorPanelCatalogTests.cpp` uses — this campaign's data model is
  never consumed by the always-compiled Network layer, unlike
  `EditorPanelCatalog.h`).

## Step 3: The Plan (detailed strategy)

### 3.1 Architecture at a glance

```
DockLayout.cpp (BuildDockspaceAndMenuBar)
  "Window" menu -> ImGui::MenuItem("Frame Debugger", nullptr,
                                    &ctx.frameDebuggerWindowOpen)
                                     |
                                     v
EditorContext::frameDebuggerWindowOpen (bool, shared)
                                     |
                                     v
ImGuiEditorLayer::BuildUI() -> m_frameDebuggerPanel.Build(m_ctx)
                                     |
                                     v
FrameDebuggerPanel::Build(EditorContext& ctx)
  if (!ctx.frameDebuggerWindowOpen) return;                 // PHASE2
  ImGui::Begin("Frame Debugger", &ctx.frameDebuggerWindowOpen);
    BuildToolbarRow(ctx)          // Enable checkbox + "Editor" stub combo   (PHASE3)
    BuildFrameStepperRow(snapshot)// "0 of 0", disabled                     (PHASE3)
    if (!m_enabled) { show "Enable..." message; }
    else {
        snapshot = BuildPlaceholderFrameDebuggerSnapshot();  // PHASE1, always empty
        [ event tree pane | splitter | inspector pane ]      // PHASE4 / PHASE5
          BuildEventTreePane(snapshot)     -> "No frame captured yet."  (PHASE4)
          BuildInspectorPane(ctx, snapshot)
            RenderTarget/Channels/Levels/Preview chrome      (PHASE5)
            BuildEventDetailsSection(details)
              details = FindEventDetailsByIndex(snapshot, m_selectedEventIndex)
              -> always std::nullopt this campaign -> "No event selected." (PHASE6)
    }
  ImGui::End();
```

`FrameDebuggerData.h/.cpp` (PHASE1) is the pure, ImGui-free data model —
structs plus `BuildPlaceholderFrameDebuggerSnapshot()`,
`FormatFrameStepperLabel()`, `ClampSelectedEventIndex()`,
`FindEventDetailsByIndex()` — every one of these is the **exact, single
seam** a future real-capture campaign needs to replace/extend. No other
file in this campaign needs to change again once that future campaign
lands, other than swapping which function produces the
`FrameDebuggerSnapshot` and populating `m_selectedEventIndex` from real
tree-row clicks (both already wired for real in PHASE4/PHASE6 — they are
just never reachable with non-default data yet, because nothing ever
produces a non-empty snapshot this campaign).

### 3.2 Locked Design Decisions (from the user's own answers — do not
relitigate these during implementation; if a phase document's plan
conflicts with one of these, the phase document is wrong and should be
fixed, not the other way around)

1. **Placement: an on-demand, closeable floating window, opened from a
   brand-new "Window" top-level menu item** (checkable, bound directly to
   `EditorContext::frameDebuggerWindowOpen`) — **not** part of the default
   docked layout (`DockLayout.cpp`'s `BuildDefaultDockLayout()`/
   `kKnownEditorPanelNames` are **not** touched by this campaign at all).
2. **Zero fake/mock rows anywhere.** The event tree pane always shows a
   plain **"No frame captured yet."** message (there is no capture logic
   yet — `BuildPlaceholderFrameDebuggerSnapshot()` always returns an
   empty `rootNodes` this campaign, by design, forever, until a *future*
   campaign replaces it with a real one). The event-details section always
   shows **"No event selected."** for the same reason. The
   frame-level chrome around it (RenderTarget selector, Channels row,
   Levels slider, texture-preview box — see PHASE5) is real, static,
   always-visible widget structure (not gated behind "has an event been
   selected") since those concepts belong to the *whole frame*, not to
   any one event — mirroring how Unity's own equivalent controls are
   frame-scoped, not per-event. This is a deliberate split; do not blur it
   by inventing fake per-event content to "fill up" the chrome.
3. **The "Enable" checkbox auto-engages the existing Pause/Resume
   toolbar.** Turning "Enable" ON sets `ctx.playbackPaused = true`
   directly (the exact same field `PlaybackControls.cpp`'s own "Pause"
   button already writes — see `frame-debugger-1`). Turning "Enable" OFF
   does **not** auto-resume (`ctx.playbackPaused` is left exactly as the
   user last set it via either control) — this asymmetry is intentional:
   a user inspecting a paused frame should not be silently un-paused just
   for unchecking a debug-window toggle. Document this reasoning
   explicitly in `PlaybackControls`-adjacent code comments in PHASE3 so a
   future reader doesn't "fix" it into a symmetric toggle.
4. **The "Editor" mode combo is a purely cosmetic, permanently-disabled
   stub** (`ImGui::BeginDisabled()` around a one-item combo showing only
   `"Editor"`) — this engine has no Play/Edit-mode split (see
   `frame-debugger-1`'s own Locked Design Decision #1), so there is
   nothing real for this control to switch between yet. It exists purely
   for visual parity with the reference screenshot.
5. **Seven phases** (`PHASE1`..`PHASE7`), each independently compilable,
   each landing a coherent, demonstrable increment — see the phase table
   above for the exact split, chosen for a finer-grained, lower-risk
   rollout per the user's own explicit request.
6. **No `EditorPanelCatalog.h` entry, no Network `/activate_tab` support**
   for "Frame Debugger" this campaign — it is an on-demand floating
   window, exactly like the precedent `BoneViewerWindow` already
   establishes (which also has no catalog entry). See PHASE7 for the
   explicit documentation of this non-omission.
7. **No frozen/Pause snapshot mechanism inside `FrameDebuggerPanel` itself**
   this campaign (unlike `RenderGraphPanel`/`ProfilerPanel`'s own "Pause"
   checkboxes) — there is no live, frame-to-frame-changing data yet to
   freeze a snapshot of. `BuildPlaceholderFrameDebuggerSnapshot()` is
   cheap and pure enough to call fresh every single `Build()` call, same
   as `RenderGraphPanel`'s own un-paused branch already does for
   `renderGraph.LastSnapshot(...)`.

### 3.3 Non-Goals (explicitly out of scope for this campaign)

- Any real frame/draw-call/render-pass capture logic whatsoever (no
  hooking into `gte::rg::RenderGraph`, `DrawStats`, `GpuTiming`, or any
  shader-reflection data). A **future** campaign wires the real data
  source in behind the exact seams this campaign builds
  (`BuildPlaceholderFrameDebuggerSnapshot()`/`FindEventDetailsByIndex()`).
- Any real shader/material property introspection (no real `_MainTex`/
  `_Color`/`unity_MatrixVP`-equivalent values are ever read from a real
  pipeline/descriptor set this campaign).
- A frame-history ring buffer / scrubbing through *multiple* past frames
  (the stepper row's "N of M" always reads "0 of 0" this campaign — no
  multi-frame capture history exists yet).
- Any Network/HTTP endpoint for the Frame Debugger (mirrors
  `frame-debugger-1`'s own identical non-goal for Pause/Step).
- Adding "Frame Debugger" to `EditorPanelCatalog.h`/the default dock
  layout (see Locked Design Decision #6).
- A `RenderGraphPanel`/`ProfilerPanel`-style Pause/frozen-snapshot control
  on this panel (see Locked Design Decision #7).
- Any change to `Application::Run()`, `Game::Update()`, `Renderer`, or
  the render-graph/present pipeline. This campaign touches **only**
  `src/Editor/` (plus `CMakeLists.txt`/`tests/CMakeLists.txt`/docs).

### 3.4 File-change inventory (full campaign, across all phases)

New files:
- `src/Editor/FrameDebuggerData.h`, `src/Editor/FrameDebuggerData.cpp` (PHASE1, extended in PHASE6)
- `src/Editor/Panels/FrameDebuggerPanel.h`, `src/Editor/Panels/FrameDebuggerPanel.cpp` (PHASE2, extended in PHASE3/4/5/6)
- `tests/Editor/FrameDebuggerDataTests.cpp` (PHASE1, extended in PHASE6)
- `task_manager/frame-debugger-2/PHASE1_COMPLETION_REPORT.md` .. `PHASE7_COMPLETION_REPORT.md`
- `task_manager/frame-debugger-2/CAMPAIGN_COMPLETION_REPORT.md` (written at the end of PHASE7)

Modified files:
- `src/Editor/EditorContext.h` (PHASE2 — one new bool field)
- `src/Editor/DockLayout.cpp` (PHASE2 — new "Window" menu)
- `src/Editor/ImGuiEditorLayer.cpp` (PHASE2 — new member + `BuildUI()` call)
- `CMakeLists.txt` (PHASE1 + PHASE2 — new source files)
- `tests/CMakeLists.txt` (PHASE1 — new test file)
- `AGENTS.md`, `README.md`, `TODO.md`, `docs/README.md`,
  `docs/conventions/editor-module-structure.md` (PHASE7)

### 3.5 Note on review depth (for the double-check / 2nd iteration)

**PHASE6 is the single heaviest/most detail-dense chunk of this
campaign** — the most new ImGui widgets, the most new pure formatting
helper functions, and the part of the UI that most directly mirrors the
reference screenshot's busiest region (the whole right-hand "Event
#2117: Draw Mesh" property block, the Preview/ShaderProperties tabs, and
the Textures/Vectors/Matrices subsections). It is explicitly called out
so a reviewing pass can choose to double-check it in isolation before
reviewing the campaign as a whole, if it judges that worthwhile.

### 3.6 Order of work

Work phases 1 → 7 strictly in order; each does a fast compile check
before moving on. Only PHASE7 does a full clean build (both
`GTE_ENABLE_EDITOR=ON` and `=OFF`) + full `ctest` regression + a live
runtime smoke test. See each phase file for its own exact compile-check
command and file-change inventory.
