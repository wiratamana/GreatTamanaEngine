# PHASE2 COMPLETION REPORT — Frame Debugger Unified Pass-Ownership Rework

_Child of `PHASE0_MASTER_STRATEGY.md` (`task_manager/render-pass-2/`). This
phase's own strategy file: `PHASE2_FRAME_DEBUGGER_UNIFIED_PASS_OWNERSHIP_REWORK.md`._

## What Was Done

Implemented the plan exactly as written, step by step, entirely inside
`src/Editor/FrameDebuggerData.cpp` and `src/Editor/FrameDebuggerData.h`:

1. **New anonymous-namespace helpers** (`FrameDebuggerData.cpp`, added
   immediately after `WriteRowLabelForKind()`, before `BuildComputeDispatchLeaf()`):
   - `WrapPassWithOwnedChildEvent(FrameDebuggerEventNode passLevelNode, int
     childEventIndex, const std::string& childEventLabel)` — copies the
     already-built pass-level leaf, retargets the copy's own
     `name`/`eventIndex`/`details->eventIndex`/`details->eventLabel` to the
     child's structural label, clears the copy's (always-empty) `children`,
     and attaches it as `passLevelNode`'s one and only child.
   - `GraphicsChildEventLabelFor(rg::RenderPassDrawKind drawKind)` — an
     exhaustive switch (no `default:`, mirroring
     `ReadRowLabelForKind()`/`WriteRowLabelForKind()`'s own convention)
     mapping `DrawMesh`/`DrawQuad`/`Blit` to `"Draw Mesh"`/`"Draw
     Quad"`/`"Blit"`.
2. **`BuildGraphicsPassLeaf()`'s own `details.eventLabel`** — changed from
   the hardcoded placeholder `"Draw Pass"` to
   `GraphicsChildEventLabelFor(pass.drawKind)`, so the parent row and its new
   child row both report the same real, structural operation label.
3. **All three call sites in `BuildRealFrameDebuggerSnapshot()`** updated to
   wrap their leaf via `WrapPassWithOwnedChildEvent()`, each now consuming
   exactly 2 `nextEventIndex` values per surviving pass (parent, then child,
   assigned back-to-back):
   - The pre-view loop (`Compute LUT`/`Compute Dispatches (Pre-GameView)`) —
     wraps with child label `"Compute Dispatch"`.
   - The view-region walk's `else` branch (`"DrawSkyBackground"`, and any
     future `"RenderTransparent"`) — wraps with child label
     `GraphicsChildEventLabelFor(pass.drawKind)` (`"Draw Quad"` for
     `"DrawSkyBackground"` today, per PHASE1's tagging).
   - The post-view loop (`Compute Dispatches (Post-GameView)`) — wraps with
     child label `"Compute Dispatch"`.
4. **`BuildRenderOpaqueLeaf()`/`BuildRenderOpaqueDrawRecordLeaf()` and their
   one call site** (the `if (isRenderOpaqueLeaf)` branch, lines ~801-817 in
   the final file) were re-read after every edit above and confirmed
   BYTE-FOR-BYTE unchanged from before this phase — required self-check per
   Step 3.6, satisfied.
5. **`FrameDebuggerData.h`'s `BuildRealFrameDebuggerSnapshot()` doc
   comment** — the ASCII tree diagram was updated to show every pass-level
   leaf (`"AtmosphereTransmittanceLutPass"`-style Compute LUT entries,
   Pre/Post-GameView compute dispatches, `"DrawSkyBackground"`, a future
   `"RenderTransparent"`) as a nested `v PassName -> child` shape instead of
   a flat bullet, and a new paragraph explains `WrapPassWithOwnedChildEvent()`'s
   role plus the new doubled `nextEventIndex`-per-pass consumption rule, so a
   future reader does not have to re-read the `.cpp` implementation to
   understand the eventIndex-spacing invariant.
6. **`src/Editor/Panels/FrameDebuggerPanel.cpp`** — confirmed via `git
   status` after all edits: NOT modified at all (zero changes), exactly as
   required — its existing `!node.children.empty()` -driven `RenderEventNode()`
   automatically starts drawing an expandable "v" arrow for every one of
   these newly-nested rows with no code changes needed there.

## Exact New `eventIndex`-Consumption Rule

Before this phase, every pass leaf built by `BuildComputeDispatchLeaf()`/
`BuildGraphicsPassLeaf()` consumed exactly ONE `nextEventIndex` value.
After this phase, every one of those same three call sites consumes exactly
TWO, in this fixed order: `parentIndex = nextEventIndex++` (assigned to the
pass-level node when it is built), then `childIndex = nextEventIndex++`
(passed into `WrapPassWithOwnedChildEvent()` immediately afterward, before
moving on to the next pass). This preserves the function's own pre-existing
"chronological, monotonically increasing eventIndex" invariant, since both
indices for the same pass are always assigned back-to-back. `"RenderOpaque"`
itself is unaffected by this rule change — it still consumes exactly one
index for its own leaf, plus one more per real per-entity draw record, via
its own pre-existing, untouched mechanism.

## Deviations From The Plan

None. Every step (3.1 through 3.7) was implemented exactly as specified in
`PHASE2_FRAME_DEBUGGER_UNIFIED_PASS_OWNERSHIP_REWORK.md`, at line locations
matching PHASE0/PHASE2's own predicted locations (small +/- a few lines due
to comment-only drift already present in the file, nothing structural). No
design ambiguity was hit that the plan didn't already resolve, so
`ask_questions` was not needed for this phase.

