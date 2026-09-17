# PHASE2 — Frame Debugger Consumes ViewScope — Fix The Duplicate/Mis-Scoped Leak

_Read `PHASE0_MASTER_STRATEGY.md` and `PHASE1_RENDERGRAPH_VIEWSCOPE_CHOKEPOINT_
INFRASTRUCTURE.md` in full first. This phase REQUIRES PHASE1's
`RenderGraphPassSnapshot::viewScope` field to already exist and be correctly
stamped — do not start this phase until PHASE1's own Definition of Done is
fully satisfied._

## Step 1: The Goal (Where are we going?)

Fix the actual, user-confirmed bug: capture a frame in the Frame Debugger and
see NO duplicate/indistinguishable compute-pass leaves, and NO Scene-View-only
pass (or Scene-View-only debug tool) appearing anywhere under
`"Compute Dispatches (Pre-GameView)"`/`"Compute Dispatches (Post-GameView)"`.
Concretely, capturing a frame against `TestScene.gtscene` (Directional Light
present, both Game View and Scene View visible) must now show:

- `"Compute Dispatches (Pre-GameView)"`: `AtmosphereTransmittanceLutPass`,
  `AtmosphereMultiScatteringLutPass` (both `Shared`), `AtmosphereSkyViewLutPass`
  (the ONE `GameView`-scoped instance), `AtmosphereAerialPerspectiveVolumePass`
  (the ONE `GameView`-scoped instance),
  `AtmosphereAerialPerspectiveVolumeDebugSlicePass` (`GameView`-scoped, only
  instance that exists today) — **exactly 5 leaves, no duplicates.**
- `"Compute Dispatches (Post-GameView)"`: `AtmosphereAerialPerspectiveCompositePass`
  (the ONE `GameView`-scoped instance) — **exactly 1 leaf**, down from the
  previously-observed 4 (which incorrectly included a second
  `AtmosphereSkyViewLutPass`, a second `AtmosphereAerialPerspectiveVolumePass`,
  and a second `AtmosphereAerialPerspectiveCompositePass`, all really
  belonging to Scene View).
- If the Editor's "Show Compute Blur (debug)" toggle is on, `ComputeBlurValidation`
  must NOT appear anywhere in the Game-View-scoped tree at all anymore (it is
  `SceneView`-scoped — confirmed in PHASE1 Step 2).

This phase changes ONLY `FrameDebuggerData.cpp`'s discovery loops (plus their
own doc comments/tests) — it does not touch `RenderPasses.cpp`,
`Application.cpp`, or the render graph core again (that was PHASE1).

## Step 2: The Situation (Where are we now?)

- `src/Editor/FrameDebuggerData.cpp::BuildRealFrameDebuggerSnapshot()` has two
  loops (re-read this function in full again right now — it may have shifted
  slightly since this campaign's planning, though PHASE1 should not have
  touched it at all):

  ```cpp
  for (int i = 0; i < gameViewIndex; ++i) {
      const rg::RenderGraphPassSnapshot& pass = graphSnapshot.passesInExecutionOrder[i];
      if (!pass.isComputePass || pass.isCulled) {
          continue;
      }
      preGameViewGroup.children.push_back(BuildComputeDispatchLeaf(pass, nextEventIndex++));
  }
  ...
  for (int i = gameViewIndex + 1; i < ...size(); ++i) {
      const rg::RenderGraphPassSnapshot& pass = graphSnapshot.passesInExecutionOrder[i];
      if (!pass.isComputePass || pass.isCulled) {
          continue;
      }
      postGameViewGroup.children.push_back(BuildComputeDispatchLeaf(pass, nextEventIndex++));
  }
  ```

- Thanks to PHASE1, `pass.viewScope` (a real `rg::ViewScope`) is now available
  on every entry here.

## Step 3: The Plan (exact changes)

### 3.1 The filter change

Add exactly one more condition to BOTH loops' existing `continue` guard —
exclude any pass whose `viewScope` is `SceneView` (keep `Shared` and
`GameView`):

```cpp
if (!pass.isComputePass || pass.isCulled || pass.viewScope == rg::ViewScope::SceneView) {
    continue;
}
```

