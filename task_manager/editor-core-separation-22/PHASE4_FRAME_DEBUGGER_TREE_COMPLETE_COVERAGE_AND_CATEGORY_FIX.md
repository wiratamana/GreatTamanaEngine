# PHASE4 — Fix `RenderPassCategory::Debug` misuse and make the Frame Debugger tree structurally incapable of silently dropping a surviving pass

Parent: `PHASE0_MASTER_STRATEGY.md` (read it fully first — Step 2.2 has the
full root-cause trace this phase implements against).
Campaign folder: `task_manager/editor-core-separation-22/`
Previous phase report to read first: `PHASE3_COMPLETION_REPORT.md` — confirm
no PHASE3 fix touched `RenderPassCategory`/`FrameDebuggerData.cpp` in a way
that changes this phase's own starting assumptions; if it did, reconcile
before proceeding.

## Step 1: The Goal (Where are we going?)

1. `DemoRenderFeaturePlugin_Clear`/`DemoRenderFeatureSecondPlugin_Clear`
   (and any other real, user-toggleable, non-Frame-Debugger-internal pass,
   present OR future) must appear as a real, named, selectable leaf
   somewhere in the Frame Debugger's own captured event tree whenever it
   genuinely, non-culled, executes this frame — full stop, regardless of
   its `RenderPassEvent` tier, its registration order, or whether some
   OTHER, unrelated Compute-kind pass happens to sit near it in execution
   order.
2. `RenderPassCategory::Debug`'s own documented contract ("Frame-Debugger-
   internal replay passes... never a real Frame Debugger tree citizen")
   must be honored EXACTLY as documented — and used ONLY for passes that
   are genuinely, permanently, Frame-Debugger-internal scaffolding
   (`FrameDebuggerReplayStepN`) — never reused as a stand-in for "this is
   an optional/debug-flavored real feature".
3. This must be a STRUCTURAL guarantee, not a patch for this one pass: the
   tree-building code must be reworked so that ANY future new pass, of any
   kind, tagged any category except the genuinely-internal one, that
   survives culling, is GUARANTEED a leaf somewhere — by construction, not
   by whoever adds that new pass remembering to also update
   `FrameDebuggerData.cpp`'s own walk logic.

## Step 2: The Situation (Where are we now?)

Re-read `PHASE0_MASTER_STRATEGY.md`'s Step 2.2 in full. Summary of the two
independent, COMPOUNDING problems it found:

