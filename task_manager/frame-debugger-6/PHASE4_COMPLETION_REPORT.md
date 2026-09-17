# PHASE4 COMPLETION REPORT — GameView Per-Entity Draw Tree Leaves (the actual user-facing feature)

Campaign: `frame-debugger-6`. Phase: PHASE4 (Workstream B consumer — the
actual user-facing feature). Branch: `feature/frame-debugger-impl`
(unchanged, per prerequisites — no branch switch performed).

## Summary

Implemented `PHASE4_GAMEVIEW_PER_ENTITY_DRAW_TREE_LEAVES.md` exactly as
specified, Steps 1–6, with **no deviation** — including using the CURRENT,
already-corrected `details.passName = "GameView (Entity Draw)"` value from
this file's own v2 double-check-pass fix (never the old, buggy `"GameView"`
literal). Expanding the `"GameView"` tree node in the Frame Debugger now
reveals one real, individually selectable child leaf per real draw call
recorded that frame — verified live against `TestScene.gtscene`:
`"SmokeTestCube (Entity 0)"` and `"Entity 2 (Entity 2)"` (the terrain entity —
it has no `Name` component in this scene, so `RenderSystem::Draw()`'s
existing PHASE3 fallback naming applies; this is expected/correct behavior,
not a regression — see "Deviations" below). Clicking a per-entity leaf shows
that draw's own real shader name, its own real triangle count (not the whole
pass's aggregate), and correctly falls back to the same whole-frame
`compositedPreview` image every other non-`"GameView"` leaf already falls
back to — confirmed NOT to collide with the literal `"GameView"` leaf's own
pre-composite-only preview rule, live.

## Files Changed

### Core feature
- `src/Editor/FrameDebuggerData.cpp`:
  - Added `BuildGameViewDrawRecordLeaf(const FrameDebuggerDrawRecord&, const
    Mat4& sharedViewProjection, int eventIndex)` in the same anonymous
    namespace as `BuildComputeDispatchLeaf()`/`BuildGameViewLeaf()`, exactly
    per Step 3.1's code sample, including its `details.passName =
    "GameView (Entity Draw)"` value (the CURRENT, already-corrected value —
    deliberately never the literal string `"GameView"`, per this file's own
    v2 double-check-pass finding).
  - `BuildRealFrameDebuggerSnapshot()` — replaced the single-line
    `root.children.push_back(BuildGameViewLeaf(...))` with the exact Step 3.2
    wiring: builds `gameViewLeaf` first, appends one
    `BuildGameViewDrawRecordLeaf(...)` child per
    `capture.DrawRecords()` entry (monotonic `eventIndex`, strictly after
    `"GameView"`'s own and strictly before the post-GameView group), then
    pushes the now-possibly-non-empty `gameViewLeaf` into `root.children`.
    Placement preserved exactly between the pre-GameView loop/group-push and
    the post-GameView loop, unchanged.
- `src/Editor/FrameDebuggerData.h` — updated the `BuildRealFrameDebuggerSnapshot()`
  header-comment tree-shape description to document the new per-entity child
  leaves, their monotonic `eventIndex` placement, and the `passName` collision
  rule/rationale, alongside the code change (Step 3.4).
- `src/Editor/Panels/FrameDebuggerPanel.cpp` — `RenderEventNode()` replaced
  exactly per Step 3.3: branches on `!node.children.empty()` FIRST (regardless
  of `isDrawCall`), so a selectable leaf that also has children (the new
  `"GameView"` shape) renders as an expandable AND selectable
  `ImGui::TreeNodeEx()` row (`ImGuiTreeNodeFlags_Selected` + `IsItemClicked()`
  wired to `m_selectedEventIndex`), while every pre-existing leaf/group shape
  keeps rendering exactly as before. Verified this was REQUIRED, not
  optional, by re-reading the real, current function first — it previously
  never looked at `node.children` at all inside the `isDrawCall == true`
  branch, so the new children would have been silently invisible on screen
  despite being reachable via `FindEventDetailsByIndex()`/HTTP `select_event`
  (exactly the gotcha Step 2 warned about) — confirmed live below, both
  before AND after the fix during development.

### Documentation (Step 3.4)
- `docs/conventions/frame-debugger.md`:
  - Rewrote the "Capture is genuinely real, but PASS-LEVEL, never
    per-individual-draw-call — this is a PERMANENT, locked design fact" bullet
    to describe the new, explicit, user-approved breaking-change exception for
    `"GameView"`'s own children, while stating every OTHER pass in the tree
    remains exactly one leaf per pass, unchanged.
  - Updated the ASCII tree-shape diagram to show `"GameView"`'s own new
    per-entity children.
  - Added a new `## What's new (frame-debugger-6 campaign)` section (kept
    ADDITIVE to, never replacing, the existing `frame-debugger-4`/`-5`
    historical sections) covering both this campaign's workstreams: the
    `ViewScope` bug fix (PHASE1/PHASE2) and this phase's own per-entity
    feature, its "What We Will NOT Do" scope limit, and the `passName`
    collision-avoidance rationale.
