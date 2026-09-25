# PHASE0 — MASTER STRATEGY
## Campaign: `editor-core-separation-10` — "ImGui Widget ID Uniqueness" (the Render Graph Panel duplicate-checkbox bug, fixed structurally, forever)

This is the orchestrator document. Every child phase (`PHASE1`..`PHASE4`) must
read this file FIRST before doing any work, and must re-read the previous
phase's own completion report (`PHASEn_COMPLETION_REPORT.md`) for continuity
clues. This file is the single source of truth for scope, locked decisions,
and the phase order. Do not deviate from the Locked Design Decisions below
without stopping and asking the user via `ask_questions`.

---

## Step 1: The Goal (Where are we going?)

Make it **structurally impossible**, from this campaign forward, for two
on-screen ImGui widgets in this Editor to ever end up sharing the same
internal ImGui ID within a single frame — **without touching whatever
application-level data caused the name collision in the first place** — and
back that structural guarantee with a **proactive, logged, engine-owned
early-warning system** so that if this bug class is ever reintroduced by
future code (a new panel, a new loop, a new author who doesn't know this
history), it is caught immediately and loudly through this engine's own
Logger (`GET /get_logs`), instead of silently manifesting months later as a
confusing red-highlight visual glitch that looks like a rendering bug.

Two deliverables, both permanent, both code:

1. **A structural fix**: a small, mandatory RAII helper
   (`gte::ScopedUniqueId`) that any ImGui code drawing a widget inside a loop
   over runtime data must use — it derives ID uniqueness from the loop's own
   iteration position (which is *always* distinct by construction), never
   from the data's content (a name string, which is *never* guaranteed
   distinct). Retrofitted onto the exact reported bug (`RenderGraphPanel.cpp`)
   and then onto every other existing ImGui loop in the Editor, so the whole
   Editor — not just one panel — is permanently protected.

2. **A proactive detection system**: a tiny, Tier-1-testable conflict
   tracker (`gte::ImGuiIdConflictTracker`) plus an Editor-owned wrapper
   (`gte::ImGuiIdConflictGuard`) that notices a real ID collision the instant
   it happens (before Dear ImGui's own built-in visual red-highlight even
   gets a chance to draw), and reports it once, clearly, via
   `GTE_LOG_ERROR(...)` — visible in the Editor's "Log" panel and via
   `GET /get_logs` — naming the exact call site and the exact key that
   collided, so a future regression is a five-second log read, not a
   detective investigation like the one that produced this campaign's own
   source bug report.

## Step 2: The Situation (Where are we now?)

### 2.1 — What actually happened (confirmed, do not re-investigate)

Full root-cause analysis already exists and is TRUSTED AS FACT for this
campaign:

`C:\Users\F5954\Documents\TAMANA\GreatTamanaEngin-Ideas\bugs\2026-09-25_RenderGraphPanel_Duplicate_Checkbox_ID_Highlight_Bug.txt`

Summary: `src/Editor/Panels/RenderGraphPanel.cpp`'s `BuildPassRow()` builds a
checkbox's ImGui ID purely from `"##Enabled_" + pass.name`. The "Offscreen
Regime (Game View + Scene View)" table intentionally lists Game View's and
Scene View's render-graph passes together, in one flat list, in one frame.
Two Atmosphere passes (`"AtmosphereSkyViewLutPass"`,
`"AtmosphereAerialPerspectiveVolumePass"`, both declared in
`src/Renderer/Atmosphere/AtmosphereLutRenderer.cpp`) are registered under a
hard-coded literal name that is never made unique per view — so the SAME
name appears twice in that one table, twice in one frame, and therefore both
rows' checkboxes get the literal same ImGui ID. Dear ImGui's own built-in
"ID Conflict" debug safety net (`io.ConfigDebugHighlightIdConflicts`,
default `true`, `third_party/imgui/imgui.cpp`) then draws a red rectangle
around both and shows a "Programmer error: N visible items with conflicting
ID!" tooltip the instant either row is hovered — which is exactly what the
user saw and (reasonably) mistook for a rendering bug.

