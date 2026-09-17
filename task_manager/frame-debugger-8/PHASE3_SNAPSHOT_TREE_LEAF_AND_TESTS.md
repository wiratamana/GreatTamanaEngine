# PHASE3 — Snapshot Tree Leaf and Tests

_Child of `PHASE0_MASTER_STRATEGY.md` (READ THAT FIRST). Depends on PHASE1
(`FrameDebuggerDrawRecord::isSkyBackgroundDraw`, `DescribeSkyBackgroundPipelineState()`)
and PHASE2 (the extra, correctly-ordered replay step whose image
`stepPreviewIndex` will point at) both already being in place._

## Step 1 — The Goal

Make the new sky draw record (PHASE1) actually show up as its own real,
correctly-labeled, correctly-positioned tree leaf under `"GameView"`, using
the EXACT SAME existing loop in `BuildRealFrameDebuggerSnapshot()` that
already builds one leaf per entity - zero structural changes to that outer
loop, only a new branch inside the one function it already calls per record.

## Step 2 — The Situation

`src/Editor/FrameDebuggerData.cpp`'s `BuildRealFrameDebuggerSnapshot()`
already contains this exact loop (UNCHANGED by this phase - shown here only
for context):

```cpp
int perObjectStepIndex = 0;
for (const FrameDebuggerDrawRecord& record : capture.DrawRecords()) {
    gameViewLeaf.children.push_back(BuildGameViewDrawRecordLeaf(
        record, capture.LastViewProjection(), nextEventIndex++, perObjectStepIndex));
    ++perObjectStepIndex;
}
```

Because PHASE1 makes `capture.DrawRecords()` naturally include the sky
record LAST (after every entity), this loop ALREADY iterates over it
correctly and ALREADY assigns it the correct, monotonically-next
`eventIndex` and the correct, monotonically-next `perObjectStepIndex` (which
PHASE2 already made line up with the real replay-step ordering) - with
**zero changes needed to this loop itself.** The only work left is inside
`BuildGameViewDrawRecordLeaf()` (the private, `anonymous`-namespace function
this loop calls) - it needs to know how to build a DIFFERENT-shaped leaf when
`record.isSkyBackgroundDraw == true`.

## Step 3 — The Plan

### 3.1 — `src/Editor/FrameDebuggerData.cpp`: rewrite `BuildGameViewDrawRecordLeaf()`

Find the existing function (private, anonymous namespace, currently ~lines
449-541) and replace its body. Full replacement:

