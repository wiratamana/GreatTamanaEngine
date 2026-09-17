# PHASE3 — Completion Report: Snapshot Tree Leaf and Tests

_Reports to `PHASE0_MASTER_STRATEGY.md`. Implements
`PHASE3_SNAPSHOT_TREE_LEAF_AND_TESTS.md` (v2, including its "(v2 addendum)"
Step 3.5) in full._

## Summary

Made the new Sky Background draw record (PHASE1) actually show up as its
own real, correctly-labeled, correctly-positioned tree leaf under
`"GameView"`, using the EXACT SAME existing loop in
`BuildRealFrameDebuggerSnapshot()` that already builds one leaf per entity —
zero structural changes to that outer loop, only a new branch inside
`BuildGameViewDrawRecordLeaf()`, the one function it already calls per
record. Confirmed before starting that both of this phase's dependencies
already exist in the real, current source: `FrameDebuggerDrawRecord::
isSkyBackgroundDraw` and `DescribeSkyBackgroundPipelineState()` (PHASE1,
`src/Editor/FrameDebuggerCapture.h/.cpp`), and PHASE2's restructured
`AddFrameDebuggerReplayPasses()` producing the extra, correctly-ordered
"sky step" replay image (`src/Application/RenderPasses.cpp`) — both
verified live in the working tree (`git status` showed a clean tree with
PHASE1/PHASE2 already committed) before any edit in this phase began.

## What was done (maps 1:1 to the phase document's Step 3)

- **3.1 — `src/Editor/FrameDebuggerData.cpp`**: replaced
  `BuildGameViewDrawRecordLeaf()`'s body with the phase document's exact new
  two-branch shape — a new `if (record.isSkyBackgroundDraw)` branch builds
  the sky leaf (`leaf.name`/`details.shaderName` = the real
  `AtmosphereSkyBackground.vert/AtmosphereSkyBackground.frag` shader-pair
  string threaded through `record.pipelineDebugName`; `details.eventLabel =
  "Draw Fullscreen Triangle"`; `details.passName = "GameView (Sky Draw)"`;
  only a "Triangle Count" vector row, deliberately no "Entity (Index,
  Generation)" row; blend/Z/stencil from `DescribeSkyBackgroundPipelineState()`
  — real `Equal`/`Off`, not the generic `Less`/`On`), and the pre-existing
  `else` branch is BYTE-FOR-BYTE unchanged (confirmed by direct comparison
  against the pre-edit file read earlier in this session). The shared
  `ViewProjection` matrix block at the end of the function is now used by
  both branches, exactly as specified.
- **3.2 — `src/Editor/FrameDebuggerData.h`**: appended the specified new
  paragraph to `BuildRealFrameDebuggerSnapshot()`'s own header doc comment,
  immediately before its signature, explaining `capture.DrawRecords()` now
  carries one extra sky record whenever a Sky Background draw ran that
  frame, and that the existing loop needed no changes to pick it up.
- **3.3 — `tests/Editor/FrameDebuggerDataTests.cpp`**: added all 5 documented
  new tests, reaching the new branch indirectly via the public
  `BuildRealFrameDebuggerSnapshot()` entry point (this file already had
  `#include "Editor/FrameDebuggerData.h"`, which transitively pulls in both
  `FrameDebuggerCaptureContext` and `rg::RenderGraphSnapshot`, so no new
  includes were needed — mirrors this same file's own pre-existing style,
  which never called `BuildGameViewDrawRecordLeaf()` directly since it is a
  private, anonymous-namespace function):
  `SkyBackgroundDrawRecordProducesDistinctLeaf`,
  `SkyBackgroundLeafHasNoEntityIdentityVector`,
  `SkyBackgroundLeafEventIndexIsMonotonicallyAfterEntityLeaves`,
  `SkyBackgroundLeafStepPreviewIndexMatchesItsPositionInDrawRecords`, and
  `EntityLeafShapeIsUnchangedWhenNoSkyRecordExists`.
- **3.4 — `tests/Editor/FrameDebuggerSnapshotBuilderTests.cpp`**: added
  `SkyBackgroundLeafSurvivesAlongsideComputeDispatchSplit`, reusing this
  file's own existing `MakePass()`/`MakeComputePass()` fixture helpers,
  proving the full 5-leaf tree shape (Pre-GameView group, `"GameView"` with
  its 2 children — entity then sky — Post-GameView group) still holds
  together with strictly monotonic `eventIndex` values end to end.