### 2.2 — What we are explicitly NOT doing (the user's own instruction)

**We are not fixing the Atmosphere pass-naming duplication.** The two
Atmosphere passes keep their exact current hard-coded names, forever, under
this campaign. The fact that the "Offscreen Regime" table can legitimately
show two rows with an identical `pass.name` — and that toggling one of those
rows' checkboxes deliberately flips BOTH rows' shared enabled/disabled state
at once (see `RenderGraphPanel.cpp` lines 63-69's existing tooltip text,
already documenting this as intentional) — is a **permanent, intentional,
unchanged fact** about this engine, both before and after this campaign.
Nothing in `AtmosphereLutRenderer.cpp`, `AtmospherePassSequence.cpp`,
`Core.cpp`, `RenderGraphSnapshot.cpp`, or `RenderGraphMetadata.cpp` is
touched by any phase below. If a future reader is tempted to "finally fix"
the duplicate pass names, that is explicitly out of scope here — a possible
future campaign, not this one.

### 2.3 — Why this is fixable anyway ("make the system smarter")

The bug is not really about pass names at all. It is about ONE FILE
(`RenderGraphPanel.cpp`) deriving a widget's uniqueness entirely from
application data that nobody ever promised to be unique. The real, durable
fix has nothing to do with pass names — it is to stop trusting data content
for ID uniqueness ANYWHERE in the Editor, and instead always derive it from
the one thing that IS always unique by construction: **the loop's own
iteration position**. A checkbox row is never drawn twice at the same loop
index in the same frame — so keying every per-row ID scope off that index
(in addition to, never instead of, a human-readable label for debuggability)
makes the entire bug class structurally unreachable, regardless of whatever
duplicate/garbage/adversarial data a future feature ever feeds into a table.

### 2.4 — Current codebase inventory (confirmed via `search_in_dir`, may
drift slightly by implementation time — re-verify, do not blindly trust
line numbers below)

Every existing `ImGui::PushID(...)` call site in `src/Editor/` today:

| File | Line(s) | Current key | Already index-based? |
|---|---|---|---|
| `Panels/RenderGraphPanel.cpp` | 292 | `entry.name.c_str()` (plugin feature name) | No — latent duplicate-name risk, not yet reported |
| `Panels/HierarchyPanel.cpp` | 70 | `static_cast<int>(entity.index)` | Yes |
| `Panels/InspectorPanel.cpp` | 755, 762 | `chainIndex`, `jointIndex` | Yes |
| `Panels/ProjectPanel.cpp` | 135, 224 | `entry.relativePath.c_str()` | No — latent risk |
| `Panels/ProjectPanel.cpp` | 209 | `segmentIndex++` | Yes |
| `BoneViewerWindow.cpp` | 587, 643, 712 | `boneIndex`, `index`, `chainIndex` | Yes |

Plus the exact reported bug's own two checkbox-ID-building call sites, which
do **not** even use `PushID` today (they build the ID directly into the
widget's own label string instead):

| File | Line(s) | Pattern |
|---|---|---|
| `Panels/RenderGraphPanel.cpp` | 59-60 (`BuildPassRow`) | `"##Enabled_" + pass.name` |
| `Panels/RenderGraphPanel.cpp` | 357-358 (`BuildDisabledBuiltInPassesSection`) | `"##Enabled_" + state.name` |

### 2.5 — Relevant engineering conventions already in this codebase (follow
these, do not invent new ones)