Apply this identical change to BOTH the pre-GameView loop and the
post-GameView loop in `BuildRealFrameDebuggerSnapshot()` — do not write two
different conditions for the two loops; the rule ("never show a Scene-View-only
pass in the Game-View-scoped tree") is symmetric.

Do NOT filter on `pass.name` or any resource-name suffix anywhere in this
file — the whole point of PHASE1 was to make this filter a plain,
one-line, structural boolean check. If you find yourself wanting to compare
strings, PHASE1 was not applied correctly to that pass's call site — go fix
PHASE1's own coverage instead of working around it here.

### 3.2 Update the doc comment above `BuildRealFrameDebuggerSnapshot()`

The existing header-comment tree-shape diagram and its surrounding prose
(inside `FrameDebuggerData.h`, near the function's declaration, and mirrored
again inside `FrameDebuggerData.cpp` right above the loops) currently only
describes filtering by `isComputePass`/`isCulled`/execution-order position.
Update it to also state the `viewScope != SceneView` rule, and reference this
campaign (`frame-debugger-6`) as the origin, mirroring how the existing
comments already reference `frame-debugger-5` for the split-group mechanism
itself. Do not delete the existing `frame-debugger-5` attribution/history —
add to it.

### 3.3 `FrameDebuggerSnapshot`/`FrameDebuggerRenderTargetInfo` — no other struct
changes needed

`viewScope` is consumed ONLY inside the filtering loop — it does not need to
be copied into `FrameDebuggerEventNode`/`FrameDebuggerEventDetails` (the user's
own ask was "which pass ran", not "show me the raw ViewScope enum value" —
keep the leaf's own displayed fields exactly as `BuildComputeDispatchLeaf()`
already produces them today, no changes needed inside that function at all
for this phase).

## Step 4: Tests — the actual regression proof

Per `AGENTS.md`'s rule ("Fixing a bug in one of these files must add a
regression test that fails before the fix and passes after it"), add a new
case to `tests/Editor/FrameDebuggerSnapshotBuilderTests.cpp` (locate the exact
existing test file/fixture pattern used there first) that:

1. Hand-builds an `rg::RenderGraphSnapshot` containing (at minimum): the
   `"GameView"` pass itself, ONE `isComputePass=true` pass named
   `"AtmosphereSkyViewLutPass"` with `viewScope = ViewScope::GameView`
   positioned before `"GameView"`'s index, and a SECOND, DIFFERENT
   `isComputePass=true` pass ALSO literally named `"AtmosphereSkyViewLutPass"`
   (same name, on purpose — this is the exact real-world collision) but with
   `viewScope = ViewScope::SceneView`, ALSO positioned before `"GameView"`'s
   index.
2. Calls `BuildRealFrameDebuggerSnapshot()` with this hand-built snapshot.
3. Asserts the resulting `"Compute Dispatches (Pre-GameView)"` group has
   EXACTLY ONE child named `"AtmosphereSkyViewLutPass"` — not two.
4. Add a second case covering the POST-GameView group symmetrically (a
   Scene-View-scoped pass positioned after `"GameView"`'s index must not
   appear there either) — e.g. modeling the real
   `"AtmosphereAerialPerspectiveCompositePass"` duplicate scenario.
5. Add a third case confirming a `ViewScope::Shared` pass (e.g. modeling
   `"AtmosphereTransmittanceLutPass"`) still appears normally — this guards
   against an over-eager fix that accidentally excludes `Shared` passes too.
6. If `tests/Editor/FrameDebuggerDataTests.cpp` has any existing hand-built
   `RenderGraphPassSnapshot` fixtures/helpers used across multiple test files,
   check whether they need a `viewScope` field explicitly set now that it
   exists (an aggregate-initialized struct missing a new field simply
   defaults it to `Shared`, which is almost always still correct for an
   existing, pre-PHASE1 test case — but re-read each affected fixture to
   confirm this default doesn't silently change what that specific test was
   asserting).

## Step 5: Definition of Done for PHASE2

- [ ] Both discovery loops in `BuildRealFrameDebuggerSnapshot()` exclude
      `ViewScope::SceneView` passes.
- [ ] Doc comments updated (function-level in both `.h` and `.cpp`).
- [ ] New regression tests added per Step 4, all passing; no existing
      Frame-Debugger test file's assertions were loosened to make this pass.
- [ ] Incremental build succeeds.
- [ ] Manual live re-check (same recipe as PHASE1's own Step 5 manual check):
      launch the engine, load `TestScene.gtscene`, open+enable+capture the
      Frame Debugger over HTTP, `GET /get_swapchain`, and visually confirm the
      tree now shows exactly the leaf counts described in this phase's own
      Step 1 (5 Pre-GameView leaves, 1 Post-GameView leaf, no duplicates).
      Also toggle "Show Compute Blur (debug)" on (find its real toggle
      location in the Editor UI or its own HTTP/EditorContext field if one
      exists; if none is HTTP-exposed, toggling it manually via a screenshot
      + a described click is acceptable, or simply reason about it via the
      code path and note that it was not live-toggled this phase) and confirm
      `ComputeBlurValidation` does not appear in the Game-View tree. Stop the
      background process when done.
- [ ] Commit via `git_add`/`git_commit`.
