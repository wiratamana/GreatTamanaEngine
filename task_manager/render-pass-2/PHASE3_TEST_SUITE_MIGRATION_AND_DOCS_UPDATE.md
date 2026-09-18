# PHASE3 — Test Suite Migration & Documentation Update

_Child of `PHASE0_MASTER_STRATEGY.md` (`task_manager/render-pass-2/`). Read
that file first. Assumes PHASE1 + PHASE2 have already landed — read both
`PHASE1_COMPLETION_REPORT.md` and, especially,
`PHASE2_COMPLETION_REPORT.md`'s own list of test files it flagged as broken,
first._

## Step 1: The Goal (Where are we going?)

Bring `GreatTamanaEngineTests` back to fully compiling and 100% passing
against the NEW nested "pass owns one child event" tree shape PHASE2
shipped, by rewriting every existing test assertion that hardcodes the OLD
flat shape or an OLD `eventIndex`/`totalEventCount` number, and adding new
Tier-1 coverage for the new mechanism itself. Also bring
`docs/conventions/frame-debugger.md` and `AGENTS.md` back in sync with the
real, shipped tree shape. This phase does **NOT** run the full `ctest`
regression suite (that is PHASE4's job) — it runs a SCOPED/FILTERED
execution of just the affected test binaries to iterate and confirm
correctness of the tests THIS phase touches.

## Step 2: The Situation (Where are we now?)

A full audit (`search_in_dir`, this campaign's own PHASE0 investigation)
found the following files with actual, real hardcoded-shape assertions:

- `tests/Editor/FrameDebuggerSnapshotBuilderTests.cpp` — 31 `TEST(...)`
  cases, the primary surface this phase touches. See Step 3.3's own
  per-test checklist below.
- `tests/Editor/FrameDebuggerDataTests.cpp` — one test,
  `EntityLeafShapeIsCorrect` (line 331), is `"RenderOpaque"`-only (no
  compute pass, no `"DrawSkyBackground"`) — confirmed UNAFFECTED by this
  campaign; no change expected, but re-run it anyway as part of this
  phase's scoped test pass to be sure.
- `tests/Editor/FrameDebuggerCaptureTests.cpp` — only carries HISTORICAL
  comments referencing `"DrawSkyBackground"` (lines 196, 201); no actual
  test asserts anything about it. Confirmed UNAFFECTED; skim it once to be
  sure nothing was missed.
- `tests/Renderer/RenderGraph/RenderPassTests.cpp` — has a
  `"DrawSkyBackground"` fixture at line 88 (a `RenderGraphBuilder`-level
  integration test, NOT a `FrameDebuggerData.cpp` test) — this is
  PHASE1's own territory (confirm PHASE1 already added/updated whatever
  this test needed for the new `drawKind` parameter; this phase only needs
  to confirm it still compiles/passes, not redesign it).
- PHASE1's own new tests (`RenderGraphTypesTests.cpp`,
  `RenderGraphSnapshotTests.cpp`, `RenderPassTests.cpp`) — already landed;
  just confirm they still pass in this phase's own scoped test run.

### 2.1 The exact mechanism that changed (re-read this before touching any assertion)

`WrapPassWithOwnedChildEvent()` (PHASE2) takes an ALREADY-FULLY-BUILT
pass-level node and ONLY (a) appends exactly one new child to it, and (b)
consumes exactly one MORE `nextEventIndex` value for that child. It never
mutates the pass-level node's own pre-existing `name`/`eventIndex`/`details`
fields. This means:

- Any assertion that reads ONLY `.name`, or ONLY non-index fields of
  `.details` (`blendMode`, `zTest`, `shaderName`, `textures`, `vectors`,
  `matrices`, `stepPreviewKind`, `passName`) on a PASS-LEVEL node is
  **completely unaffected** — no change needed.
- Any assertion of a GROUP node's own `.children.size()` (e.g.
  `preGroup.children.size()`, `postGroup.children.size()`,
  `root.children.size()`) counts SIBLING PASSES, never grandchildren — this
  number is **unaffected** by this whole campaign in every single existing
  test (a group's own pass-child count never changes; only each individual
  pass-child may now itself additionally have one child of its own).