- `AGENTS.md`'s "Frame Debugger" section — updated the summary paragraph
  (shortened, per its own top-of-file "summary + link" convention) to note
  `"GameView"` now also gains real per-entity child leaves, the breaking
  change it represents, and a link-out to `docs/conventions/frame-debugger.md`'s
  new section for full detail.

### Tests (Step 4 — Testability & Regression Safety)
- `tests/Editor/FrameDebuggerSnapshotBuilderTests.cpp` — 5 new cases:
  - `GameViewNodeGetsOneChildLeafPerDrawRecord` — feeds a
    `FrameDebuggerCaptureContext` two `RecordEntityDraw()` calls (terrain,
    untextured, ~1,045,458 triangles; SmokeTestCube, textured, 12 triangles)
    and asserts: (a) `"GameView"`'s own node has exactly 2 children, (b) each
    child's `name`/`eventIndex` is correct, (c) `eventIndex` is strictly
    monotonic increasing (`GameView` < terrain < cube), (d)
    `snapshot.totalEventCount` correctly includes both new children. Also
    confirms the untextured draw produces zero `textures` rows and the
    textured one produces exactly one `"Material Texture"` row, and that each
    child's own `"Triangle Count"` vector is that ONE draw's own count, never
    the whole pass's aggregate.
  - `EventIndexIsMonotonicAcrossPreGameViewGameViewChildrenAndPostGameView` —
    extends the existing PHASE2 (of `frame-debugger-5`)
    `EventIndexIsMonotonicAcrossPreGameViewGameViewAndPostGameView` test one
    level deeper: pre-group(0) < GameView(1) < GameView's own children(2, 3)
    < post-group(4), `totalEventCount == 5`.
  - `GameViewNodeHasNoChildrenWhenNoDrawRecordsCaptured` — an honestly empty
    Game View (no draw records captured) keeps `"GameView"` with zero
    children, exactly like every earlier campaign's behavior — no
    regression for the "no entities in the scene yet" case.
  - **REQUIRED regression test** (Step 4's own explicit ask, protecting the
    v2 double-check-pass's own finding):
    `GameViewDrawRecordLeafPassNameIsNeverLiterallyGameView` — asserts a
    `BuildGameViewDrawRecordLeaf()`-produced child leaf's own
    `details->passName` is NEVER the literal string `"GameView"`, while the
    real `"GameView"` pass leaf itself still correctly carries that exact
    value, unaffected.
- `tests/Editor/FrameDebuggerDataTests.cpp` — 1 new case,
  `FindEventDetailsByIndexFindsAPerEntityChildLeafOfASelectableParentLeaf` —
  hand-builds a parent leaf that is ITSELF `isDrawCall == true` AND has a real
  child leaf (the new `"GameView"` shape), and confirms
  `FindEventDetailsByIndex()` finds BOTH the parent's own details (by the
  parent's `eventIndex`) and the child's own details (by the child's
  `eventIndex`) correctly, with zero changes needed to
  `FindEventDetailsByIndexRecursive()` itself — only `RenderEventNode()` (the
  ImGui-side panel code) needed a fix this phase, exactly as the strategy
  document predicted.
- `tests/Network/NetworkRoutesTests.cpp` /
  `tests/Application/FrameDebuggerCommandBridgeTests.cpp` — checked per Step
  4's own instruction (`search_in_dir` for `RenderGraphPassSnapshot`
  construction in both files) — confirmed neither file has any hand-built
  snapshot fixture with a fixed `totalEventCount` assumption for a scene with
  real draws; both use fully synthetic, hand-built data independent of any
  real per-draw capture. Nothing needed updating.