- Tier-1 testability discipline (`AGENTS.md`, "Testability & Regression
  Safety"): pure logic gets its own `tests/<Layer>/...Tests.cpp` file,
  mirroring its `src/` folder. `tests/Editor/` already exists and already
  mirrors `src/Editor/` 1:1 for exactly this reason.
- Logging discipline (`AGENTS.md`, "Logging"): the ONLY sanctioned way to
  log anything anywhere in this engine is the `GTE_LOG_DEBUG/INFO/WARNING/
  ERROR` macros (`src/Core/Logging.h`) — never `std::cerr`/`printf`/`assert`
  for anything a human is meant to read. Retrievable live via
  `GET /get_logs` and `POST /clear_logs`.
- Meyers-singleton precedent for a process-global, main-thread-only Editor
  utility with no natural single owner to thread a reference through:
  `LoggerLogSink::Instance()` (`src/Editor/Logger.h`) is the exact pattern
  to copy for `ImGuiIdConflictGuard::Instance()`.
- Build system: both `CMakeLists.txt` (root, `gte_editor` target's source
  list) and `tests/CMakeLists.txt` (`GTE_TEST_SOURCES`) are **explicit file
  lists**, not globs — every new `.cpp`/test file MUST be added by hand to
  both, in the existing alphabetically-loose "near its sibling files" style
  already used there (see `src/Editor/Logger.h`/`.cpp` around root
  `CMakeLists.txt` line ~988, and `Editor/LoggerTests.cpp` around
  `tests/CMakeLists.txt` line ~2309, as the nearest anchor points today).
- Dear ImGui's own `io.ConfigDebugHighlightIdConflicts` stays at its
  default (`true`) — see Locked Design Decision #4 below. This campaign adds
  a second, earlier, logged safety net; it does not remove or silence the
  first one.

## Step 3: The Plan (super-detailed strategy)

### 3.0 — Locked Design Decisions (confirmed; the user was unavailable to
answer live, so these were decided using best engineering judgment for an
AI-in-the-loop, minimal-human-intervention workflow — do not silently
re-litigate these in a later phase; if a later phase finds a genuine reason
one of these is wrong, stop and use `ask_questions` before deviating)

- **LDD1 — Scope: whole Editor, not just the reported panel.** Every
  existing ImGui loop in `src/Editor/` (inventory in 2.4 above) is migrated
  onto the new mandated helper, not just `RenderGraphPanel.cpp`. "Never
  happen again once and for all" is read literally: engine-wide, permanent,
  and binding on all future code via a new `AGENTS.md` convention section
  (PHASE4).
- **LDD2 — Log once per NEW conflict, not once per frame.** A genuine
  conflict, if one ever recurs, must not spam ~60 identical log lines per
  second while the offending panel stays open/hovered. `ImGuiIdConflictGuard`
  tracks "currently ongoing" conflicts across frames and only logs the
  transition into a new conflict — see PHASE1 for the exact mechanism.
- **LDD3 — Keep `io.ConfigDebugHighlightIdConflicts` at its Dear ImGui
  default (`true`), untouched.** Our new system is an ADDITIONAL, earlier,
  logged safety net — never a replacement for Dear ImGui's own visual one.
  No phase below sets this flag anywhere. If somehow a real conflict still
  reaches Dear ImGui's own detector after this campaign (should be
  structurally impossible for anything routed through `ScopedUniqueId`, but
  never impossible for code that ISN'T), the red highlight staying available
  is a deliberate, permanent, second line of defense.
- **LDD4 — Detection is a logged warning only. It never crashes, asserts,
  or throws.** This is a UI-layer diagnostic, not a data-integrity
  contradiction like `DetectRenderPassEventContradictions()`. An autonomous
  AI-driven session must be able to keep running and keep working even if a
  conflict is (incorrectly) detected somewhere — a hard crash would instead
  block an unattended agent entirely. `GTE_LOG_ERROR` is the ceiling.
- **LDD5 — No new HTTP endpoint.** `GET /get_logs` (category filter
  `"ImGuiIdConflict"`) is suffient for CI/automation to check for conflicts.
  Do not add a dedicated `/imgui_id_conflicts` endpoint — it would be a
  second way to answer a question `GET /get_logs` already answers, for zero
  real benefit.