```cpp
FrameDebuggerEventNode BuildGameViewDrawRecordLeaf(
    const FrameDebuggerDrawRecord& record, const Mat4& sharedViewProjection, int eventIndex, int stepPreviewIndex)
{
    FrameDebuggerEventNode leaf;
    leaf.isDrawCall = true;
    leaf.eventIndex = eventIndex;

    FrameDebuggerEventDetails details;
    details.eventIndex = eventIndex;
    details.stepPreviewKind = FrameDebuggerStepPreviewKind::PerObjectStep;
    details.stepPreviewIndex = stepPreviewIndex;

    // frame-debugger-8 campaign, PHASE3 - the Sky Background draw is not a
    // real ECS entity (record.entityIndex/entityGeneration are meaningless
    // for it - see FrameDebuggerCapture.h's own FrameDebuggerDrawRecord doc
    // comment) - it needs its own, differently-shaped leaf. This branch is
    // the ONLY change to this function versus its pre-campaign shape; the
    // `else` arm below is BYTE-FOR-BYTE the same code this function already
    // had for every real entity, unchanged.
    if (record.isSkyBackgroundDraw) {
        // Locked Design Decision 2 (PHASE0_MASTER_STRATEGY.md) - the tree
        // row's own name AND its Inspector "Shader" row are BOTH the real,
        // hand-verified shader file pair
        // (AtmosphereSkyBackgroundRenderer::ShaderDebugName(), threaded
        // through here via record.pipelineDebugName) - never an invented
        // cosmetic label like "Sky Background".
        leaf.name = record.pipelineDebugName;
        details.eventLabel = "Draw Fullscreen Triangle";
        // "GameView (Sky Draw)" - a real, structural fact (which real pass
        // this draw happened inside), DISTINCT from both the literal
        // "GameView" pass leaf's own passName AND from
        // "GameView (Entity Draw)" (the per-entity leaves' own passName) -
        // mirrors that exact, pre-existing distinctness precedent (see this
        // function's own `else` arm below, and its historical doc comment
        // above this function for the original "why must this differ from
        // the literal 'GameView' string" reasoning, which applies here too).
        details.passName = "GameView (Sky Draw)";
        details.shaderName = record.pipelineDebugName;

        // vectors - just the real triangle count (always 1 - a single
        // full-screen triangle, see FrameDebuggerCapture.cpp's own
        // RecordSkyBackgroundDraw()). Deliberately NO "Entity (Index,
        // Generation)" row here (unlike the `else` arm below) - there is no
        // real ECS entity behind this record at all, and fabricating one
        // would violate this whole tree's own "never invent a fact" rule.
        {
            FrameDebuggerVectorProperty triangleCount;
            triangleCount.name = "Triangle Count";
            triangleCount.x = static_cast<float>(record.triangleCount);
            details.vectors.push_back(triangleCount);
        }

        // blend/Z/stencil - THIS pass's own real, distinct pipeline state
        // (Locked Design Decision 4, PHASE0_MASTER_STRATEGY.md) - never the
        // generic per-mesh DescribeStandardPipelineState() every entity
        // leaf reuses (see the `else` arm below).
        {
            const FrameDebuggerStandardPipelineState pipelineState = DescribeSkyBackgroundPipelineState();
            details.blendMode = pipelineState.blendMode;
            details.zClip = pipelineState.zClip;
            details.zTest = pipelineState.zTest;
            details.zWrite = pipelineState.zWrite;
            details.cull = pipelineState.cull;
            details.stencilRef = pipelineState.stencilRef;
            details.stencilComp = pipelineState.stencilComp;
            details.stencilPass = pipelineState.stencilPass;
            details.stencilFail = pipelineState.stencilFail;
            details.stencilZFail = pipelineState.stencilZFail;
        }
    } else {
        // UNCHANGED from before this campaign - every real per-entity leaf
        // keeps behaving exactly as it always has.
        leaf.name = record.displayName + " (Entity " + std::to_string(record.entityIndex) + ")";
        details.eventLabel = "Draw Mesh";
        details.passName = "GameView (Entity Draw)";
        details.shaderName = record.pipelineDebugName;

        if (!record.materialTextureDebugName.empty()) {
            FrameDebuggerTextureProperty texture;
            texture.name = "Material Texture";
            texture.valueLabel = record.materialTextureDebugName;
            details.textures.push_back(std::move(texture));
        }

        {
            FrameDebuggerVectorProperty triangleCount;
            triangleCount.name = "Triangle Count";
            triangleCount.x = static_cast<float>(record.triangleCount);
            details.vectors.push_back(triangleCount);

            FrameDebuggerVectorProperty entityIdentity;
            entityIdentity.name = "Entity (Index, Generation)";
            entityIdentity.x = static_cast<float>(record.entityIndex);
            entityIdentity.y = static_cast<float>(record.entityGeneration);
            details.vectors.push_back(entityIdentity);
        }

        {
            const FrameDebuggerStandardPipelineState pipelineState = DescribeStandardPipelineState();
            details.blendMode = pipelineState.blendMode;
            details.zClip = pipelineState.zClip;
            details.zTest = pipelineState.zTest;
            details.zWrite = pipelineState.zWrite;
            details.cull = pipelineState.cull;
            details.stencilRef = pipelineState.stencilRef;
            details.stencilComp = pipelineState.stencilComp;
            details.stencilPass = pipelineState.stencilPass;
            details.stencilFail = pipelineState.stencilFail;
            details.stencilZFail = pipelineState.stencilZFail;
        }
    }

    // matrices - the SAME shared view-projection every draw in this one
    // real Game-View pass this frame used, for BOTH branches above (the
    // sky's own fragment shader really does invert this exact matrix - see
    // AtmosphereSkyBackgroundRenderer::Draw()'s own `viewProjection`
    // parameter) - UNCHANGED code, simply now shared by both branches
    // instead of only ever running for the entity case.
    {
        FrameDebuggerMatrixProperty viewProjection;
        viewProjection.name = "ViewProjection";
        for (int row = 0; row < 4; ++row) {
            for (int col = 0; col < 4; ++col) {
                viewProjection.values[static_cast<std::size_t>(row * 4 + col)] = sharedViewProjection(row, col);
            }
        }
        details.matrices.push_back(viewProjection);
    }

    leaf.details = std::move(details);
    return leaf;
}
```