- Any assertion of the form `<wrapped-pass-node>.children.empty()` or
  `.children.size() == 0u` on a Compute-kind pass, OR a Graphics-kind pass
  other than `"RenderOpaque"` in the view region (today, only
  `"DrawSkyBackground"`), is now WRONG and must become `.children.size() ==
  1u`, with new companion assertions for that one child's own `.name` /
  `.details->eventLabel` (`"Compute Dispatch"`, or
  `GraphicsChildEventLabelFor(pass.drawKind)`'s result) / `.eventIndex`.
- Any assertion of an exact `snapshot.totalEventCount` number is now WRONG
  by exactly `+N`, where `N` = the count of passes in that SPECIFIC
  fixture that get wrapped this campaign (every surviving Compute-kind
  pass anywhere in the fixture, PLUS every surviving Graphics-kind,
  non-`"RenderOpaque"` pass in the view region).
- Any assertion of an exact `eventIndex` number for a node whose real
  construction happens AFTER at least one wrapped pass (in
  `BuildRealFrameDebuggerSnapshot()`'s own real processing order — pre-view
  loop, then view-region walk incl. `"RenderOpaque"`'s own per-entity
  children, then post-view loop) is now WRONG. **Do not try to shortcut
  this arithmetic with a formula** — always re-derive the exact new number
  by hand-simulating the function's real per-pass processing order for
  THAT SPECIFIC fixture, incrementing `nextEventIndex` exactly ONCE for an
  un-wrapped node (`"RenderOpaque"` itself, or one of its own real
  per-entity draw-record children) and exactly TWICE, back-to-back
  (parent, then its one new child), for every wrapped pass — Step 3.1/3.2
  below give three fully worked examples of exactly this simulation; follow
the same method for every other test.

- **A separate, non-arithmetic gotcha found during this review:**
  `GraphicsChildEventLabelFor(pass.drawKind)` (PHASE2) reads
  `RenderGraphPassSnapshot::drawKind` PURELY STRUCTURALLY, never `pass.name`
  (Locked Design Decision #3). This file's own `MakePass()`/`MakeComputePass()`
  helpers do NOT set `drawKind` — it stays at its struct default,
  `RenderPassDrawKind::DrawMesh` — so a hand-built `MakePass("DrawSkyBackground")`
  fixture is NOT automatically `DrawQuad` just because of its name (unlike the
  real production call site, `AddDrawSkyBackgroundPass()`, explicitly tagged by
  PHASE1). Every existing fixture in this file that builds its own
  `"DrawSkyBackground"` pass and expects its owned child to read `"Draw Quad"`
  (tests #28/#29/#30 below) MUST be updated to explicitly set `.drawKind =
  rg::RenderPassDrawKind::DrawQuad;` on that pass object before pushing it —
  see Step 3.1/3.2/3.3 below for the exact fixture-construction fix at each
  real call site.

## Step 3: The Plan

### 3.1 Fully worked example #1 — `DrawSkyBackgroundLeafSurvivesAlongsideComputeDispatchSplit` (line 975)

Fixture: `PreA` (compute) → `RenderOpaque` (pivot, one entity draw record
`"terrain"`) → `DrawSkyBackground` (graphics) → `PostA` (compute).

**Fixture-construction fix required (found during this review):** the real
file's line 980 constructs the `"DrawSkyBackground"` pass via plain
`MakePass("DrawSkyBackground")`, which leaves `drawKind` at its struct
default (`RenderPassDrawKind::DrawMesh`) — `GraphicsChildEventLabelFor()`
(PHASE2) derives the child's label PURELY from `pass.drawKind`, NEVER from
`pass.name` (PHASE0's Locked Design Decision #3), so this fixture MUST be
changed to explicitly tag it, mirroring `AddDrawSkyBackgroundPass()`'s own
real PHASE1 tagging:

```cpp
    rg::RenderGraphPassSnapshot skyPass = MakePass("DrawSkyBackground");
    skyPass.drawKind = rg::RenderPassDrawKind::DrawQuad;
    graphSnapshot.passesInExecutionOrder.push_back(skyPass); // Replaces the old plain MakePass(...) push (line 980).
```

Without this fixture change, `skyChild.name` in the rewrite below would
actually be `"Draw Mesh"`, not `"Draw Quad"`, and the test would FAIL. The
exact same fix is needed for tests #29/#30 (Step 3.2/3.3 below) — every one
of the three tests that builds its own `"DrawSkyBackground"` fixture from
scratch needs it.

Hand-simulated new processing order and index consumption:

| Step | Node | Old `eventIndex` | New `eventIndex` |
|---|---|---|---|
| pre-view: `PreA` parent | `preGroup.children[0]` | 0 | 0 |
| pre-view: `PreA` child `"Compute Dispatch"` | NEW | — | 1 |
| view-region: `RenderOpaque` parent | `renderOpaqueLeaf` | 1 | 2 |
| view-region: `RenderOpaque`'s own entity child `"terrain (Entity 2)"` | `entityLeaf` | 2 | 3 |
| view-region: `DrawSkyBackground` parent | `skyLeaf` | 3 | 4 |
| view-region: `DrawSkyBackground` child `"Draw Quad"` | NEW | — | 5 |
| post-view: `PostA` parent | `postGroup.children[0]` | 4 | 6 |
| post-view: `PostA` child `"Compute Dispatch"` | NEW | — | 7 |
| `totalEventCount` | | 5 | **8** |

Required rewrite (replace lines 1005-1025 with):

```cpp
    EXPECT_EQ(skyLeaf.name, "DrawSkyBackground");
    EXPECT_TRUE(skyLeaf.isDrawCall);
    ASSERT_EQ(skyLeaf.children.size(), 1u); // NOW owns exactly one real child event.
    ASSERT_TRUE(skyLeaf.details.has_value());
    EXPECT_EQ(skyLeaf.details->passName, "DrawSkyBackground");
    EXPECT_EQ(skyLeaf.details->shaderName, "DrawSkyBackground");
    EXPECT_EQ(skyLeaf.details->zTest, "Equal");
    EXPECT_EQ(skyLeaf.details->zWrite, "Off");
    EXPECT_EQ(skyLeaf.details->stepPreviewKind, FrameDebuggerStepPreviewKind::PreComposite);

    const FrameDebuggerEventNode& skyChild = skyLeaf.children[0];
    EXPECT_EQ(skyChild.name, "Draw Quad"); // DrawSkyBackground is tagged RenderPassDrawKind::DrawQuad (PHASE1).
    ASSERT_TRUE(skyChild.details.has_value());
    EXPECT_EQ(skyChild.details->eventLabel, "Draw Quad");
    EXPECT_EQ(skyChild.details->passName, "DrawSkyBackground"); // Same owning pass.

    EXPECT_EQ(postGroup.name, "Compute Dispatches (Post-GameView)");
    ASSERT_EQ(postGroup.children.size(), 1u);
    ASSERT_EQ(postGroup.children[0].children.size(), 1u);
    EXPECT_EQ(postGroup.children[0].children[0].name, "Compute Dispatch");

    ASSERT_EQ(preGroup.children[0].children.size(), 1u);
    EXPECT_EQ(preGroup.children[0].children[0].name, "Compute Dispatch");

    // eventIndex strictly monotonic, now 8 total leaves (each wrapped pass
    // contributes one parent + one child).
    EXPECT_EQ(preGroup.children[0].eventIndex, 0);
    EXPECT_EQ(preGroup.children[0].children[0].eventIndex, 1);
    EXPECT_EQ(renderOpaqueLeaf.eventIndex, 2);
    EXPECT_EQ(entityLeaf.eventIndex, 3);
    EXPECT_EQ(skyLeaf.eventIndex, 4);
    EXPECT_EQ(skyChild.eventIndex, 5);
    EXPECT_EQ(postGroup.children[0].eventIndex, 6);
    EXPECT_EQ(postGroup.children[0].children[0].eventIndex, 7);
    EXPECT_EQ(snapshot.totalEventCount, 8);
```

Also update line 1001 (`ASSERT_EQ(renderOpaqueLeaf.children.size(), 1u);`
comment) — no numeric change needed there, `"RenderOpaque"` itself is
unaffected, just double-check the comment still reads correctly.

### 3.2 Fully worked example #2 — `DebugCategoryGraphicsPassInsideViewRegionProducesNoExtraLeaf` (line 1059)

Fixture: `RenderOpaque` (pivot, no entity records) → `DrawSkyBackground` →
two `Debug`-category Graphics passes (skipped, never consume an index) →
`AtmosphereAerialPerspectiveCompositePass` (compute).

**Fixture-construction fix required (found during this review — same gotcha
as Step 3.1's own note above):** the real file's line 1063 also constructs
`"DrawSkyBackground"` via plain `MakePass("DrawSkyBackground")` — update it
identically to explicitly set `.drawKind = rg::RenderPassDrawKind::DrawQuad;`
before pushing, otherwise the `"Draw Quad"` assertion in the rewrite below
would actually see `"Draw Mesh"` (the un-tagged default).

| Step | Node | Old `eventIndex` | New `eventIndex` |
|---|---|---|---|
| `RenderOpaque` parent | `root.children[0]` | 0 | 0 |
| `DrawSkyBackground` parent | `root.children[1]` | 1 | 1 |
| `DrawSkyBackground` child `"Draw Quad"` | NEW | — | 2 |
| composite parent | `postGroup.children[0]` | 2 | 3 |
| composite child `"Compute Dispatch"` | NEW | — | 4 |
| `totalEventCount` | | 3 | **5** |

Required rewrite (replace lines 1090-1096 with):

```cpp
    // eventIndex is monotonic and SKIPS the two Debug-category passes
    // entirely (they never get an eventIndex at all): RenderOpaque(0),
    // DrawSkyBackground(1) + its own child "Draw Quad"(2), composite(3) +
    // its own child "Compute Dispatch"(4).
    EXPECT_EQ(root.children[0].eventIndex, 0);
    EXPECT_EQ(root.children[1].eventIndex, 1);
    ASSERT_EQ(root.children[1].children.size(), 1u);
    EXPECT_EQ(root.children[1].children[0].eventIndex, 2);
    EXPECT_EQ(root.children[1].children[0].name, "Draw Quad");
    EXPECT_EQ(postGroup.children[0].eventIndex, 3);
    ASSERT_EQ(postGroup.children[0].children.size(), 1u);
    EXPECT_EQ(postGroup.children[0].children[0].eventIndex, 4);
    EXPECT_EQ(snapshot.totalEventCount, 5);
```

### 3.3 Fully worked example #3 — `ViewRegionHasExactlyRenderOpaqueAndDrawSkyBackgroundWhenRenderTransparentAbsent` (line 1034)

This test asserts NO exact `eventIndex`/`totalEventCount` at all — only
`root.children.size() == 2u` and both names. That assertion is completely
**unaffected** (still exactly 2 top-level siblings: `"RenderOpaque"` +
`"DrawSkyBackground"` — the new child is nested INSIDE
`"DrawSkyBackground"`, not a new sibling). Add exactly one new assertion at
the end proving the fix itself — but ONLY once the fixture's own
`"DrawSkyBackground"` pass (line 1038 of the real file) is ALSO updated to
explicitly set `.drawKind = rg::RenderPassDrawKind::DrawQuad;` before pushing
it (see Step 3.1's own "Fixture-construction fix required" note above —
`MakePass()` never infers `drawKind` from a pass's name, so without this the
new assertion below would see `"Draw Mesh"`, not `"Draw Quad"`):

```cpp
    rg::RenderGraphPassSnapshot skyPass = MakePass("DrawSkyBackground");
    skyPass.drawKind = rg::RenderPassDrawKind::DrawQuad;
    graphSnapshot.passesInExecutionOrder.push_back(skyPass); // Replaces the old plain MakePass(...) push.
```

Then add exactly one new assertion at the end of the test body proving the
fix itself:

```cpp
    ASSERT_EQ(root.children[1].children.size(), 1u);
    EXPECT_EQ(root.children[1].children[0].name, "Draw Quad");
```

### 3.4 Per-test checklist — `tests/Editor/FrameDebuggerSnapshotBuilderTests.cpp`

Go through EVERY one of the 31 `TEST(FrameDebuggerSnapshotBuilderTest, ...)`
cases below, in file order. For each, (a) re-read its current body, (b)
apply Step 2.1's rules to decide whether it needs a change, (c) if it does,
follow the exact hand-simulation method Step 3.1/3.2 demonstrate. The
"expected" column below is this campaign's own best-effort classification
from directly reading most of these bodies during PHASE0's investigation —
**verify it yourself against the real, current file before trusting it**,
since PHASE1/PHASE2 may have shifted line numbers slightly:

| # | Test name | Expected |
|---|---|---|
| 1 | `NoRenderOpaquePassProducesEmptyResult` | UNCHANGED (no `"RenderOpaque"` pass at all → early-return empty result, nothing to wrap) |
| 2 | `RenderOpaqueWithNoComputePassesProducesExactlyOneLeaf` | UNCHANGED (verified: only `"SceneView"` + `"RenderOpaque"`, zero compute passes, no `"DrawSkyBackground"`) |
| 3 | `MixedComputeLutAndPreGameViewCategoriesProduceBothGroupsInFixedOrder` | **NEEDS UPDATE** (verified: 2 compute passes) — hand-simulate per Step 3.1's method |
| 4 | `OnlyAtmosphereLutCategoryPreGameViewPassProducesOnlyComputeLutGroup` | UNCHANGED as currently written (verified: only checks `root.children.size()`/names, no index/count-on-wrapped-node assertions) — OPTIONAL: add an assertion that `lutGroup.children[0].children.size() == 1u` for extra coverage |
| 5 | `NonComputePassIsNeverTreatedAsComputeDispatch` | UNCHANGED (verified: the extra pass is Graphics-kind, never enters a compute loop at all, zero compute passes exist) |
| 6 | `DistinctPipelineAndTextureNamesProduceDistinctEntriesNotDuplicates` | UNCHANGED (`"RenderOpaque"`-only fixture) |
| 7 | `ViewProjectionMatrixRoundTripsWithoutTransposing` | UNCHANGED (`"RenderOpaque"`-only fixture) |
| 8 | `RenderTargetInfoIsRealAndNamedGameView` | UNCHANGED (`"RenderOpaque"`-only fixture) |
| 9 | `PostGameViewComputePassProducesLeafUnderPostGameViewGroup` | **NEEDS UPDATE** (verified: 1 compute pass; `totalEventCount` 2→3; add child assertions) |
| 10 | `NoComputePassesProduceNeitherGroup` | UNCHANGED (verified: `"RenderOpaque"`-only) |
| 11 | `PreAndPostGameViewGroupsAppearTogetherAsSiblingsInRealExecutionOrder` | **NEEDS UPDATE** (verified: 2 compute passes, one each side) |
| 12 | `SceneViewScopedPreGameViewPassIsExcludedEvenWhenNameCollidesWithGameViewOne` | **NEEDS UPDATE** (re-verified against the real file: the surviving `GameView`-scoped `AtmosphereSkyViewLutPass` copy is now wrapped; `EXPECT_EQ(snapshot.totalEventCount, 2)` must become `3`; the excluded `SceneView`-scoped duplicate is still never wrapped/never consumes an index, unchanged) |
| 13 | `SceneViewScopedPostGameViewPassIsExcludedEvenWhenNameCollidesWithGameViewOne` | **NEEDS UPDATE** (re-verified against the real file: the surviving `GameView`-scoped `AtmosphereAerialPerspectiveCompositePass` copy is now wrapped; `EXPECT_EQ(snapshot.totalEventCount, 2)` must become `3`; the excluded `SceneView`-scoped duplicate is still never wrapped/never consumes an index, unchanged) |
| 14 | `SharedViewScopedPassStillAppearsNormally` | **NEEDS UPDATE** (re-verified against the real file: its one real surviving `Shared`-scoped compute pass — `AtmosphereTransmittanceLutPass`, category left at default `General` here so it lands in `preGroup`, not `lutGroup` — is now wrapped; `EXPECT_EQ(snapshot.totalEventCount, 2)` must become `3`) |
| 15 | `RenderOpaqueNodeGetsOneChildLeafPerDrawRecord` | UNCHANGED (RenderOpaque + draw records only — this is the mechanism PHASE2 explicitly must not touch) |
| 16 | `EventIndexIsMonotonicAcrossPreGameViewRenderOpaqueChildrenAndPostGameView` | **NEEDS UPDATE** (name implies compute passes both before and after `"RenderOpaque"` — re-simulate fully) |
| 17 | `RenderOpaqueNodeHasNoChildrenWhenNoDrawRecordsCaptured` | UNCHANGED (`"RenderOpaque"`-only) |
| 18 | `RenderOpaqueDrawRecordLeafPassNameIsNeverLiterallyRenderOpaque` | UNCHANGED (`"RenderOpaque"`-only) |
| 19 | `CulledComputePassIsExcludedFromEitherComputeDispatchGroup` | **NEEDS UPDATE** (re-verified against the real file: it DOES contain 2 real surviving compute passes — `SurvivingPreComputePass`/`SurvivingPostComputePass` — each now wrapped; `EXPECT_EQ(snapshot.totalEventCount, 3)` must become `5`; a CULLED pass itself is still never wrapped/never consumes an index, unchanged) |
| 20 | `ComputeDispatchLeafLabelsReadWriteRowsByRealResourceKind` | **CONFIRMED UNCHANGED** (re-verified against the real file: it only reads `.details->textures` on the PASS-LEVEL (parent) node — no `children.empty()`/index/`totalEventCount` assertion exists anywhere in this test) |
| 21 | `EventIndexIsMonotonicAcrossPreGameViewGameViewAndPostGameView` | **NEEDS UPDATE** (name implies the same shape as #16 pre-PHASE4-rename; re-simulate fully) |
| 22 | `PreGameViewLeafGetsNotYetDrawnStepPreviewKind` | **CONFIRMED UNCHANGED** (re-verified against the real file: it only reads `.details->stepPreviewKind` on the pass-level node — no index/child-count/`totalEventCount` assertion exists) |
| 23 | `RenderOpaqueLeafGetsPreCompositeStepPreviewKind` | UNCHANGED (`"RenderOpaque"`-only) |
| 24 | `PerObjectDrawRecordChildrenGetPerObjectStepKindAndIncreasingIndex` | UNCHANGED (`"RenderOpaque"`'s own per-entity children only) |
| 25 | `PostGameViewLeafBeforeCompositePassGetsPreCompositeStepPreviewKind` | **CONFIRMED UNCHANGED** (re-verified against the real file: only `postGroup.children.size() == 2u` — a GROUP sibling count, unaffected per Step 2.1 — and `.details->stepPreviewKind` on the pass-level node; no index/`totalEventCount` assertion exists) |
| 26 | `CompositePassLeafItselfGetsPostCompositeStepPreviewKind` | **CONFIRMED UNCHANGED** (re-verified against the real file: only `postGroup.children.size() == 2u` — sibling count, unaffected — and `.details->stepPreviewKind` on each pass-level node; no index/`totalEventCount` assertion exists) |
| 27 | `PostGameViewLeavesDefaultToPostCompositeWhenNoCompositePassSurvives` | **CONFIRMED UNCHANGED** (re-verified against the real file: only `postGroup.children.size() == 1u` — sibling count, unaffected — and `.details->stepPreviewKind` on the pass-level node; no index/`totalEventCount` assertion exists) |
| 28 | `DrawSkyBackgroundLeafSurvivesAlongsideComputeDispatchSplit` | **NEEDS UPDATE** — see Step 3.1's full worked example above |
| 29 | `ViewRegionHasExactlyRenderOpaqueAndDrawSkyBackgroundWhenRenderTransparentAbsent` | Minor addition only — see Step 3.3 above |
| 30 | `DebugCategoryGraphicsPassInsideViewRegionProducesNoExtraLeaf` | **NEEDS UPDATE** — see Step 3.2's full worked example above |
| 31 | `MaterialTextureRowIsNeverARenderGraphResource` | UNCHANGED (about `"Material Texture"` rows on `"RenderOpaque"`'s per-entity children, unrelated to this campaign) |

**Update from an independent deep review of this document (pre-implementation
sanity check):** every row originally marked `VERIFY` above (rows 12/13/14/
19/20/22/25/26/27) has now been read against the real, current test file and
resolved to a definitive `NEEDS UPDATE`/`CONFIRMED UNCHANGED` classification
in the table itself — rows 12/13/14/19 turned out to actually need an update
(each has a real, positively-asserted surviving compute pass whose wrapping
changes its `totalEventCount`), while rows 20/22/25/26/27 are confirmed
genuinely unaffected (each only reads pass-level, non-index fields or a
GROUP's own sibling count). The implementer should still re-verify against
whatever the file actually looks like once PHASE1/PHASE2 have landed (line
numbers may have shifted), but no row in this table is an open question
anymore.

### 3.5 New tests to add (do not skip — these cover the fix itself, not just non-regression)

Add to `tests/Editor/FrameDebuggerSnapshotBuilderTests.cpp`:

1. **`WrappedPassLevelNodeAndItsChildAreBothIndependentlySelectable`** — build
   a fixture with `"RenderOpaque"` + `"DrawSkyBackground"`, call
   `BuildRealFrameDebuggerSnapshot()`, then call
   `FindEventDetailsByIndex(snapshot, <parent eventIndex>)` AND
   `FindEventDetailsByIndex(snapshot, <child eventIndex>)` — both must
   return a populated `std::optional` with `passName == "DrawSkyBackground"`
   (Locked Design Decision #2 — dual selectability, proven end-to-end
   through the real lookup function, not just by inspecting the tree
   directly).
2. **`GraphicsChildEventLabelMatchesRenderPassDrawKind`** — build THREE
   synthetic fixtures, each with `"RenderOpaque"` + one other Graphics-kind
   pass in the view region (e.g. named `"SyntheticQuadPass"`,
   `"SyntheticMeshPass"`, `"SyntheticBlitPass"`), each tagged a DIFFERENT
   `rg::RenderPassDrawKind` (`DrawQuad`/`DrawMesh`/`Blit`) directly on the
   `RenderGraphPassSnapshot` fixture (mirrors how `MakePass()`/
   `MakeComputePass()` helpers already let a test set arbitrary fields) —
   assert each one's owned child is named exactly `"Draw Quad"`/`"Draw
   Mesh"`/`"Blit"` respectively. This is the ONE test in this whole
   campaign that actually exercises the `Blit` scaffold value end-to-end,
   even though no real pass uses it yet.
3. **`ComputeLutSubPassAlsoOwnsAComputeDispatchChild`** — reuse/extend
   `OnlyAtmosphereLutCategoryPreGameViewPassProducesOnlyComputeLutGroup`'s
   own fixture, add the assertion
   `ASSERT_EQ(lutGroup.children[0].children.size(), 1u);` +
   `EXPECT_EQ(lutGroup.children[0].children[0].name, "Compute Dispatch");`
   (or fold this directly into test #4's row above instead of a new test —
   implementer's choice, just make sure this exact fact is covered
   somewhere).

**Found during this review — one gap worth closing, and one edge case already covered (no new test needed):**
- Test 1 above (`WrappedPassLevelNodeAndItsChildAreBothIndependentlySelectable`) only proves dual-selectability for a wrapped GRAPHICS-kind pass (`"DrawSkyBackground"`). Since `WrapPassWithOwnedChildEvent()` is the exact same generic mechanism for a Compute-kind pass too, extend this same test (or add a fourth) to ALSO assert both the parent and child `eventIndex` of a wrapped COMPUTE pass (e.g. add one `MakeComputePass(...)` to the same fixture) are independently resolvable via `FindEventDetailsByIndex()` — cheap to add, and it is the one remaining un-exercised combination this campaign's own fix touches.
- The hinted "a pass with NO children at all today should behave identically to before" edge case is **already fully covered** — the only real-world node that can still have zero children after this whole campaign is `"RenderOpaque"` itself when `capture.DrawRecords()` is empty (every OTHER pass leaf now ALWAYS gets exactly one child, by design — there is no longer any other "childless today" case to preserve). `RenderOpaqueNodeHasNoChildrenWhenNoDrawRecordsCaptured` (test #17) already proves this exact case is untouched by PHASE2. No new test needed for this specific hint, but record in the completion report that this was explicitly checked, not overlooked.

### 3.6 Docs update

- `docs/conventions/frame-debugger.md`: update every ASCII tree diagram and
  prose paragraph that shows `"DrawSkyBackground"` (or any compute pass) as
  a flat, childless leaf (lines 76, 105, 116, 136, 163, 172, 189, 262, 296,
  309 all reference it — re-read each one in context, not just the line
  number, since some are just prose mentions that may not need a diagram
  change) to show the new nested "v PassName -> child event" shape. Add a
  short new paragraph introducing `rg::RenderPassDrawKind` and
  `WrapPassWithOwnedChildEvent()`'s role, mirroring how PHASE1-PHASE5 of
  `render-pass-1`'s own work is already documented in this same file.
- `AGENTS.md`'s "Render Pass System" section (line 123) and its "Frame
  Debugger" section: add a short paragraph noting every real pass leaf
  (except `"RenderOpaque"`, which uses its own per-entity mechanism) now
  owns exactly one real child event row describing its actual GPU
  operation, and that a Graphics-kind pass's own child label comes from its
  `rg::RenderPassDrawKind` tag (`AddRenderPass()`'s new optional trailing
  parameter, PHASE1) — never a pass-name string comparison.
- **Do NOT touch the root `README.md`'s "## Status" section in this phase.**
  Adding this campaign's own summary bullet there is intentionally deferred
  to PHASE4 (its own Step 3.5), since that bullet needs to quote the final
  build/regression/live-verification numbers only PHASE4 produces — do not
  duplicate or race that work here.

### 3.7 Build + scoped test run

- `cmake --build build --target GreatTamanaEngineTests` — confirm zero
  compile errors.
- Run the built test binary directly, FILTERED to just the files this
  campaign touches (NOT the full `ctest` suite — that is PHASE4's job):
  e.g. `build\tests\GreatTamanaEngineTests.exe
  --gtest_filter=FrameDebuggerSnapshotBuilderTest.*:FrameDebuggerDataTest.*:FrameDebuggerCaptureTest*.*:RenderGraphTypesTest.*:RenderGraphSnapshotTest.*:RenderPassTest.*`
  (adjust the exact executable path/test-suite names to whatever
  `tests/CMakeLists.txt` actually produces — confirm via `browse_dir`/
  `search_in_dir` first if unsure). Iterate until 100% of this filtered set
  passes.
- Only once that scoped run is green, move on to the completion report.

### 3.8 Completion report + commit

Write `task_manager/render-pass-2/PHASE3_COMPLETION_REPORT.md`: which of
the 31 tests actually needed updates vs. which were confirmed unchanged
(a real checklist, not just "see PHASE3.md's table" — record your OWN
verified findings), the 3 new tests added, the docs changed, and the exact
scoped test command + its final result. `git add` + `git commit` the code +
tests + docs + report together.

## Definition of Done

- [ ] Every one of the 31 existing tests in
      `FrameDebuggerSnapshotBuilderTests.cpp` verified against Step 2.1's
      rules — updated where needed, confirmed unchanged where not, with the
      real finding (not this document's guess) recorded in the completion
      report.
- [ ] The 3 new tests from Step 3.5 added and passing.
- [ ] `docs/conventions/frame-debugger.md` and `AGENTS.md` updated to
      describe the new nested tree shape and `RenderPassDrawKind` vocabulary.
- [ ] `cmake --build build --target GreatTamanaEngineTests` succeeds with
      zero errors.
- [ ] The scoped/filtered test run (Step 3.7) is 100% green.
- [ ] `PHASE3_COMPLETION_REPORT.md` written and committed alongside the
      code/tests/docs.

## What We Will NOT Do

- We will NOT run the full `ctest` regression suite (all ~1578+ tests) in
  this phase — only the scoped/filtered subset this campaign actually
  touches. Full regression is PHASE4's job.
- We will NOT create any brand-new `.cpp` test file — every change lands in
  the existing files named above.
- We will NOT change any production code in this phase (`src/` is
  untouched) — this phase is tests + docs only.