## Test Files This Phase Is Known To Have Broken

Per this phase's own Step 3.9 instruction (do not fix these — PHASE3's job;
just name them so PHASE3 starts from a documented, known state):

- **`tests/Editor/FrameDebuggerSnapshotBuilderTests.cpp`** — the file most
  heavily impacted. The overwhelming majority of its ~31 `TEST(...)` cases
  call `BuildRealFrameDebuggerSnapshot()` directly and assert either an
  exact `snapshot.totalEventCount` number (now doubled for every
  Compute-kind/other-Graphics-kind pass involved), an exact `eventIndex`
  number, or a childless (`children.empty()`) shape for a Compute LUT/
  Compute Dispatch/`"DrawSkyBackground"` leaf. Known/expected-to-fail (not
  an exhaustive re-derivation, since re-running was explicitly out of scope
  this phase) includes at least:
  `RenderOpaqueWithNoComputePassesProducesExactlyOneLeaf`,
  `MixedComputeLutAndPreGameViewCategoriesProduceBothGroupsInFixedOrder`,
  `OnlyAtmosphereLutCategoryPreGameViewPassProducesOnlyComputeLutGroup`,
  `PostGameViewComputePassProducesLeafUnderPostGameViewGroup`,
  `PreAndPostGameViewGroupsAppearTogetherAsSiblingsInRealExecutionOrder`,
  `SceneViewScopedPreGameViewPassIsExcludedEvenWhenNameCollidesWithGameViewOne`,
  `SceneViewScopedPostGameViewPassIsExcludedEvenWhenNameCollidesWithGameViewOne`,
  `SharedViewScopedPassStillAppearsNormally`,
  `EventIndexIsMonotonicAcrossPreGameViewRenderOpaqueChildrenAndPostGameView`,
  `CulledComputePassIsExcludedFromEitherComputeDispatchGroup`,
  `ComputeDispatchLeafLabelsReadWriteRowsByRealResourceKind`,
  `EventIndexIsMonotonicAcrossPreGameViewGameViewAndPostGameView`,
  `PreGameViewLeafGetsNotYetDrawnStepPreviewKind`,
  `PostGameViewLeafBeforeCompositePassGetsPreCompositeStepPreviewKind`,
  `CompositePassLeafItselfGetsPostCompositeStepPreviewKind`,
  `PostGameViewLeavesDefaultToPostCompositeWhenNoCompositePassSurvives`,
  `DrawSkyBackgroundLeafSurvivesAlongsideComputeDispatchSplit` (PHASE0's own
  named example — this test literally asserts the OLD flat/childless shape
  this phase intentionally replaces),
  `ViewRegionHasExactlyRenderOpaqueAndDrawSkyBackgroundWhenRenderTransparentAbsent`
  (PHASE0's other named example), and
  `DebugCategoryGraphicsPassInsideViewRegionProducesNoExtraLeaf`. Tests that
  only ever exercise `"RenderOpaque"`'s own leaf/per-entity children in
  isolation (e.g. `NoRenderOpaquePassProducesEmptyResult`,
  `RenderOpaqueNodeGetsOneChildLeafPerDrawRecord`,
  `RenderOpaqueNodeHasNoChildrenWhenNoDrawRecordsCaptured`,
  `RenderOpaqueDrawRecordLeafPassNameIsNeverLiterallyRenderOpaque`,
  `RenderOpaqueLeafGetsPreCompositeStepPreviewKind`,
  `PerObjectDrawRecordChildrenGetPerObjectStepKindAndIncreasingIndex`,
  `RenderTargetInfoIsRealAndNamedGameView`,
  `ViewProjectionMatrixRoundTripsWithoutTransposing`,
  `DistinctPipelineAndTextureNamesProduceDistinctEntriesNotDuplicates`,
  `MaterialTextureRowIsNeverARenderGraphResource`) are expected to remain
  unaffected, since `"RenderOpaque"`'s own shape is byte-for-byte untouched.

- **`tests/Editor/FrameDebuggerDataTests.cpp`** — checked directly: its one
  `BuildRealFrameDebuggerSnapshot()`-calling test
  (`EntityLeafShapeIsCorrect`) only ever exercises a graph with a single
  `"RenderOpaque"` pass plus one entity draw record, never a Compute-kind or
  other Graphics-kind pass — confirmed NOT broken by this phase (still
  compiles and passes unmodified). Listed here explicitly so PHASE3 knows it
  was checked and does not need touching.

- **`tests/Network/NetworkRoutesTests.cpp`** — checked directly: its
  `totalEventCount`-referencing test
  (`BuildFrameDebuggerStateResponseJsonTests.ProducesExactExpectedShape`)
  only ever manually constructs a synthetic `FrameDebuggerStateResponseView`
  with a hardcoded `totalEventCount = 5` — has nothing to do with
  `BuildRealFrameDebuggerSnapshot()`'s own real event-index arithmetic —
  confirmed NOT broken by this phase.

No `Blit`-drawKind-specific test exists yet anywhere (expected — `Blit`
remains a real-but-unused scaffold value per PHASE1; nothing in this engine
ever tags a real pass with it today).

## Verification

- `cmake --build build --target GreatTamanaEngine` (working directory
  `C:\Users\F5954\Documents\TAMANA\GreatTamanaEngine`) — **succeeded, zero
  errors, zero warnings** (6/6 build steps: `FrameDebuggerData.cpp` (this
  phase's own edited file), `FrameDebuggerHistory.cpp`,
  `Panels/FrameDebuggerPanel.cpp` (recompiled only because it's in the same
  translation unit group / depends on the touched header — its own source
  text is unmodified, confirmed via `git status`), `ImGuiEditorLayer.cpp`,
  then linking `libgte_core.a` and `GreatTamanaEngine.exe` successfully).
- Per PHASE0's Cross-Cutting Rules and this phase's own Step 3.8,
  `GreatTamanaEngineTests`/`ctest` was **deliberately NOT built or run** this
  phase — it is expected to fail to compile and/or fail assertions against
  the old flat shape until PHASE3 lands. This is a documented, accepted
  intermediate state, not an oversight.
- `git status` after all edits confirms exactly two files touched:
  `src/Editor/FrameDebuggerData.cpp` and `src/Editor/FrameDebuggerData.h` —
  `src/Editor/Panels/FrameDebuggerPanel.cpp` has zero changes, as required.

## Definition of Done — Status

- [x] `WrapPassWithOwnedChildEvent()` and `GraphicsChildEventLabelFor()`
      exist in `FrameDebuggerData.cpp`'s anonymous namespace.
- [x] All three call sites (`Compute LUT`/pre-view loop, post-view loop,
      view-region walk's `else` branch) wrap their leaf via
      `WrapPassWithOwnedChildEvent()`, each consuming exactly 2
      `nextEventIndex` values per surviving pass, in the correct
      parent-then-child order.
- [x] `"DrawSkyBackground"` now produces a node with exactly one child named
      `"Draw Quad"` (via `pass.drawKind == RenderPassDrawKind::DrawQuad`,
      PHASE1) — both the parent row and the child row are independently
      findable via `FindEventDetailsByIndex()` (which needed zero changes,
      per Step 2 point 5 — its recursive walk already visits `children`
      unconditionally) with correct, matching pass-level facts (both are
      built from the same source `details` copy before the child's own
      `name`/`eventIndex`/`eventLabel` are overwritten).
- [x] Every individual `"Compute LUT"` pass (and every other real compute
      dispatch) now produces a node with exactly one child named `"Compute
      Dispatch"`.
- [x] `"RenderOpaque"`'s own leaf + per-entity children are BYTE-FOR-BYTE
      unchanged from before this phase (re-confirmed by direct re-read,
      Step 3.6).
- [x] `src/Editor/Panels/FrameDebuggerPanel.cpp` has ZERO changes (confirmed
      via `git status`).
- [x] `FrameDebuggerData.h`'s tree-shape doc comment updated to match the
      new nested shape.
- [x] `cmake --build build --target GreatTamanaEngine` succeeds with zero
      errors/warnings.
- [x] `PHASE2_COMPLETION_REPORT.md` written (explicitly listing every test
      file this phase is known to have broken) and committed alongside the
      code.

## What Was NOT Done (by design, per this phase's own scope)

- `BuildRenderOpaqueLeaf()`, `BuildRenderOpaqueDrawRecordLeaf()`, and their
  one call site were not touched.
- No test file was fixed/updated in this phase — entirely PHASE3's job (see
  the list above).
- No full build or `ctest` regression run was performed — reserved for
  PHASE4.

## Next Step

PHASE3 (`PHASE3_TEST_SUITE_MIGRATION_AND_DOCS_UPDATE.md`) rewrites every
test named above that hardcodes the old flat shape/old
`eventIndex`/`totalEventCount` numbers to match this phase's new nested
shape, adds new tests for `WrapPassWithOwnedChildEvent()`/
`GraphicsChildEventLabelFor()`/dual-selectability, and updates
`docs/conventions/frame-debugger.md`/`AGENTS.md`.
