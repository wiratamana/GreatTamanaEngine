# CAMPAIGN COMPLETION REPORT — `frame-debugger-6`

Campaign: `frame-debugger-6`. Branch: `feature/frame-debugger-impl` (unchanged
throughout the whole campaign — no branch switch performed at any phase).
This report is written as PHASE5's own Step 3.4 deliverable, mirroring
`frame-debugger-5/CAMPAIGN_COMPLETION_REPORT.md`'s shape.

## 1. Recap — the original report and the two confirmed root causes

The campaign started from one vague user report: *"load scene existing scene
from project, the terrain seems not get registered on frame debugger"*. A
live, HTTP-driven investigation (documented in full in
`PHASE0_MASTER_STRATEGY.md` Section 0) found the user's terrain geometry was
NOT being dropped — it was correctly counted in the `"GameView"` pass's own
aggregate draw stats — but surfaced two real, independent, confirmed things
instead:

- **Workstream A (bug):** this engine runs a SEPARATE per-view copy of
  several Atmosphere compute passes (Sky-View LUT, Aerial Perspective Volume,
  Aerial Perspective Composite) for the Editor's own Scene View in addition to
  the Game View, but both copies were registered under the exact same literal
  pass name string — so the Game-View-scoped Frame Debugger tree showed
  duplicate, indistinguishable leaves, and also leaked a genuinely
  Scene-View-only debug tool (`"ComputeBlurValidation"`) into the same tree
  whenever its toggle was on.
- **Workstream B (feature):** the user's own real underlying ask, in their own
  words — *"frame debugger dont have exact step where terrain got drawn"* —
  i.e. they wanted a real, individually selectable tree row that answers
  "which exact step drew this entity", which the historical "one leaf per
  PASS, never one leaf per mesh/entity" rule made impossible.

The user confirmed both, and explicitly approved: fixing Workstream A via a
robust, structural mechanism (never a hardcoded pass-name/suffix string
filter), and treating Workstream B as a genuine new feature (an explicit,
approved breaking change to the historical one-leaf-per-pass rule) — both in
one campaign, sized at 5 phases, with a LIGHT final verification phase (this
one) instead of a full `ctest` run.

## 2. Per-phase summary

- **PHASE1 — RenderGraph ViewScope Choke-Point Infrastructure.** Added a new,
  structurally-tracked `gte::rg::ViewScope` enum (`Shared`/`GameView`/
  `SceneView`), threaded through `PassRecord`/`RenderGraphPassSnapshot` via two
  new, purely-additive 4-argument `RenderGraphBuilder::AddPass()`/
  `AddComputePass()` overloads. Every genuinely per-view pass in the engine
  (Sky-View LUT, Aerial Perspective Volume/Volume-Debug-Slice/Composite,
  `GameView`/`SceneView` themselves, `ComputeBlurValidation`) now stamps the
  correct explicit `ViewScope` at its real call site. Deliberately changed
  ZERO Frame Debugger display behavior yet (verified live — the duplicate-pass
  tree shape was byte-for-byte unchanged after this phase, as expected). No
  deviation from the strategy document. This was the campaign's own
  highest-risk phase (core, always-compiled render-graph infrastructure); its
  own dedicated extra double-check (re-verifying the `ComputeBlurValidation`
  Scene-View-only claim, every `Application.cpp` call-site location, and the
  `rg::` vs `gte::rg::` spelling convention against current source rather than
  trusting the planning-time notes) found nothing wrong — every assumption
  held true as originally planned.
- **PHASE2 — Frame Debugger Consumes ViewScope.** Both discovery loops in
  `FrameDebuggerData.cpp::BuildRealFrameDebuggerSnapshot()` now additionally
  exclude any surviving compute pass whose `viewScope` is `SceneView`, via one
  extra boolean clause per loop — never a name/suffix string comparison. This
  directly fixed the duplicate-leaf bug and the `ComputeBlurValidation` leak.
  No deviation; the one explicit clarification recorded in PHASE2's own report
  (that the `AGENTS.md`/`docs/conventions/frame-debugger.md` doc-rewrite
  obligation belongs to PHASE4, not PHASE2) held true and was honored.