**Nothing else in `FrameDebuggerData.cpp`'s CODE changes** - not
`BuildRealFrameDebuggerSnapshot()`'s own loop (Step 2 above already showed
why), not `BuildGameViewLeaf()`, not `BuildComputeDispatchLeaf()`, not
`ChooseFrameDebuggerPreviewSource()`. Its own PRE-EXISTING DOC COMMENT
(immediately above the function, not its body) does need one small addition
- see Step 3.5 below (v2 addendum).

### 3.2 — `src/Editor/FrameDebuggerData.h`: doc-comment-only touch-up

`BuildRealFrameDebuggerSnapshot()`'s own header comment already documents the
tree shape and the `"GameView"` children mechanism in detail (frame-debugger-6/
frame-debugger-7 campaign comments). Append one short paragraph (do not
rewrite the existing prose) right after the existing frame-debugger-7 PHASE4
paragraph, before the function's own signature:

```cpp
// frame-debugger-8 campaign
// (PHASE3_SNAPSHOT_TREE_LEAF_AND_TESTS.md) - capture.DrawRecords() now
// carries exactly ONE additional record per captured frame whenever a Sky
// Background draw actually ran this frame (FrameDebuggerCaptureContext::
// RecordSkyBackgroundDraw(), always the LAST record, appended strictly
// after every real per-entity record) - this loop needed NO changes at all
// to pick it up correctly; only BuildGameViewDrawRecordLeaf() itself
// (FrameDebuggerData.cpp) gained a new branch for it. See
// task_manager/frame-debugger-8/PHASE0_MASTER_STRATEGY.md for the full
// story.
```

### 3.3 — `tests/Editor/FrameDebuggerDataTests.cpp`: new Tier-1 tests

Find this file's existing tests that exercise `BuildGameViewDrawRecordLeaf()`
indirectly via `BuildRealFrameDebuggerSnapshot()` (or directly, if this file
already has a way to reach it - check whether the function is exposed for
testing via a test-only forward declaration or if tests only go through the
public `BuildRealFrameDebuggerSnapshot()` entry point; mirror whichever
pattern this file already uses for the existing per-entity leaf tests
EXACTLY). Add:

1. **`SkyBackgroundDrawRecordProducesDistinctLeaf`** - build a
   `FrameDebuggerCaptureContext`, call `RecordEntityDraw()` once (a fake
   entity) then `RecordSkyBackgroundDraw("Sky.vert/Sky.frag")`, build a
   snapshot, and assert: the `"GameView"` node has exactly 2 children; the
   2nd child's `name == "Sky.vert/Sky.frag"` (NOT
   `"... (Entity 0)"`-shaped); its `details->passName ==
   "GameView (Sky Draw)"`; its `details->zTest == "Equal"`; its
   `details->zWrite == "Off"`.
2. **`SkyBackgroundLeafHasNoEntityIdentityVector`** - same setup as above,
   assert the sky leaf's own `details->vectors` contains NO entry named
   `"Entity (Index, Generation)"` (only `"Triangle Count"`).
3. **`SkyBackgroundLeafEventIndexIsMonotonicallyAfterEntityLeaves`** - assert
   the sky leaf's own `eventIndex` is exactly one greater than the entity
   leaf's own `eventIndex`, and that both are less than any Post-GameView
   compute leaf's own `eventIndex` (a direct regression test for this
   campaign's own cross-phase ordering invariant, mirroring the existing
   `EventIndexIsMonotonicAcrossPreGameViewGameViewAndPostGameView`-style test
   this file/its sibling `FrameDebuggerSnapshotBuilderTests.cpp` already has
   for the pre-existing pre/post-GameView split - copy that exact test's own
   fixture-building style).
4. **`SkyBackgroundLeafStepPreviewIndexMatchesItsPositionInDrawRecords`** -
   assert the sky leaf's own `details->stepPreviewIndex` equals
   `capture.DrawRecords().size() - 1` (its own real, 0-based position) - a
   direct regression test for PHASE0's own "Cross-phase invariant" section.
5. **`EntityLeafShapeIsUnchangedWhenNoSkyRecordExists`** - a pure regression
   guard: build a snapshot with ONLY `RecordEntityDraw()` calls (no sky
   record at all, exactly like every pre-campaign test already does),
   assert every existing entity-leaf assertion this file already had STILL
   passes byte-for-byte (proves the `else` branch truly is unchanged).