- **LDD6 — The pure detection logic must be genuinely Tier-1-testable, with
  zero dependency on `<imgui.h>` or `Logger`.** `ImGuiIdConflictTracker`
  (PHASE1) is a small, standalone class operating only on plain
  `std::uint32_t` values, tested directly with no live ImGui context. The
  ImGui/Logger-aware wrapper around it (`ImGuiIdConflictGuard`) is
  deliberately Tier 2 / untested-by-design, exactly like every other
  ImGui-owning class in this codebase (mirrors the `GpuMemoryTracker`
  pure-vs-Vulkan-owning-class split already documented in `AGENTS.md`).
- **LDD7 — Root-cause data (Atmosphere pass names, shared toggle-registry
  keying) is permanently out of scope.** Restated from 2.2 for emphasis —
  no phase may touch `AtmosphereLutRenderer.cpp`,
  `AtmospherePassSequence.cpp`, `Core.cpp`'s Atmosphere provider, or
  `RenderPassToggleRegistry`'s keying scheme.
- **LDD8 — No full build / full `ctest` regression except in the final
  phase (PHASE4).** Phases 1-3 use incremental/quick compile checks only
  (per the campaign's own operating rules) — this machine's full build and
  full test suite are slow and must not be run repeatedly.

### 3.1 — Phase breakdown (this is the authoritative phase list)

1. **PHASE1 — ID Conflict Detection Foundation.** Build the two new,
   ImGui-agnostic-at-the-core pieces from scratch: `ImGuiIdConflictTracker`
   (pure, Tier-1-tested) and `ImGuiIdConflictGuard` (the Editor-owned
   singleton wrapper using it + `Logger`). Wire `BeginFrame()` into
   `ImGuiEditorLayer::NewFrame()`. Nothing calls this yet — it is inert
   scaffolding until PHASE2. Ends with a passing new Tier-1 test file and an
   incremental compile check.

2. **PHASE2 — `ScopedUniqueId` Helper + The Exact Reported Bug, Fixed.**
   Build the public `gte::ScopedUniqueId` RAII helper on top of PHASE1's
   guard, then retrofit `RenderGraphPanel.cpp`'s THREE loops
   (`BuildPassRow`/`BuildPassTable`, `BuildDisabledBuiltInPassesSection`,
   `BuildPluginRenderFeaturesSection`) to use it. This phase alone already
   fixes the literal bug from the bug report. Ends with an incremental
   compile check and a live, HTTP-driven screenshot proving the "Offscreen
   Regime" table still renders correctly (duplicate Atmosphere pass names
   still visibly present, by design) plus a `GET /get_logs` check showing no
   `"ImGuiIdConflict"` entries during a live session.

3. **PHASE3 — Editor-Wide Retrofit Sweep.** Migrate every OTHER existing
   `ImGui::PushID(...)` call site inventoried in 2.4 onto `ScopedUniqueId`
   too (`ProjectPanel.cpp` x3 spots, `HierarchyPanel.cpp`, `InspectorPanel.cpp`
   x2 spots, `BoneViewerWindow.cpp` x3 spots) — making LDD1 concretely true.
   Ends with a `search_in_dir` "grep gate" proving the only remaining raw
   `ImGui::PushID`/`ImGui::PopID` call sites anywhere under `src/Editor/` are
   inside `ScopedUniqueId`'s own implementation file, plus an incremental
   compile check.

4. **PHASE4 — Documentation, Full Regression, and Campaign Closeout.**
   Write `docs/conventions/imgui-id-uniqueness.md`, add the matching
   `AGENTS.md` section (mandating `ScopedUniqueId` for all FUTURE code, so
   this is binding forever, not just a one-time cleanup), run a full clean
   build + full `ctest` regression, and do a final live HTTP-driven proof.
   Ends with `CAMPAIGN_COMPLETION_REPORT.md`.

### 3.2 — File manifest (every file any phase creates or edits)