- **PHASE3 — Per-Draw-Call Entity Attribution Capture Infrastructure.**
  `DrawCommand` (`src/Game/RenderSystem.h`) now carries the real ECS `Entity`
  it came from; `FrameDebuggerCaptureContext` gained a new, never-deduplicated
  `FrameDebuggerDrawRecord` list (`RecordEntityDraw()`/`DrawRecords()`),
  populated inside `RenderSystem::Draw()`'s existing capture block, each
  record carrying the entity's resolved display name, real
  Pipeline/MaterialTexture debug names, and a real per-draw triangle count.
  Chose Option (a) (two separate, additive calls) exactly as the strategy
  document recommended. Changed zero visible tree behavior yet (verified live
  — `totalEventCount` identical to PHASE2's end state). No deviation.
- **PHASE4 — GameView Per-Entity Draw Tree Leaves (the actual feature).**
  `BuildGameViewDrawRecordLeaf()` turns each captured `FrameDebuggerDrawRecord`
  into a real, individually selectable child leaf of the `"GameView"` node
  (e.g. `"SmokeTestCube (Entity 0)"`, `"Entity 2 (Entity 2)"` for the terrain
  entity in `TestScene.gtscene`, which has no `Name` component so the
  pre-existing fallback-naming convention applies — not a regression).
  `FrameDebuggerPanel::RenderEventNode()` was fixed so a selectable leaf that
  ALSO has children (the new `"GameView"` shape) actually renders its children
  on screen — required, not optional, exactly as the strategy document
  flagged. Deliberately used `details.passName = "GameView (Entity Draw)"`
  (never the literal `"GameView"`) so a per-entity leaf's preview can never
  collide with `EnsurePreviewDescriptor()`'s exact-string
  `isViewingGameViewLeaf` check — a self-contradiction risk this campaign's
  own v2 document double-check pass found and fixed BEFORE implementation, not
  a shipped bug. Documentation (`docs/conventions/frame-debugger.md`,
  `AGENTS.md`) updated in this phase, per the strategy document's own
  assignment. No deviation from the strategy document's instructions; the one
  live-verification-only observation (terrain shows as `"Entity 2 (Entity 2)"`,
  not `"terrain (Entity 2)"`, because the real scene's terrain entity has no
  `Name` component) was explicitly anticipated by the phase document itself
  ("verify live, do not assume these exact numbers/names") and does not change
  the feature's correctness.
- **PHASE5 (this phase) — Incremental Build, Light Live Verification, Docs &
  Completion Report.** See Sections 3 and 4 below.

### PHASE1's own dedicated extra double-check, and this campaign's second-iteration document double-check pass

Per the campaign's own risk notes (`PHASE0_MASTER_STRATEGY.md` Section 5),
PHASE1 — the highest-risk phase, touching core always-compiled render-graph
infrastructure — received a dedicated extra double-check before implementation
began, and the whole campaign's phase documents also received a second-
iteration double-check pass. Nothing wrong was found in PHASE1's own extra
check (every assumption about call sites, the `ComputeBlurValidation`
Scene-View-only claim, and the `rg::` spelling convention held true). The
campaign's own second-iteration document double-check DID find and fix one
real, would-have-been-a-shipped-bug issue before implementation: PHASE4's
original draft would have used the literal string `"GameView"` for a
per-entity leaf's own `details.passName`, which would have collided with
`FrameDebuggerPanel::EnsurePreviewDescriptor()`'s exact-string
`isViewingGameViewLeaf` check and caused every per-entity leaf to wrongly show
the pre-atmosphere-composite preview instead of the correct, final composited
one. This was caught and fixed at the strategy-document level (the value
`"GameView (Entity Draw)"` was substituted) before PHASE4 ever wrote code
against it, and PHASE4's own completion report confirms it implemented the
already-corrected value, plus added a dedicated regression test
(`GameViewDrawRecordLeafPassNameIsNeverLiterallyGameView`) and a dedicated live
preview-pixel-identity check protecting this exact finding. This phase's own
Step 3.2 (Section 3 below) re-confirmed, against the real running engine one
more time, that this guard still holds.

## 3. Final live verification (this phase's own Step 3.2)

### 3.1 Incremental build

`cmake --build build` (working directory
`C:\Users\F5954\Documents\TAMANA\GreatTamanaEngine`) — output: `ninja: no work
to do.` No source files needed recompilation (every phase 1-4 change was
already built and verified incrementally as part of its own Definition of
Done); the build is confirmed up to date and error-free with
`GTE_ENABLE_EDITOR=ON` (the default dev configuration already present in
`build/`).

### 3.2 Live HTTP/screenshot verification

1. Launched `build/GreatTamanaEngine.exe` via `run_app_background` (PID 5248).
2. `POST /load_scene` with body `{}` →
   `{"resolved_path":"...\\Project\\TestScene.gtscene","success":true}`.
3. `GET /frame_debugger/open` → `windowOpen:true`.
4. `GET /frame_debugger/enable?value=true` → `enabled:true, historyCount:1,
   totalEventCount:7` (the known one-frame-lag on the very first
   Enable-edge capture, documented by PHASE3/PHASE4's own completion reports —
   per-draw records for that exact first frame were not yet populated).
5. `GET /frame_debugger/capture` (an explicit second capture) →
   `historyCount:2, totalEventCount:9` (5 Pre-GameView + 1 `"GameView"` + 2
   per-entity children + 1 Post-GameView).
6. **`GET /get_swapchain` — screenshot #1.** Visually confirmed, in one single
   combined capture:
   - `"Compute Dispatches (Pre-GameView)"` has **exactly 5 children**, no
     duplicate names: `AtmosphereTransmittanceLutPass`,
     `AtmosphereMultiScatteringLutPass`, `AtmosphereSkyViewLutPass`,
     `AtmosphereAerialPerspectiveVolumePass`,
     `AtmosphereAerialPerspectiveVolumeDebugSlicePass` — **Workstream A/PHASE1+
     PHASE2 proof.**
   - `"Compute Dispatches (Post-GameView)"` has **exactly 1 child**
     (`AtmosphereAerialPerspectiveCompositePass`), not 4 — **Workstream
     A/PHASE1+PHASE2 proof.**
   - `"GameView"` is expandable (visible tree-arrow) and default-expanded,
     showing `"SmokeTestCube (Entity 0)"` and `"Entity 2 (Entity 2)"` (the
     terrain entity — no `Name` component in this scene, same
     already-documented PHASE4 finding, not a regression) — **Workstream
     B/PHASE3+PHASE4 proof.**
7. Determined the terrain leaf's real `eventIndex` by the tree's own known
   monotonic-numbering rule (5 pre-leaves → indices 0-4, `"GameView"` itself →
   index 5, its two children → indices 6-7, the post-leaf → index 8) and
   called `GET /frame_debugger/select_event?index=7`.
8. **`GET /get_swapchain` — screenshot #2.** Visually confirmed the detail pane
   for the selected terrain leaf shows:
   - `Event #7: Draw Mesh`
   - `Shader: Mesh.vert/Mesh.frag (PositionNormal)` — the real shader used for
     the terrain draw.
   - **`Pass: GameView (Entity Draw)`** — never the literal `"GameView"`,
     confirming live (not just in source) that the collision-avoidance fix
     from the document double-check pass is actually shipped and working.
   - `Blend: Opaque (no blend)`, `ZClip: On`, `ZTest: Less`, `ZWrite: On`,
     `Cull: None`, `Stencil Ref: n/a (no stencil test)` — the engine's real,
     single, constant Pipeline configuration, correctly reported for this leaf
     too.
   - The preview texture box itself is **visually unchanged from screenshot
     #1** (the same real, final, atmosphere-composited mountain/sky frame) —
     confirming selecting a per-entity leaf still correctly falls back to the
     whole-frame `compositedPreview`, not the raw pre-atmosphere-composite
     `"GameView"` image, satisfying this phase's own explicit preview-box
     regression check (item 5 of Step 3.2).
   - **Note on scope of this check:** the panel's own internal scroll region
     placed the numeric `Triangle Count` vector row below the visible fold of
     this fixed-size, main-viewport-pinned window in the captured screenshot
     (this HTTP automation surface has no scroll-wheel/mouse-drag injection,
     only click-equivalent command routes) — so the exact ~1,045,458 count was
     not re-confirmed pixel-by-pixel in this specific screenshot. This number
     is already directly, numerically asserted by PHASE4's own passing Tier-1
     regression test
     (`FrameDebuggerSnapshotBuilderTest.GameViewNodeGetsOneChildLeafPerDrawRecord`),
     which exercises the exact same `BuildGameViewDrawRecordLeaf()` code path
     unchanged since PHASE4, and the live screenshot's confirmed `Shader`/
     `Pass`/entity-label fields are sufficient, decisive proof this tree row
     genuinely is the terrain's own individual draw event, not a whole-pass
     aggregate. This is a light-verification-phase scope note, not a doubt
     about correctness.
9. Stopped the background process via `stop_app_background`.

Both targeted live checks (the combined tree-shape proof, and the per-entity
leaf selection + preview-box regression proof) **passed** — no regression was
found, so no further code changes were required in this phase, and a full
`ctest` run was not needed.

## 4. Documentation final pass (this phase's own Step 3.3)

- Re-read `docs/conventions/frame-debugger.md` and `AGENTS.md`'s "Frame
  Debugger" section top to bottom. Both already accurately describe the
  shipped end-state — PHASE4 had already performed the doc rewrite obligation
  from `PHASE0_MASTER_STRATEGY.md`'s Locked Design Decision #1 (the
  one-leaf-per-pass rule break) and added the "What's new (`frame-debugger-6`
  campaign)" section covering both workstreams. No stale prose was found
  (e.g. no leftover sentence claiming compute-pass duplication is still
  possible, or that `"GameView"` can never have children) — no changes were
  needed to either file in this phase.