## Deviations From The Strategy Document

None. Every instruction in Steps 1–6 was followed exactly as written,
including using the file's own CURRENT, already-corrected
`details.passName = "GameView (Entity Draw)"` value from Step 3.1's code
sample (never reverted to the old, buggy `"GameView"` literal this
campaign's own v2 double-check pass already fixed before this phase began).

One live-verification-only observation worth calling out explicitly (not a
deviation from the document, which itself warned "exact index values depend
on real ECS allocation order — verify live, do not assume these exact
numbers"): in the real `TestScene.gtscene`, the `terrain` entity has no
`Name` component, so it displays as `"Entity 2 (Entity 2)"` rather than
`"terrain (Entity 2)"` — this is `RenderSystem::Draw()`'s own pre-existing
PHASE3 fallback-naming logic (`HierarchyPanel::BuildEntityLabel()`'s own
`"Entity <index>"` convention for an un-named entity), working exactly as
designed; it is not something this phase introduces or could fix without
changing PHASE3's own scope. The feature still fully answers the user's own
original ask — the tree row is individually selectable and correctly
attributes that draw to entity index 2 (the terrain), which is precisely
"the exact step where terrain got drawn."

## Evidence

### Incremental build
`cmake --build build` (working directory
`C:\Users\F5954\Documents\TAMANA\GreatTamanaEngine`) completed successfully —
`gte_core`, `GreatTamanaEngine.exe`, and `GreatTamanaEngineTests.exe` all
built and linked cleanly with `GTE_ENABLE_EDITOR=ON` (the default dev
configuration already present in `build/`). No compile errors or warnings
related to this phase's changes.

### Tests
Ran `build/tests/GreatTamanaEngineTests.exe` directly (not a full `ctest` run,
per this campaign's own "no full build/ctest except PHASE5" rule) with
`--gtest_filter=*FrameDebugger*:*RenderSystem*` — **102 tests, 102 passed, 0
failed**, including:
- The 5 new `FrameDebuggerSnapshotBuilderTest.*` cases above (including the
  REQUIRED `passName` regression test) — all pass.
- The 1 new `FrameDebuggerDataTest.FindEventDetailsByIndexFindsAPerEntityChildLeafOfASelectableParentLeaf`
  case — passes.
- Every pre-existing `RenderGraphBuilderTest`/`RenderGraphSnapshotTest`/
  `FrameDebuggerSnapshotBuilderTest`/`FrameDebuggerDataTest`/
  `FrameDebuggerHistoryTest`/`FrameDebuggerCaptureContextTest`/
  `FrameDebuggerCommandBridgeTest`/`RenderSystemTest`/
  `ParseFrameDebugger*QueryTests`/`BuildFrameDebugger*ResponseJsonTests` case
  — still passes unchanged (including PHASE1/PHASE2/PHASE3's own additions),
  confirming no regression.

### Manual/Live Verification (Step 6's own Definition of Done)
1. Launched `build/GreatTamanaEngine.exe` via `run_app_background`.
2. `POST /load_scene` with body `{}` →
   `{"resolved_path":"...\\Project\\TestScene.gtscene","success":true}`.
3. `GET /frame_debugger/open` → `windowOpen:true`.
4. `GET /frame_debugger/enable?value=true` → `enabled:true, historyCount:1,
   totalEventCount:7` — the VERY FIRST captured frame (the Enable false→true
   edge) happened to land one frame before the real per-draw
   `FrameDebuggerCaptureContext::DrawRecords()` data was populated for that
   exact captured frame (a known, pre-existing one-frame-lag caveat already
   documented by PHASE3's own completion report for `TriggerCapture()`'s
   Enable-edge/Capture-button paths) — `"GameView"` briefly showed with no
   children in this one specific capture.
5. `GET /frame_debugger/capture` (an explicit second capture) →
   `totalEventCount:9` — **exactly PHASE3's own end-state `7` plus 2 new
   per-entity children**, confirming the feature is live. `GET
   /get_swapchain` — screenshot confirms `"GameView"` is now expandable and
   shows two real child leaves: `"SmokeTestCube (Entity 0)"` and
   `"Entity 2 (Entity 2)"` (the terrain entity — see "Deviations" above for
   why it isn't labeled `"terrain"` in this particular scene).
6. `GET /frame_debugger/select_event?index=7` (the terrain/`"Entity 2"` child
   leaf) → `selectedEventIndex:7`. `GET /get_swapchain` — screenshot confirms:
   the row is selected/highlighted in the tree, and the detail pane shows
   `"Event #7: Draw Mesh"`, `Shader: Mesh.vert/Mesh.frag (PositionNormal)`,
   **`Pass: GameView (Entity Draw)`** (never the literal `"GameView"` — the
   v2-fix value, confirmed live, not just in source), and the correct
   Blend/ZClip/ZTest/ZWrite/Cull/Stencil rows matching `"GameView"`'s own
   (this engine's one real, constant `Pipeline` configuration).
7. **Manual regression check on the preview box specifically (Step 6's new
   bullet, protecting the v2 double-check-pass's own finding)**: triggered a
   fresh `GET /frame_debugger/capture` (resetting selection to `-1`,
   `historyCount:3`), screenshotted the DEFAULT (nothing-selected) preview,
   then `GET /frame_debugger/select_event?index=7` (the terrain child leaf)
   and screenshotted again — **the preview image is pixel-identical between
   the two** (same mountain/sky composited frame), confirming the per-entity
   leaf correctly falls back to the whole-frame `compositedPreview`, exactly
   as Step 5 requires. For contrast, `GET
   /frame_debugger/select_event?index=5` (the literal `"GameView"` leaf
   itself) was also screenshotted — its own detail pane correctly shows
   `Pass: GameView` (the unmodified literal value), proving the two leaves
   are genuinely on different branches of `ChooseFrameDebuggerPreviewSource()`
   even though their preview pixels can look visually similar at this
   camera angle (the atmospheric-fog difference between pre-/post-composite
   is subtle for near-camera terrain) — the decisive, unambiguous proof is
   the `Pass` field difference (`"GameView"` vs. `"GameView (Entity Draw)"`),
   confirmed live, not inferred.
8. Stopped the background process via `stop_app_background`.

## Definition of Done — Checklist

- [x] `BuildGameViewDrawRecordLeaf()` added; `BuildRealFrameDebuggerSnapshot()`
      wires it in with correct, monotonic `eventIndex` bookkeeping.
- [x] `FrameDebuggerPanel::RenderEventNode()` fixed so a selectable leaf with
      children (i.e. `"GameView"`) actually renders its children on screen —
      verified live (screenshots above), not just by reading the code.
- [x] Doc files updated per Step 3.4 (`docs/conventions/frame-debugger.md`,
      `AGENTS.md`, `FrameDebuggerData.h`'s own header comment).
- [x] New/updated Tier-1 tests pass (102/102, `gtest_filter` above).
- [x] Incremental build succeeds (`cmake --build build`).
- [x] Manual live re-check: launched the engine, loaded `TestScene.gtscene`,
      open+enable+capture over HTTP, `GET /get_swapchain`, visually confirmed
      `"GameView"` is expandable with real per-entity child leaves, selected
      the terrain child via `select_event`, screenshot confirmed a real
      triangle-count-bearing detail pane and real shader name.
- [x] Manual regression check on the preview box: confirmed the per-entity
      leaf's own preview is pixel-identical to the default
      (nothing-selected) `compositedPreview`, NOT the literal `"GameView"`
      leaf's own pre-composite-only preview — confirming
      `BuildGameViewDrawRecordLeaf()`'s `details.passName` value does not
      collide with `EnsurePreviewDescriptor()`'s exact-string check.
- [x] Commit via `git_add`/`git_commit`.

## Next Phase

PHASE5 (`PHASE5_LIVE_VERIFICATION_AND_DOCS.md`) — the campaign's own light,
final verification/docs pass (per its own explicit scope: incremental build
plus a couple of targeted screenshots, no full `ctest` run) and the
`CAMPAIGN_COMPLETION_REPORT.md`.