- **3.5 (v2 addendum) — `src/Editor/FrameDebuggerData.cpp`**: appended the
  specified new paragraph to the existing doc comment directly above
  `BuildGameViewDrawRecordLeaf()` (the "frame-debugger-6 campaign, PHASE4 -
  one real, individually selectable leaf..." block), explaining this
  function now also builds a second, differently-shaped leaf for the one
  non-entity sky record, without rewriting any of the pre-existing prose.

**Nothing else in `FrameDebuggerData.cpp`'s CODE changed** — confirmed by
inspecting the diff: `BuildRealFrameDebuggerSnapshot()`'s own loop,
`BuildGameViewLeaf()`, `BuildComputeDispatchLeaf()`, and
`ChooseFrameDebuggerPreviewSource()` are all untouched, exactly as the phase
document requires.

## Deviations from the plan

None. Every Step 3.x item was implemented literally as specified in the v2
phase document, including its v2-addendum Step 3.5. No new gaps or
surprises were discovered while implementing this phase.

## Compile check results (this phase's own scope)

- `cmake --build build --target gte_core` — **succeeded** (4/4 steps, clean
  — only `FrameDebuggerData.cpp`, `FrameDebuggerHistory.cpp`,
  `Panels/FrameDebuggerPanel.cpp`, and `ImGuiEditorLayer.cpp` recompiled,
  then link).
- `cmake --build build --target GreatTamanaEngineTests` — **succeeded**
  (4/4 steps, clean — `FrameDebuggerHistoryTests.cpp`,
  `FrameDebuggerDataTests.cpp`, `FrameDebuggerSnapshotBuilderTests.cpp`
  recompiled, then link).
- Ran `GreatTamanaEngineTests.exe --gtest_filter=*FrameDebugger*` first: all
  **90 Frame Debugger tests passed**, including the 6 new tests this phase
  added (5 in `FrameDebuggerDataTests.cpp`, 1 in
  `FrameDebuggerSnapshotBuilderTests.cpp`) and every pre-existing
  `FrameDebuggerData`/`FrameDebuggerSnapshotBuilder`/`FrameDebuggerCapture`/
  `FrameDebuggerHistory`/`FrameDebuggerCommandBridge`/`NetworkRoutes`
  Frame-Debugger-related test, unchanged.
- Then ran the FULL `GreatTamanaEngineTests.exe` suite (no filter), per
  `AGENTS.md`'s own "run the actual test suite" rule: **1559 tests ran, 1558
  passed, 1 skipped** (`PmxLoaderRealModelSmokeTest` — the same pre-existing,
  environment-dependent skip PHASE1's own completion report already noted,
  unrelated to this change). Zero regressions anywhere in the suite.
- Per this phase document's own v2 addendum, **no `build-editor-off` check
  was run** — `src/Editor/FrameDebuggerData.h/.cpp` and the two test files
  touched here only ever compile under `GTE_ENABLE_EDITOR=ON` (the root
  `CMakeLists.txt`'s own `if(GTE_ENABLE_EDITOR)` block), so this phase's
  changes structurally cannot affect the `GTE_ENABLE_EDITOR=OFF`
  configuration at all — confirmed correct reasoning, not skipped out of
  laziness.

## Definition of Done (this phase only) — verified

- [x] `BuildGameViewDrawRecordLeaf()` matches Step 3.1 exactly; the `else`
      arm is byte-for-byte the pre-campaign behavior.
- [x] All new Tier-1 tests (Step 3.3, 3.4) pass; zero regressions in any
      pre-existing test in these files, or anywhere else in the full suite.
- [x] The doc comment directly above `BuildGameViewDrawRecordLeaf()` (Step
      3.5) and `BuildRealFrameDebuggerSnapshot()`'s own header comment (Step
      3.2) are both updated.
- [x] A live capture would now show a real, selectable
      `"AtmosphereSkyBackground.vert/AtmosphereSkyBackground.frag"` tree row
      as the LAST child of `"GameView"`, with correct Pass/Shader/Blend/Z/
      Stencil rows — this specific claim was **not** independently
      re-verified via a fresh live HTTP/screenshot spot-check in this phase
      (PHASE2's own completion report already did the equivalent live
      spot-check confirming the underlying per-step image/ordering is
      correct, and this phase's own new Tier-1 tests directly assert every
      one of the leaf's data fields matches the phase document's
      requirements) — the next full, live, end-to-end HTTP-driven
      screenshot verification of the WHOLE feature (including this phase's
      own leaf-labeling change) is explicitly PHASE4's job, per
      `PHASE4_DOCS_REGRESSION_LIVE_VERIFICATION_AND_FULL_BUILD.md`.

## Notes for PHASE4 (next phase)

- Every piece of PHASE0's Definition of Done that requires actually
  querying a live captured frame (the new sky leaf's real name/passName/
  blend-Z-stencil rows, and the visible difference between the last entity's
  own preview and the sky leaf's own preview) is now backed by passing
  Tier-1 tests, but PHASE4's own live HTTP-driven smoke test is the first
  point in this campaign where the WHOLE chain — real capture -> real
  replay passes -> real snapshot builder INCLUDING this phase's new sky-leaf
  branch -> real Editor UI -> a real screenshot — gets exercised together
  end to end. Expect to see, on a scene with at least one mesh entity: a
  `"AtmosphereSkyBackground.vert/AtmosphereSkyBackground.frag"` leaf as the
  LAST child of `"GameView"`, its Pass row reading `"GameView (Sky Draw)"`,
  its Z Test/Z Write rows reading `"Equal"`/`"Off"`, and its own preview
  image showing the full sky/mountain background composited over whatever
  objects were drawn before it — while selecting the last entity's own leaf
  immediately before it shows the same scene WITHOUT the sky.
- The four stale-doc-comment fixes this whole campaign's own v2 addendum
  called out are now ALL individually landed: `Game.h` (PHASE1),
  `FrameDebuggerCapture.h`/`FrameDebuggerHistory.h` (PHASE2), and
  `FrameDebuggerData.cpp`'s own doc comment above
  `BuildGameViewDrawRecordLeaf()` plus `BuildRealFrameDebuggerSnapshot()`'s
  header comment (this phase). Only
  `docs/conventions/frame-debugger.md`'s own "What is real today" section
  (its ASCII tree diagram and its "exactly ONE Pipeline configuration...
  nothing to fabricate per-mesh here" claim) remains — this is explicitly
  PHASE4's own Step 3.1 job, not touched here.
- No blockers, no discovered deviations requiring a design re-think. This
  phase's scope was small, self-contained, and completed exactly as
  specified in the v2 phase document (including its v2-addendum Step 3.5).
