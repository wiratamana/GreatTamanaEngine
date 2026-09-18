# PHASE2 — Frame Debugger Unified Pass-Ownership Rework

_Child of `PHASE0_MASTER_STRATEGY.md` (`task_manager/render-pass-2/`). Read
that file first. Assumes PHASE1 (`rg::RenderPassDrawKind`,
`RenderGraphPassSnapshot::drawKind`, `"DrawSkyBackground"` tagged
`DrawQuad`) has already landed — read `PHASE1_COMPLETION_REPORT.md` first
for any deviation._

## Step 1: The Goal (Where are we going?)

This is the actual bug fix. Rework `BuildRealFrameDebuggerSnapshot()`
(`src/Editor/FrameDebuggerData.cpp`) so that EVERY real pass leaf it builds
today as a single flat, childless `FrameDebuggerEventNode` — every
individual `"Compute LUT"` pass, every Pre/Post-GameView compute dispatch,
and `"DrawSkyBackground"` (and, structurally, any future `"RenderTransparent"`
that ever becomes real and non-empty) — instead becomes a parent node
owning exactly one real, independently-selectable child event node. Once
this lands, `src/Editor/Panels/FrameDebuggerPanel.cpp`'s existing
`RenderEventNode()` (unchanged, zero edits needed) will automatically start
drawing an expandable "v" arrow for every one of these rows, because it
already keys off `!node.children.empty()`.

`"RenderOpaque"` — already correctly shaped via `BuildRenderOpaqueLeaf()` +
its real per-entity `capture.DrawRecords()` children — is explicitly OUT OF
SCOPE for this phase. Do not touch `BuildRenderOpaqueLeaf()`,
`BuildRenderOpaqueDrawRecordLeaf()`, or their one call site (lines 738-754).

## Step 2: The Situation (Where are we now?)

All line numbers below refer to `src/Editor/FrameDebuggerData.cpp` as it
stood at the end of PHASE1 (PHASE1 does not touch this file, so line
numbers are unchanged from PHASE0's own investigation).

1. **`BuildComputeDispatchLeaf()`** (lines 282-367) builds ONE
   `FrameDebuggerEventNode` per real compute pass: `leaf.name = pass.name;`
   (line 286), `details.eventLabel = "Compute Dispatch";` (line 292),
   `details.passName = pass.name;` (line 300), plus real draw/timing stats.
   `leaf.children` is never touched — stays empty. Called from THREE places:
   - Lines 674-686 (loop `for (int i = 0; i < pivotIndex; ++i)`) — builds
     `computeLutGroup`/`preGameViewGroup` children, one call per surviving
     pre-view compute pass, `nextEventIndex++` consumed once per call
     (line 680).
   - Lines 771-790 (loop `for (int i = pivotIndex + 1; ...)`) — builds
     `postGameViewGroup` children the same way, `nextEventIndex++` consumed
     once per call (line 789).
2. **`BuildGraphicsPassLeaf()`** (lines 584-622) builds ONE
   `FrameDebuggerEventNode` for a Graphics-kind pass that is not
   `"RenderOpaque"`: `leaf.name = pass.name;` (line 588), `details.eventLabel
   = "Draw Pass";` (line 594, THIS LABEL IS REPLACED THIS PHASE — see 3.2
   below), plus the `"DrawSkyBackground"`-specific pipeline-state
   `if`/`else` at line 599-600 (UNCHANGED by this phase — still correct,
   still needed for the PARENT node's own details). Called from exactly one
   place: the view-region walk's `else` branch (lines 755-761,
   `nextEventIndex++` consumed once, line 760).
3. **`BuildRealFrameDebuggerSnapshot()`**'s three call-site loops (see
   above) each currently consume exactly ONE `nextEventIndex` value per
   surviving pass. After this phase, each survivor consumes TWO
   (`parentIndex`, then `childIndex = parentIndex + 1`) — the function's own
   pre-existing "chronological, monotonically increasing eventIndex" rule
   (documented at lines 656-661, 741-746) is preserved as long as both
   indices for the SAME pass are assigned back-to-back, in order, before
   moving to the next pass — which naturally falls out of calling
   `nextEventIndex++` twice, once for parent and once for child, at each
   existing call site, in the same relative order as today.
