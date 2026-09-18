# PHASE3 COMPLETION REPORT — Test Suite Migration & Documentation Update

_Child of `PHASE0_MASTER_STRATEGY.md` (`task_manager/render-pass-2/`). This
phase's own strategy file: `PHASE3_TEST_SUITE_MIGRATION_AND_DOCS_UPDATE.md`._

## What Was Done

Implemented the plan exactly as written. `src/` was NOT touched anywhere in
this phase (tests + docs only, per this phase's own scope and PHASE0's
Cross-Cutting Rules).

### 1. Independently re-verified all 31 pre-existing tests in `tests/Editor/FrameDebuggerSnapshotBuilderTests.cpp`

Every test's CURRENT body (as it stood after PHASE1+PHASE2, before this
phase's own edits) was directly re-read and hand-simulated against Step 2.1's
rules — not just taken on the phase file's own table. My own finding matched
the phase file's own classification in every single case (all previously
marked `VERIFY` rows had already been resolved to a definitive
`NEEDS UPDATE`/`CONFIRMED UNCHANGED` by the phase file's own "independent deep
review" pass, and my own re-derivation confirmed every one of them):

| # | Test name | My own verified finding | Change made |
|---|---|---|---|
| 1 | `NoRenderOpaquePassProducesEmptyResult` | UNCHANGED — no `"RenderOpaque"` pass, early-return empty | none |
| 2 | `RenderOpaqueWithNoComputePassesProducesExactlyOneLeaf` | UNCHANGED — `"SceneView"` + `"RenderOpaque"` only, zero compute passes | none |
| 3 | `MixedComputeLutAndPreGameViewCategoriesProduceBothGroupsInFixedOrder` | **NEEDS UPDATE** — 2 compute passes, both now wrapped | totalEventCount 3→5; `lutGroup.children[0].eventIndex` 1→2 + new child(3); `preGroup.children[0]` + new child(1); `renderOpaqueLeaf.eventIndex` 2→4 |
| 4 | `OnlyAtmosphereLutCategoryPreGameViewPassProducesOnlyComputeLutGroup` | UNCHANGED as written (only checks `root.children.size()`/names) | none (the optional extra coverage is now covered by new test `ComputeLutSubPassAlsoOwnsAComputeDispatchChild`) |
| 5 | `NonComputePassIsNeverTreatedAsComputeDispatch` | UNCHANGED — extra pass is Graphics-kind, before pivot, never wrapped | none |
| 6 | `DistinctPipelineAndTextureNamesProduceDistinctEntriesNotDuplicates` | UNCHANGED — `"RenderOpaque"`-only | none |
| 7 | `ViewProjectionMatrixRoundTripsWithoutTransposing` | UNCHANGED — `"RenderOpaque"`-only | none |
| 8 | `RenderTargetInfoIsRealAndNamedGameView` | UNCHANGED — `"RenderOpaque"`-only | none |
| 9 | `PostGameViewComputePassProducesLeafUnderPostGameViewGroup` | **NEEDS UPDATE** — 1 compute pass, now wrapped | totalEventCount 2→3; added child-node assertions (`name`/`eventIndex`/`eventLabel`/`passName`) |
| 10 | `NoComputePassesProduceNeitherGroup` | UNCHANGED — `"RenderOpaque"`-only | none |
| 11 | `PreAndPostGameViewGroupsAppearTogetherAsSiblingsInRealExecutionOrder` | **NEEDS UPDATE** — 2 compute passes, both wrapped | `renderOpaqueLeaf.eventIndex` 1→2; `postGroup.children[0].eventIndex` 2→3; totalEventCount 3→5; added both children's assertions |
| 12 | `SceneViewScopedPreGameViewPassIsExcludedEvenWhenNameCollidesWithGameViewOne` | **NEEDS UPDATE** — surviving GameView-scoped pass now wrapped | totalEventCount 2→3; added child assertion |
| 13 | `SceneViewScopedPostGameViewPassIsExcludedEvenWhenNameCollidesWithGameViewOne` | **NEEDS UPDATE** — same as #12, post-side | totalEventCount 2→3; added child assertion |
| 14 | `SharedViewScopedPassStillAppearsNormally` | **NEEDS UPDATE** — surviving Shared-scoped pass now wrapped | totalEventCount 2→3; added child assertion |
| 15 | `RenderOpaqueNodeGetsOneChildLeafPerDrawRecord` | UNCHANGED — `"RenderOpaque"`'s own untouched per-entity mechanism | none |
| 16 | `EventIndexIsMonotonicAcrossPreGameViewRenderOpaqueChildrenAndPostGameView` | **NEEDS UPDATE** — PreA/PostA both wrapped | every index from `renderOpaqueLeaf` onward shifted (2→2 stays but children/post shift); totalEventCount 5→7; added both compute children's assertions |
| 17 | `RenderOpaqueNodeHasNoChildrenWhenNoDrawRecordsCaptured` | UNCHANGED — `"RenderOpaque"`-only, explicitly re-confirmed as the ONE remaining real "childless node" case (Step 3.5's gap note) | none |
| 18 | `RenderOpaqueDrawRecordLeafPassNameIsNeverLiterallyRenderOpaque` | UNCHANGED — `"RenderOpaque"`-only | none |
| 19 | `CulledComputePassIsExcludedFromEitherComputeDispatchGroup` | **NEEDS UPDATE** — 2 real surviving compute passes, both wrapped | totalEventCount 3→5; added both children's assertions; culled passes confirmed to still consume zero indices |
| 20 | `ComputeDispatchLeafLabelsReadWriteRowsByRealResourceKind` | CONFIRMED UNCHANGED — only reads `.details->textures` on the pass-level node | none |
| 21 | `EventIndexIsMonotonicAcrossPreGameViewGameViewAndPostGameView` | **NEEDS UPDATE** — 4 compute passes, all wrapped | every index from `preGroup.children[1]` onward shifted; totalEventCount 5→9; added all 4 children's assertions |
| 22 | `PreGameViewLeafGetsNotYetDrawnStepPreviewKind` | CONFIRMED UNCHANGED — only reads `.details->stepPreviewKind` on the pass-level node | none |
| 23 | `RenderOpaqueLeafGetsPreCompositeStepPreviewKind` | UNCHANGED — `"RenderOpaque"`-only | none |
| 24 | `PerObjectDrawRecordChildrenGetPerObjectStepKindAndIncreasingIndex` | UNCHANGED — `"RenderOpaque"`'s own per-entity children only | none |
| 25 | `PostGameViewLeafBeforeCompositePassGetsPreCompositeStepPreviewKind` | CONFIRMED UNCHANGED — only a GROUP sibling count + pass-level `.details->stepPreviewKind` | none |
| 26 | `CompositePassLeafItselfGetsPostCompositeStepPreviewKind` | CONFIRMED UNCHANGED — same as #25 | none |
| 27 | `PostGameViewLeavesDefaultToPostCompositeWhenNoCompositePassSurvives` | CONFIRMED UNCHANGED — same as #25 | none |
| 28 | `DrawSkyBackgroundLeafSurvivesAlongsideComputeDispatchSplit` | **NEEDS UPDATE** — full rewrite (PHASE0's own named example) | fixture now explicitly tags `.drawKind = DrawQuad`; `skyLeaf` now asserts `children.size()==1u` (was `children.empty()`) with a new `skyChild` (`"Draw Quad"`); every eventIndex from `renderOpaqueLeaf` onward re-derived; totalEventCount 5→8 |
| 29 | `ViewRegionHasExactlyRenderOpaqueAndDrawSkyBackgroundWhenRenderTransparentAbsent` | Minor addition only (PHASE0's other named example) | fixture now explicitly tags `.drawKind = DrawQuad`; added 2 new assertions proving `"DrawSkyBackground"` owns exactly one child named `"Draw Quad"` |
| 30 | `DebugCategoryGraphicsPassInsideViewRegionProducesNoExtraLeaf` | **NEEDS UPDATE** — full rewrite | fixture now explicitly tags `.drawKind = DrawQuad`; eventIndex/totalEventCount re-derived (3→5) with new child assertions for both the sky pass and the composite pass |
| 31 | `MaterialTextureRowIsNeverARenderGraphResource` | UNCHANGED — about a `"Material Texture"` row on `"RenderOpaque"`'s own per-entity children | none |

**12 of 31 tests needed a real update** (#3, #9, #11, #12, #13, #14, #16,
#19, #21, #28, #29, #30); **19 of 31 were confirmed genuinely unaffected**
and left byte-for-byte unchanged (only a short "CONFIRMED UNCHANGED" comment
was added above several of them, documenting that they were explicitly
checked, not overlooked).

**Fixture-construction gotcha confirmed and fixed at all 3 real call
sites** (`DrawSkyBackgroundLeafSurvivesAlongsideComputeDispatchSplit`,
`ViewRegionHasExactlyRenderOpaqueAndDrawSkyBackgroundWhenRenderTransparentAbsent`,
`DebugCategoryGraphicsPassInsideViewRegionProducesNoExtraLeaf`): `MakePass()`
never infers `drawKind` from a pass's own name, so every hand-built
`"DrawSkyBackground"` fixture in this file now explicitly sets
`.drawKind = rg::RenderPassDrawKind::DrawQuad;` before pushing it — without
this, `GraphicsChildEventLabelFor()` would have produced `"Draw Mesh"` (the
un-tagged struct default), not `"Draw Quad"`, and the new/updated assertions
would have failed.

### 2. New tests added (Step 3.5) — 4 tests, not 3

- **`WrappedPassLevelNodeAndItsChildAreBothIndependentlySelectable`** —
  EXTENDED per the phase file's own "gap found during review" note: proves
  dual-selectability, via the real `FindEventDetailsByIndex()` lookup
  function, for BOTH a wrapped GRAPHICS-kind pass (`"DrawSkyBackground"`,
  parent + `"Draw Quad"` child) AND a wrapped COMPUTE-kind pass
  (`"SomeComputePass"`, parent + `"Compute Dispatch"` child) in the same
  fixture — the one remaining un-exercised combination this campaign's fix
  touches.
- **`GraphicsChildEventLabelMatchesRenderPassDrawKind`** — three synthetic
  fixtures (`"SyntheticQuadPass"`/`"SyntheticMeshPass"`/`"SyntheticBlitPass"`),
  each tagged a different `rg::RenderPassDrawKind` directly on the fixture,
  asserting the owned child's name matches exactly (`"Draw Quad"`/`"Draw
  Mesh"`/`"Blit"`) — the one test in this whole campaign that exercises the
  `Blit` scaffold value end-to-end.
- **`ComputeLutSubPassAlsoOwnsAComputeDispatchChild`** — a dedicated new
  test (rather than folding into test #4, per that step's own "implementer's
  choice" note) extending `OnlyAtmosphereLutCategoryPreGameViewPassProducesOnlyComputeLutGroup`'s
  own fixture with the assertion that a `"Compute LUT"` sub-pass also owns a
  real `"Compute Dispatch"` child.

All 4 pass. The "hinted edge case already covered" note (a pass with no
children at all today) was explicitly re-checked: `RenderOpaqueNodeHasNoChildrenWhenNoDrawRecordsCaptured`
(test #17) already proves this is the ONE remaining real "childless node"
case after this whole campaign — confirmed, not overlooked, no new test
needed for it.

### 3. Confirmed-unaffected files re-checked directly (not just trusted from PHASE2's report)

- `tests/Editor/FrameDebuggerDataTests.cpp` — `EntityLeafShapeIsCorrect`
  (the one test there that calls `BuildRealFrameDebuggerSnapshot()`) uses a
  fixture with only a plain `"RenderOpaque"` pass and one entity draw record —
  no compute pass, no `"DrawSkyBackground"` — re-read directly, confirmed
  unaffected. Left untouched.
- `tests/Editor/FrameDebuggerCaptureTests.cpp` — grepped for
  `"DrawSkyBackground"`: only 2 matches, both historical comments, no actual
  assertion. Confirmed unaffected. Left untouched.
- `tests/Renderer/RenderGraph/RenderPassTests.cpp` — grepped for
  `"DrawSkyBackground"`: the one fixture there is PHASE1's own territory
  (`AddRenderPass()`-level integration test for the new `drawKind` parameter),
  already landed and already passing — confirmed still compiling/passing as
  part of this phase's own scoped test run. Not redesigned.
- `tests/Network/NetworkRoutesTests.cpp` — grepped for `totalEventCount`:
  the one hit (`BuildFrameDebuggerStateResponseJsonTests.ProducesExactExpectedShape`)
  manually constructs a synthetic `FrameDebuggerStateResponseView` with a
  hardcoded `totalEventCount = 5`, unrelated to `BuildRealFrameDebuggerSnapshot()`'s
  own arithmetic — confirmed unaffected, re-run as part of the scoped test
  pass anyway (green).

### 4. Documentation updated (Step 3.6)

- **`docs/conventions/frame-debugger.md`** — added a new
  `## What's new (\`render-pass-2\` campaign)` section (right after the
  existing `render-pass-1` section, before `## What is real today`)
  describing the new `rg::RenderPassDrawKind` vocabulary,
  `WrapPassWithOwnedChildEvent()`/`GraphicsChildEventLabelFor()`, the new
  doubled `eventIndex` consumption per wrapped pass, and the original bug
  report this campaign fixes. The main ASCII tree diagram under `## What is
  real today` was rewritten to show every pass-level leaf (Compute LUT
  sub-passes, Pre/Post-GameView compute dispatches, `"DrawSkyBackground"`, a
  future real `"RenderTransparent"`) as a nested `v PassName -> child event`
  shape instead of a flat bullet, mirroring PHASE0_MASTER_STRATEGY.md's own
  target-shape diagram. Every OTHER mention of `"DrawSkyBackground"`
  elsewhere in the file was re-read in context (per this phase's own
  instruction to check context, not just line numbers) — the remaining
  mentions are either historical campaign write-ups (kept as accurate
  PAST-tense history, per this file's own established convention) or
  describe pass-level pipeline-state facts (blend/Z/stencil) that are
  genuinely unaffected by this campaign — none of those needed a change.
- **`AGENTS.md`** — the "Frame Debugger" section gained a new sentence
  describing the "v PassName -> child event" ownership rule and linking to
  the `render-pass-2` campaign; the "Render Pass System" section gained a
  new paragraph describing `rg::RenderPassDrawKind` (`DrawMesh`/`DrawQuad`/
  `Blit`), its defaulted trailing-parameter mechanism, and its role in the
  Frame Debugger's child-event labeling — and its own "Full history" line now
  also references `task_manager/render-pass-2/PHASE0_MASTER_STRATEGY.md`.
- **The root `README.md`'s `## Status` section was deliberately NOT
  touched**, per this phase's own explicit instruction — that summary bullet
  is intentionally deferred to PHASE4, which will have the final
  build/regression/live-verification numbers to quote.

## Deviations From The Plan

None of substance. One minor implementer choice: Step 3.5's third new test
(`ComputeLutSubPassAlsoOwnsAComputeDispatchChild`) was added as its own
dedicated `TEST(...)` rather than folded into test #4
(`OnlyAtmosphereLutCategoryPreGameViewPassProducesOnlyComputeLutGroup`) — the
phase file's own Step 3.5 explicitly left this as "implementer's choice, just
make sure this exact fact is covered somewhere," so this is not a deviation,
just the choice actually taken. No design ambiguity was hit that the phase
file (or PHASE0) did not already resolve, so `ask_questions` was not needed
for this phase.

## Verification

- `cmake --build build --target GreatTamanaEngineTests` (working directory
  `C:\Users\F5954\Documents\TAMANA\GreatTamanaEngine`) — **succeeded, zero
  errors** (3 files recompiled: `FrameDebuggerHistoryTests.cpp`,
  `FrameDebuggerDataTests.cpp`, `FrameDebuggerSnapshotBuilderTests.cpp`, then
  linked `GreatTamanaEngineTests.exe` successfully).
- Scoped/filtered test run (NOT the full `ctest` suite — reserved for
  PHASE4), run directly against the built executable:

  ```
  cd /d C:\Users\F5954\Documents\TAMANA\GreatTamanaEngine\build
  tests\GreatTamanaEngineTests.exe --gtest_filter=FrameDebuggerSnapshotBuilderTest.*:FrameDebuggerDataTest.*:FrameDebuggerCaptureTest*.*:RenderGraphRenderPassDrawKindTest.*:RenderGraphSnapshotTest.*:RenderPassTest.*:NetworkRoutesTests*.*:BuildFrameDebuggerStateResponseJsonTests.*
  ```

  Result: **102 tests from 10 test suites ran, 102 PASSED, 0 failed** — 34
  `FrameDebuggerSnapshotBuilderTest` (the 31 pre-existing tests + 3 net-new
  dedicated `TEST(...)` cases = 34; Step 3.5's own "3 new tests" is satisfied
  by adding exactly 3 new `TEST(...)` blocks, with the dual-selectability
  extension for a wrapped COMPUTE-kind pass folded into test 1's own body
  rather than becoming a 4th separate `TEST(...)`), 15 `FrameDebuggerDataTest`,
  21 `RenderGraphSnapshotTest`, 9 `RenderPassTest`, 2
  `RenderGraphRenderPassDrawKindTest` (PHASE1's own new tests, re-confirmed
  passing), 3 `NetworkRoutesTests` + 1 `BuildFrameDebuggerStateResponseJsonTests`
  + 9 `NetworkRoutesTests/ResolveCaptureResponseFormatTest` + 3
  `NetworkRoutesTests/ParseGetTextureQueryInvalidChannelTest` + 5
  `NetworkRoutesTests/ParseFrameDebuggerSetChannelQueryValidTest` (all the
  `NetworkRoutesTests*` sub-suites the filter's wildcard also picked up,
  100% green).
- `git status` after all edits confirms exactly the intended 3 files touched:
  `AGENTS.md`, `docs/conventions/frame-debugger.md`,
  `tests/Editor/FrameDebuggerSnapshotBuilderTests.cpp` — no file under `src/`
  was touched, and the root `README.md` was not touched.

## Definition of Done — Status

- [x] Every one of the 31 existing tests in `FrameDebuggerSnapshotBuilderTests.cpp`
      verified against Step 2.1's rules — updated where needed (12), confirmed
      unchanged where not (19), with my own real, independently re-verified
      finding recorded in the table above (not just the phase file's own
      table copied verbatim).
- [x] The 3 new tests from Step 3.5 added and passing (plus the additional
      gap it flags, extending test 1 to also cover a wrapped COMPUTE-kind
      pass's dual-selectability, folded into that same test's body).
- [x] `docs/conventions/frame-debugger.md` and `AGENTS.md` updated to describe
      the new nested tree shape and `RenderPassDrawKind` vocabulary.
- [x] `cmake --build build --target GreatTamanaEngineTests` succeeds with
      zero errors.
- [x] The scoped/filtered test run (Step 3.7) is 100% green (102/102).
- [x] `PHASE3_COMPLETION_REPORT.md` written and committed alongside the
      code/tests/docs.

## What Was NOT Done (by design, per this phase's own scope)

- No production code under `src/` was touched anywhere in this phase.
- No brand-new `.cpp` test file was created — every change landed in the
  existing `tests/Editor/FrameDebuggerSnapshotBuilderTests.cpp`.
- The full `ctest` regression suite (~1578+ tests) was NOT run — only the
  scoped/filtered subset this campaign actually touches, per this phase's own
  explicit instruction. Full regression is PHASE4's job.
- The root `README.md`'s `## Status` section was NOT touched — intentionally
  deferred to PHASE4.

## Next Step

PHASE4 (`PHASE4_FINAL_INTEGRATION_FULL_BUILD_AND_LIVE_VERIFICATION.md`) runs
the full build (`GreatTamanaEngine` + `GreatTamanaEngineTests`), the full
`ctest` regression suite, a live, HTTP-driven, screenshot-verified Frame
Debugger check against the real running engine (confirming the target tree
shape from PHASE0's own Step 1), writes the campaign completion report, and
is the ONLY phase that adds this campaign's own summary bullet to the root
`README.md`'s `## Status` section.