**New files:**
- `src/Editor/ImGuiIdConflictTracker.h` (PHASE1)
- `src/Editor/ImGuiIdConflictTracker.cpp` (PHASE1)
- `src/Editor/ImGuiIdConflictGuard.h` (PHASE1)
- `src/Editor/ImGuiIdConflictGuard.cpp` (PHASE1)
- `tests/Editor/ImGuiIdConflictTrackerTests.cpp` (PHASE1)
- `src/Editor/ImGuiUniqueId.h` (PHASE2)
- `src/Editor/ImGuiUniqueId.cpp` (PHASE2)
- `docs/conventions/imgui-id-uniqueness.md` (PHASE4)

**Edited files:**
- `CMakeLists.txt` (root) — add all 6 new `.h`/`.cpp` pairs to `gte_editor`'s
  source list (PHASE1, PHASE2)
- `tests/CMakeLists.txt` — add the one new test file (PHASE1)
- `src/Editor/ImGuiEditorLayer.cpp` — one new line in `NewFrame()` (PHASE1)
- `src/Editor/Panels/RenderGraphPanel.cpp` — three call sites retrofitted
  (PHASE2)
- `src/Editor/Panels/ProjectPanel.cpp` — three call sites retrofitted
  (PHASE3)
- `src/Editor/Panels/HierarchyPanel.cpp` — one call site retrofitted
  (PHASE3)
- `src/Editor/Panels/InspectorPanel.cpp` — two call sites retrofitted
  (PHASE3)
- `src/Editor/BoneViewerWindow.cpp` — three call sites retrofitted (PHASE3)
- `AGENTS.md` — one new section (PHASE4)

### 3.3 — Non-Goals (explicit, so nobody "helpfully" expands scope)

- No fix to Atmosphere pass naming or `RenderPassToggleRegistry` keying
  (LDD7).
- No change to `io.ConfigDebugHighlightIdConflicts` (LDD3).
- No new HTTP endpoint (LDD5).
- No assert/crash-on-detect behavior (LDD4).
- No touching of any panel/file outside `src/Editor/` — this is purely an
  Editor-UI-layer concern; `gte_core` is untouched in every phase.
- No attempt to make `pass.name` (or any other data key) unique — uniqueness
  is solved entirely at the ImGui-ID-scope layer, never at the data layer.

### 3.4 — How each phase should verify its own work (code-only, per the
campaign's own operating rules — no full build/test until PHASE4)

- Incremental compile check: rebuild only the changed/added translation
  units via the existing `build` directory (`cmake --build build` picks up
  changed files incrementally — this is NOT the same as a from-scratch full
  build, and is fine to run every phase).
- `search_in_dir` sweeps to confirm no leftover raw `PushID` call sites
  (PHASE3) and no stray references to removed code.
- Live, HTTP-driven visual/log proof via `gte_send_request` where a phase's
  own change is visually/behaviorally observable (PHASE2, PHASE4) — use
  `run_app_background` to launch the built Editor executable, `GET
  /activate_tab` to bring the relevant panel to the front, `GET
  /get_game_view` or `/get_swapchain` for a screenshot, `GET /get_logs` to
  confirm no unexpected `"ImGuiIdConflict"` entries, then
  `stop_app_background` to close it again.

### 3.5 — Reading order for whoever implements this

Read this file, then the phase you were assigned, in full, before writing
any code. Each phase file below is self-contained but assumes everything in
this file (goal, situation, locked decisions, non-goals) as shared context —
it will not repeat all of it.

- `PHASE1_ID_CONFLICT_DETECTION_FOUNDATION.md`
- `PHASE2_SCOPED_UNIQUE_ID_HELPER_AND_RENDERGRAPHPANEL_FIX.md`
- `PHASE3_EDITOR_WIDE_RETROFIT_SWEEP.md`
- `PHASE4_DOCS_FULL_REGRESSION_AND_CAMPAIGN_CLOSEOUT.md`