4. **`FrameDebuggerEventDetails`** (`FrameDebuggerData.h`, lines 125-171)
   has its own `eventIndex`/`eventLabel` fields, populated once per node
   inside `BuildComputeDispatchLeaf()`/`BuildGraphicsPassLeaf()`/
   `BuildRenderOpaqueLeaf()`. The child node this phase adds needs its OWN
   `eventIndex` (not the parent's) and its OWN `eventLabel` (the new,
   structural label — see 3.2/3.3 below) — everything else about `details`
   (blend mode, textures, vectors, matrices, `stepPreviewKind`, `passName`,
   `shaderName`) is IDENTICAL between parent and child by design (Locked
   Design Decision #2: both are independently selectable and both should
   show the same real, correct facts about the one real pass they both
   represent).
5. `FindEventDetailsByIndexRecursive()` (`FrameDebuggerData.cpp`, lines
   96-110) already recurses into `node.children` unconditionally — it needs
   ZERO changes to correctly find either the new parent OR the new child by
   `eventIndex` once this phase adds them.
6. `RenderEventNode()` (`FrameDebuggerPanel.cpp`, lines 578-629) needs ZERO
   changes — confirmed in PHASE0's own investigation. Do not touch this file
   in this phase.

## Step 3: The Plan

### 3.1 New shared wrapping helper (`FrameDebuggerData.cpp`, anonymous namespace)

Add, near the other small helpers in the anonymous namespace (e.g. right
after `WriteRowLabelForKind()`, before `BuildComputeDispatchLeaf()`):

```cpp
// Frame Debugger Pass-Ownership campaign (task_manager/render-pass-2),
// PHASE2 - the ONE shared mechanism that turns a single, flat "pass IS the
// draw event" leaf into a real "v PassName" parent OWNING exactly one real
// child event row - the fix for "DrawSkyBackground seems not owned by any
// render-pass" (and, generalized, every other flat pass leaf this file used
// to build - PHASE0_MASTER_STRATEGY.md's Locked Design Decision #1).
//
// `passLevelNode` is a fully-built leaf EXACTLY as BuildComputeDispatchLeaf()/
// BuildGraphicsPassLeaf() already built it before this phase (real name,
// real eventIndex, real populated `details`) - this function does not
// change any of that data, it only (a) makes a COPY of it for the new child
// (Locked Design Decision #2 - both parent and child stay independently
// selectable, both show the same real pass-level facts), (b) overwrites the
// COPY's own name/eventIndex/details->eventIndex/details->eventLabel to
// describe the actual GPU operation instead of the owning pass, and (c)
// attaches it as `passLevelNode`'s one and only child.
//
// `childEventIndex` must be the NEXT value `nextEventIndex` produces AFTER
// `passLevelNode.eventIndex` was assigned - the caller is responsible for
// this ordering (see this file's own BuildRealFrameDebuggerSnapshot(), which
// calls `nextEventIndex++` twice per pass, back-to-back, to guarantee it).
FrameDebuggerEventNode WrapPassWithOwnedChildEvent(
    FrameDebuggerEventNode passLevelNode, int childEventIndex, const std::string& childEventLabel)
{
    FrameDebuggerEventNode child = passLevelNode; // Copies name/eventIndex/details/children (children is always
                                                   // empty on passLevelNode at this point - defensive-safe either way).
    child.name = childEventLabel;
    child.eventIndex = childEventIndex;
    child.children.clear();
    if (child.details.has_value()) {
        child.details->eventIndex = childEventIndex;
        child.details->eventLabel = childEventLabel;
    }
    passLevelNode.children.push_back(std::move(child));
    return passLevelNode;
}

// Frame Debugger Pass-Ownership campaign, PHASE2 - which child-event label a
// Graphics-kind pass's owned child gets, derived PURELY from its own real,
// structural rg::RenderPassDrawKind (PHASE1) - NEVER from a pass-name string
// comparison (PHASE0_MASTER_STRATEGY.md's Locked Design Decision #3).
// Deliberately NO `default:` case - mirrors ReadRowLabelForKind()/
// WriteRowLabelForKind()'s own exhaustive-switch convention immediately
// above in this same file, so a future fourth RenderPassDrawKind enumerator
// fails to compile here until updated.
const char* GraphicsChildEventLabelFor(rg::RenderPassDrawKind drawKind)
{
    switch (drawKind) {
    case rg::RenderPassDrawKind::DrawMesh:
        return "Draw Mesh";
    case rg::RenderPassDrawKind::DrawQuad:
        return "Draw Quad";
    case rg::RenderPassDrawKind::Blit:
        return "Blit";
    }
    return "Draw Mesh";
}
```