1. **Category misuse**: `src/Renderer/RenderGraph/RenderGraphTypes.h`
   (~line 387-390) defines `RenderPassCategory::Debug` as meaning
   "Frame-Debugger-internal... never a real tree citizen" — but
   `src/Core/Plugins/PluginRenderPassBuilderAdapter.cpp`'s
   `AddFullscreenClearPass()` (~line 67-82), `src/Editor/GBufferValidation.cpp`
   (~line 164, 207), and `src/Editor/ComputeBlurValidation.cpp` (~line 109)
   all tag themselves `RenderPassCategory::Debug` for a DIFFERENT,
   incompatible reason ("genuinely optional/debug-flavored... mirrors
   GBufferValidation.cpp's own precedent") — a real, confirmed, historical
   misunderstanding, propagated by each successive author copying the
   previous one's (wrong) precedent.
2. **Structural gap**: `src/Editor/FrameDebuggerData.cpp`'s
   `BuildRealFrameDebuggerSnapshot()` has generic fallback buckets for
   surviving Compute-kind passes (`"Compute Dispatches (Pre/Post-GameView)"`,
   ~line 755-781) but has NO EQUIVALENT for Graphics-kind passes. Its "view
   region" walk (~line 835-889) `break`s out entirely the first time it
   meets a surviving Compute-kind pass, handing the rest of the array to a
   second loop (~line 898-919) that explicitly skips anything that is not
   Compute-kind (line 906). A Graphics-kind pass positioned (by
   `RenderPassEvent` tier, e.g. `AfterEverything`) structurally AFTER that
   hand-off point is invisible REGARDLESS of its category — reclassifying
   `DemoRenderFeaturePlugin_Clear`'s category alone would not be sufficient.

### 2.1 — Why "just recategorize it" is necessary but not sufficient, and why the sweep-based fallback is the right, scalable answer

A narrow patch (special-case `DemoRenderFeaturePlugin_Clear`'s own name or
category inside the existing walk loops) would fix today's ONE reported
pass and leave the exact same trap for the NEXT plugin/feature author who
adds a new pass positioned anywhere structurally "inconvenient" for the
current two-loop walk's own assumptions. The user's own instruction is
explicit: "do not do lazy immediate fix... rework existing system to make
it more robust, correct, and scalable." This phase therefore adds a THIRD,
FINAL, GENERIC sweep over the whole `passesInExecutionOrder` array — after
every existing specialized bucket/walk has run — that picks up ANY
surviving pass (Graphics or Compute, any `RenderPassEvent` tier, any
position) that no earlier bucket/walk already claimed, and places it into
one new, honestly-labeled, always-correct catch-all group. This is the
SAME "generic, name-free, structural" design philosophy this codebase's own
`FindViewRegionPivot()`/`FindPostGameViewCompositePassExecutionIndex()`/
`RenderPassGroupRegistry` mechanisms already use elsewhere — this phase
extends that same philosophy to close the one remaining gap, rather than
inventing a new, less consistent approach.

## Step 3: The Plan (detailed strategy)

### 3.1 — Fix the category misuse first (mechanical, low-risk, do this before the tree rework)

1. In `src/Renderer/RenderGraph/RenderGraphTypes.h`, add a NEW enumerator to
   `RenderPassCategory` — e.g. `FrameDebuggerInternal` — with a doc comment
   stating PLAINLY: "genuinely, permanently Frame-Debugger-owned ephemeral
   scaffolding (today: `FrameDebuggerReplayStepN`) — the ONLY category value
   that is unconditionally invisible to the Frame Debugger's own tree,
   regardless of what it does or does not survive culling as. Never use
   this for a real, user-facing feature pass, no matter how 'debug-
   flavored' it feels — use `Debug` (or `General`) for that instead; see
   this enum's own updated `Debug` doc comment." Update `ToString()` and
   every exhaustive `switch` over `RenderPassCategory` accordingly (the
   file's own "no default: case, ever" convention — `search_in_dir` for
   `RenderPassCategory` across `src/` to find every switch/comparison site
   that needs updating, not just the ones already cited in PHASE0).
2. Update `RenderPassCategory::Debug`'s own doc comment to remove the now-
   incorrect "never a real Frame Debugger tree citizen" clause (that
   sentence now describes `FrameDebuggerInternal` instead) — `Debug` now
   plainly means "a real, optional/debug-flavored FEATURE pass, fully
   visible in the tree like any other survivor, when it runs."
3. `src/Editor/FrameDebuggerReplayPasses.cpp` (~line 183): change
   `rg::RenderPassCategory::Debug` to `rg::RenderPassCategory::FrameDebuggerInternal`
   — this is the ONLY real user of the new value; every other current
   `RenderPassCategory::Debug` call site (`PluginRenderPassBuilderAdapter.cpp`,
   `GBufferValidation.cpp` x2, `ComputeBlurValidation.cpp`) KEEPS
   `RenderPassCategory::Debug` unchanged, now with its CORRECTED meaning.
4. `src/Editor/FrameDebuggerData.cpp`'s own exclusion check at the "view
   region" walk (~line 852-856): change the condition from
   `pass.category == rg::RenderPassCategory::Debug` to
   `pass.category == rg::RenderPassCategory::FrameDebuggerInternal`. Confirm
   there is no OTHER place in this same file (or elsewhere) that still
   checks `== rg::RenderPassCategory::Debug` expecting the OLD "hidden"
   meaning — `search_in_dir` for `RenderPassCategory::Debug` across
   `src/Editor/` specifically before considering this step done.

### 3.2 — Build the generic, final "honesty sweep" fallback bucket

In `BuildRealFrameDebuggerSnapshot()`, track — across every existing
specialized loop/bucket already in this function (the pre-GameView compute
buckets, the view-region walk, the post-GameView compute bucket) — which
INDICES into `graphSnapshot.passesInExecutionOrder` were actually claimed
(i.e. turned into a real leaf node somewhere, OR deliberately, correctly
excluded for an honest, already-covered reason: culled, `ViewScope::SceneView`,
or `RenderPassCategory::FrameDebuggerInternal`). Concretely:
1. Add a `std::vector<bool> claimed(graphSnapshot.passesInExecutionOrder.size(), false);`
   (or an equivalent `std::vector<int>` matching index-count) near the top
   of the function, sized once, up front.
2. At every point in the EXISTING three loops where a pass is turned into a
   leaf (the pre-GameView compute loop, the view-region walk's two leaf-
   building branches, the post-GameView compute loop), mark
   `claimed[i] = true` for that pass's own index — a small, mechanical,
   additive change to each existing loop body, not a rewrite of their own
   logic.
3. At every point in the EXISTING three loops where a pass is deliberately,
   honestly skipped for an ALREADY-DOCUMENTED reason (culled,
   `ViewScope::SceneView`, `RenderPassCategory::FrameDebuggerInternal`),
   ALSO mark `claimed[i] = true` — these are not omissions, they are
   correct, intentional exclusions, and must not be re-swept into the new
   fallback bucket as if they were missed.
4. After all three existing loops finish (i.e. immediately before this
   function's own final `FrameDebuggerSnapshot snapshot; ...; return
   snapshot;` at the bottom, ~line 924 onward), add ONE final loop over
   the FULL `graphSnapshot.passesInExecutionOrder` range:
   ```cpp
   FrameDebuggerEventNode otherPassesGroup;
   otherPassesGroup.name = "Other Render Passes";
   otherPassesGroup.isDrawCall = false;
   for (std::size_t i = 0; i < graphSnapshot.passesInExecutionOrder.size(); ++i) {
       if (claimed[i]) {
           continue;
       }
       const rg::RenderGraphPassSnapshot& pass = graphSnapshot.passesInExecutionOrder[i];
       if (pass.isCulled || pass.viewScope == rg::ViewScope::SceneView
           || pass.category == rg::RenderPassCategory::FrameDebuggerInternal) {
           continue; // Still an honest, correct exclusion even here - defensive,
                     // should already be caught by claimed[i] above.
       }
       FrameDebuggerEventNode leaf = (pass.kind == rg::PassKind::Compute)
           ? WrapPassWithOwnedChildEvent(
                 BuildComputeDispatchLeaf(pass, nextEventIndex++, FrameDebuggerStepPreviewKind::PostComposite),
                 nextEventIndex++, "Compute Dispatch")
           : WrapPassWithOwnedChildEvent(
                 BuildGraphicsPassLeaf(pass, nextEventIndex++, FrameDebuggerStepPreviewKind::PostComposite),
                 nextEventIndex++, GraphicsChildEventLabelFor(pass.drawKind));
       otherPassesGroup.children.push_back(std::move(leaf));
   }
   if (!otherPassesGroup.children.empty()) {
       root.children.push_back(std::move(otherPassesGroup));
   }
   ```
   (Treat this as a concrete STARTING sketch, not literal final code — the
   implementer must confirm `BuildComputeDispatchLeaf()`/`BuildGraphicsPassLeaf()`'s
   own real signatures/step-preview-kind semantics still make sense for a
   pass that, unlike every other bucket's own passes, has NO natural
   "before/after composite" relationship — read `FrameDebuggerStepPreviewKind`'s
   own doc comment and pick the most honest available value, or add a new
   one via `ask_questions` if none fits.)
5. This sweep is the SAME "structural, name-free" pattern already
   established elsewhere in this file — it makes `BuildRealFrameDebuggerSnapshot()`
   complete BY CONSTRUCTION: any future pass, of any kind/tier/category
   (other than the one genuinely-internal exception), that survives
   culling is now GUARANTEED to end up somewhere in the tree, without the
   author of that future pass needing to touch this file at all.

### 3.3 — Update `FrameDebuggerData.h`'s own tree-shape documentation

That file's own doc comment (~line 442-499, describing the tree shape) must
be updated to describe the new terminal "Other Render Passes" group and the
corrected meaning of `RenderPassCategory::FrameDebuggerInternal` vs
`Debug` — this codebase's own convention (seen throughout every file read
during PHASE0's investigation) is to keep this kind of structural doc
comment in lock-step with the real code shape.

### 3.4 — Tests

The correct, ALREADY-EXISTING home for these tests is
`tests/Editor/FrameDebuggerSnapshotBuilderTests.cpp` — NOT
`tests/Editor/FrameDebuggerDataTests.cpp` (that file covers small, unrelated
pure helpers — `FormatFrameStepperLabel()`, `ClampSelectedEventIndex()`,
`ComputeAspectFitImageRect()`, etc. — it has no test that ever calls
`BuildRealFrameDebuggerSnapshot()` at all). `FrameDebuggerSnapshotBuilderTests.cpp`
is specifically, exclusively dedicated to `BuildRealFrameDebuggerSnapshot()`
(confirm this yourself — it already has ~35 tests exercising exactly this
function, including the `MakePass()`/`MakeComputePass()`/
`MakeGraphicsPassWithCategory()` fixture helpers this phase's own new tests
should reuse). Add the new coverage there: (a) a synthetic `RenderGraphSnapshot`
containing a Graphics-kind, non-Debug/non-FrameDebuggerInternal pass
positioned AFTER a surviving Compute-kind pass (reproducing the exact
structural shape that used to silently drop `DemoRenderFeaturePlugin_Clear`)
now produces a real leaf under `"Other Render Passes"`; (b) a synthetic
`FrameDebuggerInternal`-tagged pass remains invisible exactly as before; (c) a
real `Debug`-category pass that WOULD have been dropped by the old code is
now visible.

**MANDATORY pre-existing-test update, not optional**: this same file already
has a test, `DebugCategoryGraphicsPassInsideViewRegionProducesNoExtraLeaf`
(built via `MakeGraphicsPassWithCategory("FrameDebuggerReplayStep0",
rg::RenderPassCategory::Debug)` /
`MakeGraphicsPassWithCategory("FrameDebuggerReplayStep1", rg::RenderPassCategory::Debug)`),
which asserts `root.children.size() == 3u` (i.e. these two `Debug`-tagged
synthetic passes produce ZERO leaves). Once this phase's own fix lands
(`RenderPassCategory::Debug` no longer means "invisible", only
`FrameDebuggerInternal` does, and the new "Other Render Passes" fallback
sweep exists), this exact test WILL START FAILING — those two `Debug`-tagged
passes will now correctly surface inside the new "Other Render Passes"
group, growing `root.children.size()` to 4. You MUST update this test's own
fixture to tag those two synthetic passes `rg::RenderPassCategory::FrameDebuggerInternal`
instead of `rg::RenderPassCategory::Debug` (matching what
`FrameDebuggerReplayPasses.cpp`'s own real production code now does per Step
3.1 item 3 above) so the test keeps asserting the thing it actually means to
assert ("the Frame Debugger's own internal replay scaffolding never leaks
into the tree") rather than the now-incorrect claim that `Debug`-category
passes are invisible. Do not skip this — an untouched, now-stale
`RenderPassCategory::Debug` literal in this fixture will make the test fail
the moment this phase's code change lands, and the failure will look like a
regression in the NEW code when it is actually just a fixture that needs the
same rename applied to it.

### 3.5 — Live, HTTP-driven verification (mandatory)

1. Enable `DemoRenderFeaturePlugin_Clear` (confirm via
   `GET /render_graph/passes` it is enabled and non-culled this frame).
2. `GET /frame_debugger/capture` (or the panel's own capture trigger),
   then `GET /frame_debugger/state` and confirm a real leaf named
   `DemoRenderFeaturePlugin_Clear` (or reachable by inspecting the JSON
   tree's own `"Other Render Passes"` group) now exists with real event
   data.
3. Disable it and confirm it vanishes from BOTH the graph AND the tree.
4. Re-run PHASE1's own sky-related live checks once more (`GET /get_logs?category=RenderPassHonesty`
   included) to confirm this phase's tree rework introduced zero
   regression against PHASE1/PHASE3's own fixes — the sky's own
   `"DrawSkyBackground"` leaf is built by the PRE-EXISTING view-region walk,
   untouched by this phase's sweep addition, but confirm this explicitly
   rather than assuming it.
5. Confirm `GBufferValidation`/`ComputeBlurValidation` still show nothing in
   the Frame Debugger tree (unchanged — they remain `ViewScope::SceneView`-
   excluded, independent of this phase's category fix) — this phase must
   not accidentally make them appear if that was never the intent; if live
   testing suggests the user actually WANTS these visible too, `ask_questions`
   before changing their `ViewScope` (a much bigger, likely out-of-scope
   change) rather than assuming silently.

### 3.6 — End of phase

1. Incremental build succeeds.
2. New/extended Tier-1 tests pass (targeted filter).
3. Write `PHASE4_COMPLETION_REPORT.md` with the exact diff summary, the
   live-verification evidence from 3.5, and any `ask_questions` interaction.
4. `git_add` + `git_commit` covering every file changed and the report.