### 3.4 — `tests/Editor/FrameDebuggerSnapshotBuilderTests.cpp`: one more test

Add **`SkyBackgroundLeafSurvivesAlongsideComputeDispatchSplit`** - build a
fake `rg::RenderGraphSnapshot` with a `"GameView"` pass plus at least one
Pre-GameView and one Post-GameView compute pass (copy this file's own
existing fixture-building helper for that shape), pair it with a
`FrameDebuggerCaptureContext` that has 1 entity record + 1 sky record, call
`BuildRealFrameDebuggerSnapshot()`, and assert the overall tree shape is
still exactly:

```
Game View
  |-- Compute Dispatches (Pre-GameView)   (1 child)
  |-- GameView                            (2 children: entity, then sky)
  |-- Compute Dispatches (Post-GameView)  (1 child)
```

with every `eventIndex` across all 5 leaves still strictly monotonic
increasing in that exact left-to-right, top-to-bottom order - proving PHASE1
through PHASE3 compose correctly with the PRE-EXISTING pre/post-GameView
compute-dispatch split (`frame-debugger-5`/`frame-debugger-6` campaigns) with
zero regressions.

### 3.5 — Update the doc comment directly above `BuildGameViewDrawRecordLeaf()` itself (v2 addendum)

`FrameDebuggerData.cpp`'s existing doc comment sitting directly above
`BuildGameViewDrawRecordLeaf()` (the large comment block starting "frame-
debugger-6 campaign, PHASE4 - one real, individually selectable leaf per real
per-entity draw call...") still only describes the per-entity leaf shape -
it does not yet mention that, as of this campaign, the SAME function also
builds a differently-shaped leaf for the one non-entity sky record. Append
one short paragraph to that existing comment block (do not rewrite the
existing prose - append after it, immediately before the function's own
signature):

```cpp
// frame-debugger-8 campaign, PHASE3
// (PHASE3_SNAPSHOT_TREE_LEAF_AND_TESTS.md) - this function now also builds a
// SECOND, differently-shaped leaf when `record.isSkyBackgroundDraw == true`
// (the one, real Sky Background full-screen-triangle draw - see
// FrameDebuggerCapture.h's own FrameDebuggerDrawRecord doc comment) - see
// this function's own body for the two-branch split. The per-entity `else`
// branch this comment block already describes above is completely
// unchanged.
```

## Compile check for this phase

```
cmake --build build --target gte_core
cmake --build build --target GreatTamanaEngineTests
```

Run the test binary; confirm all new tests (Step 3.3, 3.4) pass and every
pre-existing `FrameDebuggerData`/`FrameDebuggerSnapshotBuilder`/
`FrameDebuggerCapture`/`FrameDebuggerHistory` test still passes unchanged.

**No `build-editor-off` check is needed for this phase** (v2 addendum, see
`PHASE0_MASTER_STRATEGY.md`'s own "v2 addendum" section for the full
reasoning) - `src/Editor/FrameDebuggerData.h/.cpp` only ever compiles when
`GTE_ENABLE_EDITOR` is `ON` (see the root `CMakeLists.txt`'s own
`if(GTE_ENABLE_EDITOR)` block, which this file's source list lives inside),
so this phase's changes structurally cannot affect the `GTE_ENABLE_EDITOR=OFF`
configuration at all.

## Definition of Done (this phase only)

- `BuildGameViewDrawRecordLeaf()` matches Step 3.1 exactly; the `else` arm is
  byte-for-byte the pre-campaign behavior.
- A live capture (see PHASE2's own live spot-check, now re-run with this
  phase's code also in place) shows a REAL, selectable `"AtmosphereSkyBackground.vert/
  AtmosphereSkyBackground.frag"` tree row as the LAST child of `"GameView"`,
  with correct Pass/Shader/Blend/Z/Stencil rows and a correct preview image
  (matches PHASE0's own campaign-wide Definition of Done).
- All new Tier-1 tests (Step 3.3, 3.4) pass; zero regressions in any
  pre-existing test in these files.
- The doc comment directly above `BuildGameViewDrawRecordLeaf()` (Step 3.5)
  and `BuildRealFrameDebuggerSnapshot()`'s own header comment (Step 3.2) are
  both updated.

## Report

Write `PHASE3_COMPLETION_REPORT.md` in this same folder, then
`git_add`/`git_commit`.