### 3.2 `BuildGraphicsPassLeaf()`'s own `eventLabel` (line 594)

The PARENT node's own `details.eventLabel` was `"Draw Pass"` before this
phase — a placeholder label for a row that could never be expanded to see
what it actually did. Now that this same pass ALSO gets a real child
labeled precisely (`"Draw Mesh"`/`"Draw Quad"`/`"Blit"`), change the
parent's own `details.eventLabel` at line 594 to reuse the SAME structural
label via `GraphicsChildEventLabelFor(pass.drawKind)` too — i.e. both the
parent row and its child row report the identical, correct, structural
operation label; only their `name`/tree position differ. This keeps the
Inspector's "Event #N: {label}" header (see `FrameDebuggerData.h`'s own
comment on `eventLabel`, lines 143-158) meaningful no matter which of the
two rows is selected.

### 3.3 Wire `pass.drawKind` into `BuildGraphicsPassLeaf()`'s signature

`BuildGraphicsPassLeaf()` already takes `const rg::RenderGraphPassSnapshot&
pass` as its first parameter — `pass.drawKind` (PHASE1) is already directly
available; no signature change needed, only the new line 594 body change
above plus the new call-site wrapping (3.5 below).

### 3.4 Update the two `BuildComputeDispatchLeaf()` call sites (lines 674-686, 771-790)

At line 680 (`FrameDebuggerEventNode leaf = BuildComputeDispatchLeaf(pass,
nextEventIndex++, FrameDebuggerStepPreviewKind::NotYetDrawn);`), change to:

```cpp
        FrameDebuggerEventNode leaf =
            BuildComputeDispatchLeaf(pass, nextEventIndex++, FrameDebuggerStepPreviewKind::NotYetDrawn);
        leaf = WrapPassWithOwnedChildEvent(std::move(leaf), nextEventIndex++, "Compute Dispatch");
        if (pass.category == rg::RenderPassCategory::AtmosphereLut) {
            computeLutGroup.children.push_back(std::move(leaf));
        } else {
            preGameViewGroup.children.push_back(std::move(leaf));
        }
```

At line 789 (`postGameViewGroup.children.push_back(BuildComputeDispatchLeaf(pass,
nextEventIndex++, stepPreviewKind));`), change to:

```cpp
        FrameDebuggerEventNode leaf = BuildComputeDispatchLeaf(pass, nextEventIndex++, stepPreviewKind);
        leaf = WrapPassWithOwnedChildEvent(std::move(leaf), nextEventIndex++, "Compute Dispatch");
        postGameViewGroup.children.push_back(std::move(leaf));
```

### 3.5 Update the `BuildGraphicsPassLeaf()` call site (lines 755-761)

Change:

```cpp
        } else {
            // "DrawSkyBackground" today; a real future "RenderTransparent" ...
            root.children.push_back(
                BuildGraphicsPassLeaf(pass, nextEventIndex++, FrameDebuggerStepPreviewKind::PreComposite));
        }
```

to:

```cpp
        } else {
            // "DrawSkyBackground" today; a real future "RenderTransparent" once
            // that pass ever actually exists. Frame Debugger Pass-Ownership
            // campaign (render-pass-2), PHASE2 - this pass now also gets a real
            // owned child event row, labeled by its own structural
            // rg::RenderPassDrawKind (never a pass-name string match) - this is
            // the actual fix for "DrawSkyBackground seems not owned by any
            // render-pass".
            FrameDebuggerEventNode leaf =
                BuildGraphicsPassLeaf(pass, nextEventIndex++, FrameDebuggerStepPreviewKind::PreComposite);
            leaf = WrapPassWithOwnedChildEvent(
                std::move(leaf), nextEventIndex++, GraphicsChildEventLabelFor(pass.drawKind));
            root.children.push_back(std::move(leaf));
        }
```