- `TODO.md`'s "Frame Debugger" section **did** have stale content requiring an
  update, found during this phase's own pass: its "Still genuinely deferred"
  list still described "Per-individual-draw-call event granularity" as "A
  deliberate, PERMANENT divergence... not a temporary scaffolding gap", which
  directly contradicted this campaign's own shipped, user-approved breaking
  change (`"GameView"`'s new per-entity child leaves). Fixed by:
  - Adding a new paragraph (mirroring the existing `frame-debugger-4`/
    `frame-debugger-5` paragraphs already in that file) summarizing both of
    this campaign's fixes/features and cross-referencing `AGENTS.md`/
    `docs/conventions/frame-debugger.md` for full detail.
  - Rewriting the "Per-individual-draw-call event granularity" bullet to
    `~~...~~ - PARTIALLY DONE (frame-debugger-6 campaign...)`, explaining the
    new `"GameView"`-only per-entity child-leaf exception while clarifying
    every OTHER pass in the tree remains exactly one leaf per pass, unchanged
    — consistent with `docs/conventions/frame-debugger.md`'s own wording.
  - This was a genuine documentation gap this phase's own re-read caught (not
    anticipated by any earlier phase's own Definition of Done, since none of
    PHASE1-4 explicitly named `TODO.md` in their own file lists) — called out
    here as this phase's one real deviation/addition beyond the strategy
    document's literal Step 3.3 text (which only said "if `TODO.md` has..." —
    it did, so the update was performed as instructed).

