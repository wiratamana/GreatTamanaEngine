# CAMPAIGN COMPLETION REPORT — `editor-core-separation-10`
## "ImGui Widget ID Uniqueness" (the Render Graph Panel duplicate-checkbox bug, fixed structurally, forever)

**Status:** COMPLETE — all four phases done, verified, committed.

---

## What shipped

A confirmed, live bug — `src/Editor/Panels/RenderGraphPanel.cpp`'s "Offscreen
Regime (Game View + Scene View)" table building a checkbox's ImGui ID purely from
`"##Enabled_" + pass.name`, which two intentionally-duplicate-named Atmosphere
passes (`"AtmosphereSkyViewLutPass"`, `"AtmosphereAerialPerspectiveVolumePass"`)
made collide, triggering Dear ImGui's own built-in red-highlight "Programmer error:
N visible items with conflicting ID!" tooltip — was fixed permanently, in three
senses at once:

1. **Structurally** (PHASE2/PHASE3): a new, mandatory RAII helper,
   `gte::ScopedUniqueId` (`src/Editor/ImGuiUniqueId.h/.cpp`), derives every
   per-iteration ImGui ID scope from the loop's own distinct index instead of any
   data-derived string. It was retrofitted onto the exact reported bug first
   (`RenderGraphPanel.cpp`'s three loops, PHASE2), then onto every other
   pre-existing `ImGui::PushID(...)` call site anywhere under `src/Editor/`
   (`ProjectPanel.cpp` x3, `HierarchyPanel.cpp`, `InspectorPanel.cpp` x2,
   `BoneViewerWindow.cpp` x3 — PHASE3), so the whole bug class is now structurally
   unreachable engine-wide, not just in the one reported panel.
2. **Detected proactively** (PHASE1): a small, Tier-1-tested, ImGui-agnostic
   conflict tracker (`gte::ImGuiIdConflictTracker`,
   `src/Editor/ImGuiIdConflictTracker.h/.cpp`) plus a real, ImGui/`Logger`-aware
   singleton wrapper (`gte::ImGuiIdConflictGuard`,
   `src/Editor/ImGuiIdConflictGuard.h/.cpp`) notice any FUTURE collision the instant
   it happens — before Dear ImGui's own visual red-highlight even gets a chance to
   draw — and log it exactly once per NEW incident via
   `GTE_LOG_ERROR("ImGuiIdConflict", ...)`, retrievable via
   `GET /get_logs?category=ImGuiIdConflict`. `ImGuiIdConflictGuard::BeginFrame()` is
   wired into `ImGuiEditorLayer::NewFrame()`, running every real frame.
3. **Documented and mandated forever** (PHASE4): `docs/conventions/imgui-id-uniqueness.md`
   plus a new "## ImGui Widget ID Uniqueness" section in `AGENTS.md` (placed
   immediately after "## Logging", before "## Render Target Format Matching") make
   `ScopedUniqueId` binding on all future code, not just a one-time cleanup.

## Final build/ctest numbers (before/after this campaign)

- **Before** (`editor-core-separation-9`'s own reported baseline): 1882 tests, 100%
  of executed tests passing, 2 legitimate environment-gated skips.
- **After** (this campaign's own PHASE4 full clean build + full regression pass):
  **1888 tests, 100% passing, the same 2 legitimate environment-gated skips.** The
  delta is exactly `+6` — the new `ImGuiIdConflictTrackerTests.cpp` file PHASE1
  added, with zero previously-passing test newly failing anywhere in the tree.
  Full clean build: **561/561 steps succeeded, zero errors.**

## The full, final list of every retrofitted call site (the grep gate's clean output)

Per PHASE3's own "grep gate" (`search_in_dir` for `ImGui::PushID`/`ImGui::PopID`
under `src/Editor/`, re-confirmed structurally unchanged by PHASE4 — no phase after
PHASE3 touched any of these files again):

| File | Function(s) retrofitted |
|---|---|
| `Panels/RenderGraphPanel.cpp` | `BuildPassRow()`/`BuildPassTable()`, `BuildDisabledBuiltInPassesSection()`, `BuildPluginRenderFeaturesSection()` |
| `Panels/ProjectPanel.cpp` | `RenderLeftPaneFolder()`, `RenderBreadcrumb()`, `RenderRightPaneEntry()` |
| `Panels/HierarchyPanel.cpp` | `RenderEntityNode()` |
| `Panels/InspectorPanel.cpp` | Verlet chain/joint nested loops (`chainIdScope`/`jointIdScope`) |
| `BoneViewerWindow.cpp` | `RenderBoneTreeNode()`, `RenderFlatPartRow()`, `RenderVerletChainNode()` |

The only remaining real (non-comment) `ImGui::PushID`/`ImGui::PopID` call sites
anywhere under `src/Editor/` live inside `src/Editor/ImGuiUniqueId.cpp` itself —
`ScopedUniqueId`'s own implementation, which legitimately calls the raw functions
so every other file never has to.

## The disclosed hover-simulation limitation (restated one final time)

Across PHASE2, PHASE3, and PHASE4's own live HTTP-driven smoke tests, the same
honest limitation was repeatedly, consistently disclosed rather than glossed over:
**there is no HTTP endpoint to simulate a mouse hover**, so none of these automated
checks could directly re-enact "hover the duplicate-name row, confirm Dear ImGui's
own red box no longer appears" end-to-end over the network. This is a real, standing
gap in the Networking surface (`AGENTS.md`), not something specific to this
campaign, and closing it was never in this campaign's scope. The authoritative proof
that the underlying mechanism is correct is PHASE1's deterministic, ImGui-free
Tier-1 test suite (`tests/Editor/ImGuiIdConflictTrackerTests.cpp`, 6/6 passing);
every live HTTP smoke test's own real job across all four phases was only ever to
confirm (a) nothing rendered differently after each phase's own change, and (b) no
conflict was ever logged against real, live, genuinely duplicate-named Atmosphere
pass data during an actual running session — both confirmed, repeatedly, including
in this campaign's own final PHASE4 pass.

A second, related, permanently disclosed limitation (first noted by PHASE2, still
true at closeout): no HTTP mechanism exists to scroll an ImGui panel or resize/
maximize the docked layout, so the "Offscreen Regime" table's own duplicate-named
rows themselves never fit inside any fixed-resolution `GET /get_swapchain`
screenshot taken during this campaign. This does not weaken the proof above, since
that table is built every real frame regardless of scroll position — but it does
mean no phase of this campaign ever produced a screenshot with the literal
duplicate-Atmosphere-pass-name rows visible to the human eye. A future campaign
adding panel-scroll/dock-resize HTTP control would be able to close this gap; it was
never this campaign's own job to do so.

## Explicit restatement: the root-cause data is 100% untouched, by design (LDD7)

**The Atmosphere pass-naming duplication itself — the original bug report's own
data-level root cause — remains completely, permanently unchanged.** The two
Atmosphere passes (`"AtmosphereSkyViewLutPass"`, `"AtmosphereAerialPerspectiveVolumePass"`,
both declared in `src/Renderer/Atmosphere/AtmosphereLutRenderer.cpp`) keep their
exact current hard-coded names, forever. The "Offscreen Regime" table legitimately
showing two rows with an identical `pass.name` — and toggling one of those rows'
checkboxes deliberately flipping BOTH rows' shared enabled/disabled state at once —
is a permanent, intentional fact about this engine, unchanged by any single line any
phase of this campaign touched. Nothing in `AtmosphereLutRenderer.cpp`,
`AtmospherePassSequence.cpp`, `Core.cpp`'s Atmosphere provider, or
`RenderPassToggleRegistry`'s keying scheme was ever opened, let alone edited, by any
phase. **A future reader must never conclude this campaign "fixed the duplicate pass
names" — it deliberately, permanently did not, and was never scoped to.** The fix
that shipped here lives entirely at the ImGui-ID-scope layer (deriving uniqueness
from loop position, never from data content) — the correct, durable fix for a bug
that was never really about pass names in the first place.

## Per-phase summary

- **PHASE1** — built `ImGuiIdConflictTracker`/`ImGuiIdConflictGuard` (inert
  scaffolding, nothing called it yet), wired `BeginFrame()` into
  `ImGuiEditorLayer::NewFrame()`, added 6 new Tier-1 tests. Incremental compile
  check only, per LDD8.
- **PHASE2** — built `gte::ScopedUniqueId`, retrofitted `RenderGraphPanel.cpp`'s
  three loops (the exact reported bug, fixed). Incremental compile check + a live,
  HTTP-driven screenshot/log proof.
- **PHASE3** — mechanically retrofitted every other pre-existing `PushID` call site
  (`ProjectPanel.cpp` x3, `HierarchyPanel.cpp`, `InspectorPanel.cpp` x2,
  `BoneViewerWindow.cpp` x3), confirmed via a clean "grep gate". Incremental compile
  check + a live, HTTP-driven screenshot/log proof.
- **PHASE4** — wrote `docs/conventions/imgui-id-uniqueness.md`, added the `AGENTS.md`
  section, ran the campaign's ONE full clean build (561/561 steps, zero errors) and
  ONE full `ctest` regression pass (1888/1888 tests passing, up from 1882, +6 new,
  zero regressions), did a final live HTTP-driven end-to-end proof, and wrote both
  completion reports.

See each phase's own `PHASEn_COMPLETION_REPORT.md` in this same folder for the full,
itemized, per-phase detail this summary condenses.