### 3.6 Confirm `RenderOpaque` untouched

Re-read lines 738-754 after all edits above and confirm `BuildRenderOpaqueLeaf()`
+ its per-entity children loop are BYTE-FOR-BYTE unchanged. This is a
required self-check, not optional — the whole point of Locked Design
Decision #1 is that this pass's already-correct shape must survive
untouched.

### 3.7 Update `FrameDebuggerData.h`'s doc comments

`BuildRealFrameDebuggerSnapshot()`'s own big doc comment (lines 434-463)
currently draws the OLD flat tree shape ("`DrawSkyBackground` leaf" as a
childless bullet). Update the ASCII diagram there to show the new nested
shape (mirror PHASE0_MASTER_STRATEGY.md's own target-shape diagram, Step 1)
and add a short paragraph explaining `WrapPassWithOwnedChildEvent()`'s role
and the new doubled `nextEventIndex` consumption per pass, so a future
reader of this header understands the eventIndex-spacing rule without
having to re-read `FrameDebuggerData.cpp`'s own implementation comments.

### 3.8 Incremental compile check

`cmake --build build --target GreatTamanaEngine` — confirm zero errors. Do
**NOT** attempt to build or run `GreatTamanaEngineTests`/`ctest` this phase
— every existing test that asserts the OLD flat shape or an OLD
`eventIndex`/`totalEventCount` number will now fail to compile or fail
assertions; this is EXPECTED and explicitly deferred to PHASE3 (see PHASE0's
Cross-Cutting Rules). Note this explicitly, plainly, in this phase's own
completion report so PHASE3 starts from a documented, known state, not a
surprise.

### 3.9 Completion report + commit

Write `task_manager/render-pass-2/PHASE2_COMPLETION_REPORT.md`: what
changed, the exact new eventIndex-consumption rule (2 per pass instead of
1), an explicit list of every test file this phase is KNOWN to have broken
(from PHASE0's own Step 2 point 7 investigation — do not re-run them, just
name them so PHASE3 has a checklist), and the incremental compile result.
`git add` + `git commit` the code + report together.

## Definition of Done

- [ ] `WrapPassWithOwnedChildEvent()` and `GraphicsChildEventLabelFor()`
      exist in `FrameDebuggerData.cpp`'s anonymous namespace.
- [ ] All three call sites (`Compute LUT`/pre-view loop, post-view loop,
      view-region walk's `else` branch) wrap their leaf via
      `WrapPassWithOwnedChildEvent()`, each consuming exactly 2
      `nextEventIndex` values per surviving pass, in the correct
      parent-then-child order.
- [ ] `"DrawSkyBackground"` now produces a node with exactly one child named
      `"Draw Quad"` (via `pass.drawKind == RenderPassDrawKind::DrawQuad`,
      PHASE1), and both the parent row and the child row are independently
      findable via `FindEventDetailsByIndex()` with correct, matching
      pass-level facts.
- [ ] Every individual `"Compute LUT"` pass (and every other real compute
      dispatch) now produces a node with exactly one child named `"Compute
      Dispatch"`.
- [ ] `"RenderOpaque"`'s own leaf + per-entity children are BYTE-FOR-BYTE
      unchanged from before this phase.
- [ ] `src/Editor/Panels/FrameDebuggerPanel.cpp` has ZERO changes.
- [ ] `FrameDebuggerData.h`'s tree-shape doc comment updated to match the
      new nested shape.
- [ ] `cmake --build build --target GreatTamanaEngine` succeeds with zero
      errors/warnings.
- [ ] `PHASE2_COMPLETION_REPORT.md` written (explicitly listing every test
      file this phase is known to have broken) and committed alongside the
      code.

## What We Will NOT Do

- We will NOT touch `BuildRenderOpaqueLeaf()`, `BuildRenderOpaqueDrawRecordLeaf()`,
  or their call site.
- We will NOT touch `src/Editor/Panels/FrameDebuggerPanel.cpp` — its
  existing children-driven rendering logic already handles the new shape
  correctly with zero changes.
- We will NOT attempt to fix/update any test file in this phase — that is
  entirely PHASE3's job. We WILL, however, name every test file we know
  this phase breaks, in our own completion report.
- We will NOT run a full build or `ctest` in this phase.