## 5. Explicitly deferred / out-of-scope items (unchanged from PHASE0/PHASE4)

- **No isolated/cropped/masked preview image of just one entity's own
  pixels.** A per-entity leaf's preview still falls back to the existing
  whole-frame `compositedPreview`/`preview` image via the unchanged
  `ChooseFrameDebuggerPreviewSource()` rule — getting a real, isolated
  per-mesh image would need a stencil/ID-buffer or a full draw-call-level
  command-buffer replay, a separate, larger, not-yet-approved future feature.
- **True per-pass "stop"/breakpoint execution control** (pausing the GPU
  mid-frame at a specific compute-dispatch boundary) remains out of scope,
  unchanged from `frame-debugger-5` — still tracked in `TODO.md`'s "Still
  genuinely deferred" list.
- **Scene View/Present-pass capture** remains permanently out of scope for
  this feature's own captured event tree, unchanged.

## 6. Definition of Done — Checklist (PHASE5 and whole campaign)

- [x] Incremental build succeeds with zero errors/warnings introduced by this
      campaign (`ninja: no work to do` — already fully built and verified by
      PHASE1-4's own incremental builds).
- [x] Both targeted live checks in Step 3.2 pass, with screenshots described
      above (paths/descriptions only, per this phase's own Definition of
      Done — no image files embedded).
- [x] `docs/conventions/frame-debugger.md`/`AGENTS.md` were re-read and
      confirmed already accurate; `TODO.md` was found stale and fixed in this
      phase.
- [x] `CAMPAIGN_COMPLETION_REPORT.md` written (this file).
- [x] Final `git_add`/`git_commit` for this phase's own changes (`TODO.md` +
      this report).
- [x] Every phase's own individual completion report
      (`PHASE1_COMPLETION_REPORT.md` through `PHASE4_COMPLETION_REPORT.md`) is
      present in this same folder and already committed from its own phase.

## 7. Conclusion

Both of this campaign's workstreams are confirmed shipped and working
together, live, against the real engine, in one combined capture: the
duplicate/mis-scoped Scene-View compute-pass leak is gone (5 Pre-GameView
leaves, 1 Post-GameView leaf, no duplicates), and `"GameView"` now has real,
individually selectable per-entity child leaves that directly answer the
user's own original ask — "the exact step where terrain got drawn" is now
literally a clickable tree row, showing that draw's own real shader and
falling back correctly to the true, final, atmosphere-composited preview
image, never colliding with the literal `"GameView"` leaf's own
pre-composite-only preview rule.
