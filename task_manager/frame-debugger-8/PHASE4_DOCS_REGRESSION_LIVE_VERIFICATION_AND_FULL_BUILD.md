# PHASE4 — Docs, Full Build, and Live Verification

_Child of `PHASE0_MASTER_STRATEGY.md` (READ THAT FIRST). Depends on PHASE1,
PHASE2, and PHASE3 all already being complete. This is the ONLY phase in this
campaign allowed to run a full build/full regression suite - see this
phase's own Step 3.3._

## Step 1 — The Goal

Close out the campaign exactly like every prior `frame-debugger-N` campaign
before it: update the permanent documentation so a future contributor
understands this fix without re-deriving it, prove the whole thing actually
works end-to-end against the real, running engine (not just unit tests), do
the one full build this whole campaign earns (in BOTH `GTE_ENABLE_EDITOR`
configurations - v2 addendum, see Step 3.3), and write the closing
`CAMPAIGN_COMPLETION_REPORT.md`.

## Step 2 — The Situation

PHASE1-PHASE3 already made every real code/test change. What remains is
documentation (this codebase treats its own `docs/conventions/*.md` files as
load-bearing, not optional - every prior `frame-debugger-N` campaign updated
`docs/conventions/frame-debugger.md` as part of closing out - AND, per this
campaign's own v2 double-check pass, that update must correct the EXISTING
"What is real today" section too, not just append a new "What's new" one -
see Step 3.1 below), plus the one full, real build + live smoke test this
multi-phase campaign has earned (per this whole task's own top-level rule:
"don't do full regression test or full build test for every phase... do test
only on last phase").

## Step 3 — The Plan

### 3.1 — `docs/conventions/frame-debugger.md`: update TWO existing sections, then add a new one

**(a) First, update the EXISTING "## What is real today" section in two
places** (v2 addendum - found during this campaign's own second-pass
double-check; mirrors the EXACT precedent `frame-debugger-6` already set when
IT updated this same section in place for its own per-entity-leaf change,
rather than only ever adding a brand-new "What's new" section and leaving the
"current reality" summary stale):

1. Find the bullet beginning "**Every real compute-shader dispatch that ran
   this frame is now a first-class, AUTOMATICALLY DISCOVERED tree citizen...**"
   - it contains this ASCII tree diagram:
   ```
   "GameView" (root)
     |-- "Compute Dispatches (Pre-GameView)"   (only present if >=1 child)
     |     |-- <every surviving compute pass whose own real execution-order
     |     |     index in graphSnapshot.passesInExecutionOrder is BEFORE
     |     |     "GameView"'s own index, in real execution order>
     |-- "GameView" leaf                        (the one real graphics/draw pass)
     |     |-- <one real child leaf PER real per-entity draw call this pass
     |     |     issued this frame - e.g. "terrain (Entity 2)",
     |     |     "SmokeTestCube (Entity 3)" - frame-debugger-6 campaign, PHASE4>
     |-- "Compute Dispatches (Post-GameView)"  (only present if >=1 child)
           |-- <every surviving compute pass whose own real execution-order
           |     index is AFTER "GameView"'s own index, in real execution order>
   ```
   Update the `"GameView"` leaf's own child-line to also mention the sky, and
   add a new line right after it:
   ```
   "GameView" (root)
     |-- "Compute Dispatches (Pre-GameView)"   (only present if >=1 child)
     |     |-- <every surviving compute pass whose own real execution-order
     |     |     index in graphSnapshot.passesInExecutionOrder is BEFORE
     |     |     "GameView"'s own index, in real execution order>
     |-- "GameView" leaf                        (the one real graphics/draw pass)
     |     |-- <one real child leaf PER real per-entity draw call this pass
     |     |     issued this frame - e.g. "terrain (Entity 2)",
     |     |     "SmokeTestCube (Entity 3)" - frame-debugger-6 campaign, PHASE4>
     |     |-- <one final real child leaf for the Sky Background full-screen-
     |     |     triangle draw, always LAST - e.g.
     |     |     "AtmosphereSkyBackground.vert/AtmosphereSkyBackground.frag" -
     |     |     frame-debugger-8 campaign>
     |-- "Compute Dispatches (Post-GameView)"  (only present if >=1 child)
           |-- <every surviving compute pass whose own real execution-order
           |     index is AFTER "GameView"'s own index, in real execution order>
   ```
2. Find the bullet beginning "**Shader/pass-state 'reflection' is real, but
   PASS-scoped, aggregated across every real draw call that pass issued that
   frame...**" - it currently ends with: "...blend/Z/stencil rows report this
   engine's real, single, constant `Pipeline` configuration
   (`DescribeStandardPipelineState()`...) — this engine has exactly ONE
   Pipeline configuration today (`Pipeline.cpp`: no blend, `VK_COMPARE_OP_LESS`
   depth test, no stencil test anywhere), so there is nothing to fabricate
   per-mesh here; a genuine future per-material blend/Z/stencil VARIATION
   would need its own follow-up campaign, not just a data-plumbing change."
   This claim is no longer accurate once this campaign's own Sky Background
   leaf exists (a second, genuinely different, real pipeline state). Append a
   new sentence right after it (do not delete/rewrite the existing sentence -
   it is still correct for every MESH leaf):
   ```
   The Sky Background leaf (`frame-debugger-8` campaign) is the ONE other real,
   non-fabricated pipeline-state fact in this window - it reports its own
   genuinely different `DescribeSkyBackgroundPipelineState()` values (`Depth
   Test = Equal`, `Depth Write = Off`, both DELIBERATELY different from every
   mesh's `Less`/`On`) - still not a per-MATERIAL/per-mesh variation (there is
   still exactly one Pipeline configuration for every real mesh draw), simply
   a second, distinct, real draw TYPE this tree now honestly distinguishes.
   ```

**(b) Then add a new top-level section**, immediately after the existing
`## What's new (frame-debugger-7 campaign)` section and BEFORE `## Known
limitation, now fixed (frame-debugger-4 campaign)` (i.e. right after the most
recent "What's new" entry, mirroring the exact placement pattern every prior
campaign's own `## What's new (frame-debugger-N campaign)` section already
uses - most recent campaign's section goes right after the previous
most-recent one, in strict chronological order, ahead of the older "Known
limitation" appendix sections):

```markdown
## What's new (`frame-debugger-8` campaign)

`frame-debugger-8` (`task_manager/frame-debugger-8/PHASE0_MASTER_STRATEGY.md`,
four phases) fixed one more real, user-confirmed gap: the Sky Background pass
- a real, direct `vkCmdDraw()` full-screen-triangle draw
(`AtmosphereSkyBackgroundRenderer::Draw()`, drawn LAST every frame, after
every real entity, using its own genuinely different `EQUAL`-depth-test
pipeline state so it only paints pixels nothing else touched yet) - never
went through `RenderSystem::Draw()`/`Renderer::Submit()` at all, so it was
COMPLETELY INVISIBLE anywhere in the event tree, even though it genuinely
runs every single frame. A second, closely-related bug was found (and
independently spotted by the user, comparing the last object's own preview
image against the object drawn just before it) while investigating: the LAST
real entity's own per-step preview image secretly ALREADY included the sky,
bundled in for an unrelated technical reason, with no way to see "just after
the last object, before sky" as its own distinct state.

- **A new `FrameDebuggerCaptureContext::RecordSkyBackgroundDraw()` method**
  (`src/Editor/FrameDebuggerCapture.h/.cpp`) is now called exactly once, from
  `AddGameViewPass()`'s own `execute` lambda (`src/Application/RenderPasses.cpp`),
  immediately after the real `recordSkyBackground` callback runs - the same
  "only touch the capture context when it's actually armed" discipline every
  other call site already follows. It reuses `RecordDraw()`'s own existing
  dedup/draw-call-count/last-view-projection bookkeeping internally (the sky
  uses the exact same view-projection matrix every other draw in the pass
  used that frame), and appends one new `FrameDebuggerDrawRecord` marked
  `isSkyBackgroundDraw = true` - a new field appended at the end of that
  struct, alongside the real per-entity records `RecordEntityDraw()` already
  produces, always LAST (mirroring the real GPU draw order:
  every entity, then sky).
- **The new leaf's own identifying name is a REAL, hand-verified fact, never
  an invented cosmetic label.** `AtmosphereSkyBackgroundRenderer::ShaderDebugName()`
  (a new, permanent, `constexpr` static method) returns the actual real
  shader-file-pair string this pass genuinely loads,
  `"AtmosphereSkyBackground.vert/AtmosphereSkyBackground.frag"` - this is
  both the new tree row's own display name AND its Inspector "Shader" row.
  This deliberately follows the exact same hard-learned lesson the
  `frame-debugger-5` campaign already established when it REMOVED the old
  hardcoded "GPU Skinning"/"Aerial Perspective Composite" cosmetic-label
  special cases (see that campaign's own section above) - never fabricate a
  friendly name detached from a real, checkable fact. Its own `passName` row
  reads `"GameView (Sky Draw)"`, mirroring the pre-existing
  `"GameView (Entity Draw)"` per-entity-leaf convention exactly (a real,
  structural "which pass did this happen inside" fact, distinct from the
  literal `"GameView"` pass leaf's own name, for the same
  string-collision-avoidance reason `frame-debugger-6`'s own PHASE4
  originally documented).
- **The new leaf's own Blend/Z/Stencil Inspector rows are genuinely
  different from every mesh's**, via a new, dedicated
  `DescribeSkyBackgroundPipelineState()` function
  (`src/Editor/FrameDebuggerCapture.h/.cpp`), hand-transcribed directly from
  `AtmosphereSkyBackgroundRenderer.cpp`'s own real
  `VkPipelineDepthStencilStateCreateInfo` construction: `Depth Test = Equal`
  (not `Less`) and `Depth Write = Off` (not `On`) - the ONE other place in
  this whole engine (besides the single, constant
  `DescribeStandardPipelineState()` every mesh leaf reuses) that reports a
  genuinely different real Pipeline configuration in this window.
- **A real, correctly-ordered replay preview for the new leaf** - the
  `frame-debugger-7` campaign's own per-object replay-rendering mechanism
  (`AddFrameDebuggerReplayPasses()`, `src/Application/RenderPasses.cpp`) is
  restructured so the sky is NEVER drawn inside a per-object replay step
  anymore (fixing the "last object's own image secretly already included
  sky" bug outright) - instead, exactly ONE new, dedicated replay step is
  added whenever a sky callback exists that frame, redrawing every real
  object and then the sky, becoming the new leaf's own correct,
  pixel-accurate "Game View as of right after the sky was drawn" preview
  image (pixel-identical to the whole-frame `preview`, by construction - the
  same useful internal cross-check the OLD, buggy code's own comment already
  noted, now genuinely isolated onto its own step instead of incorrectly
  fused onto the last object's). A scene with ZERO mesh entities now also
  correctly gets exactly one real replay step (the sky alone), instead of
  zero - the old code's own `if (objectCount == 0) return;` early-out used
  to skip replay entirely for an empty scene, silently hiding the sky there
  too.
- **`BuildGameViewDrawRecordLeaf()` (`src/Editor/FrameDebuggerData.cpp`)
  branches on the new `isSkyBackgroundDraw` flag** to build this
  differently-shaped leaf (no fabricated "Entity (Index, Generation)" row,
  since there is no real ECS entity behind it) - the existing
  `BuildRealFrameDebuggerSnapshot()` loop that walks
  `capture.DrawRecords()` needed ZERO structural changes at all; it already
  iterates the sky record correctly simply because
  `RecordSkyBackgroundDraw()` appends it in true chronological (real GPU)
  order.
- **Scope stayed Game View ONLY, per this feature's own permanent rule** -
  the underlying `AtmosphereSkyBackgroundRenderer::Draw()` function is
  genuinely shared/identical for both the Game View and the Editor's own
  Scene View (same real Vulkan pipeline, same shader files) - only the Frame
  Debugger's own CAPTURE call site differs, and it was only ever wired into
  `AddGameViewPass()` to begin with; `AddSceneViewPass()` remains untouched
  by this campaign, exactly like every other Game-View-only mechanism this
  window already has.
- **A known, EXPLICITLY DEFERRED, pre-existing, narrow gap this campaign did
  NOT fix**: the parent `"GameView"` leaf's own aggregate "Draw Stats
  (Calls, Tris)" row (sourced from `RenderGraphPassSnapshot::stats.drawStats`,
  fed only by `Renderer::Submit()`'s own bookkeeping) still does not count
  the sky's own raw `vkCmdDraw()` call, since that call happens outside any
  `Renderer::Submit()`/`BeginGraphPassRecording()` bracket - a real, narrow,
  separate gap from the one this campaign fixed (the sky's OWN dedicated
  leaf's own "Triangle Count" row is correct in isolation; only the parent
  aggregate undercounts by exactly one draw call). Left as a clean, isolated
  future item, not silently forgotten.

See `task_manager/frame-debugger-8/CAMPAIGN_COMPLETION_REPORT.md` for the
full four-phase writeup plus the live, HTTP-driven, screenshot-verified proof
both the new Sky Background leaf and the corrected per-step replay preview
work end-to-end.
```

### 3.2 — `AGENTS.md`: one-line pointer update

`AGENTS.md`'s own "Frame Debugger" section already ends with a sentence
about `frame-debugger-7`'s per-entity leaves and links to
`docs/conventions/frame-debugger.md`. Append one clause to that paragraph's
sentence ending "...PLUS one real, individually selectable per-entity child
leaf under `"GameView"` per real draw call it issued that frame." (do not
restructure the paragraph), e.g.:

> ...PLUS one real, individually selectable per-entity child leaf under
> `"GameView"` per real draw call it issued that frame, including the Sky
> Background pass itself (`frame-debugger-8` campaign) - never only meshes.

Keep this to a single added clause - `AGENTS.md` is deliberately a short
summary-plus-link document; the full story lives in
`docs/conventions/frame-debugger.md` (Step 3.1 above).

### 3.3 — Full build, BOTH `GTE_ENABLE_EDITOR` configurations (the ONLY phase in this campaign allowed to do this)

```
cmake --build build
```

Working directory: `C:\Users\F5954\Documents\TAMANA\GreatTamanaEngine`. Fix
any compile error found here before proceeding (should be none if PHASE1-3
each already did their own incremental compile check honestly).

**v2 addendum - also do a full build of the pre-existing `build-editor-off`
directory** (`-DGTE_ENABLE_EDITOR=OFF`), matching every prior
`frame-debugger-N` campaign's own `CAMPAIGN_COMPLETION_REPORT.md` "full clean
build (both `GTE_ENABLE_EDITOR` configs)" convention (see e.g.
`task_manager/frame-debugger-7/CAMPAIGN_COMPLETION_REPORT.md`) - this
campaign touched `src/Application/RenderPasses.cpp`, a CORE, always-compiled
file, in both PHASE1 and PHASE2, so this configuration must be proven to
still compile and link cleanly, not just the default `GTE_ENABLE_EDITOR=ON`
`build` directory:

```
cmake --build build-editor-off
```

No fresh `cmake` configure step is needed for either directory (both already
exist, pre-configured) - a plain incremental `--build` of each is enough,
since PHASE1/PHASE2/PHASE3 should already have left both in a working state.

Then run the regression test suite (against the default `build` directory -
`build-editor-off` has no test binary of its own that exercises anything this
campaign touched beyond what its own compile+link success already proves,
matching PHASE1/PHASE2's own "no need to run its tests" note):

```
cd /d C:\Users\F5954\Documents\TAMANA\GreatTamanaEngine\build && ctest -C Debug --output-on-failure
```

Every test added across PHASE1/PHASE3 must show as passing, and zero
pre-existing test may newly fail. If anything regresses, diagnose it and fix
it as part of this same phase (do not leave the campaign in a red state) -
per this task's own top-level rule, only escalate to a brand-new
`delegate_task` if the root cause turns out to live in a phase already
reported "done" and needs materially more investigation than a quick,
confident, in-place fix here.

### 3.4 — Live, HTTP-driven, screenshot-verified smoke test

Mirrors every prior `frame-debugger-N` campaign's own closing verification
style (see `docs/conventions/frame-debugger.md`'s own "Testing this feature"
section for the precedent). Using `run_app_background` +
`gte_send_request`/`stop_app_background`:

1. Launch the engine, load a scene containing at least one mesh entity, plus
   the exact empty-scene case too if convenient (Camera + Directional Light
   only) - both are part of this campaign's own Definition of Done.
2. `GET /frame_debugger/open`, `GET /frame_debugger/enable?value=true`.
3. `GET /frame_debugger/state` - confirm `totalEventCount` grew by exactly
   one versus what it would have been before this campaign (one new sky
   leaf).
4. `GET /frame_debugger/select_event?index=<the new sky leaf's own index>`,
   then `GET /get_swapchain` - visually confirm (via `load_image` on the
   resulting capture, or directly via the returned image) that:
   - the Inspector shows `Pass = GameView (Sky Draw)`,
     `Shader = AtmosphereSkyBackground.vert/AtmosphereSkyBackground.frag`,
     `ZTest = Equal`, `ZWrite = Off`.
   - the preview box shows the full, correct sky (and every real entity
     already drawn before it).
5. `GET /frame_debugger/select_event?index=<the LAST real entity leaf's own
   index>`, then `GET /get_swapchain` again - visually confirm the preview
   box now shows that entity WITHOUT the sky (a real, visible difference
   from step 4 - this is the direct, final, live proof the PHASE2 bug fix
   actually works, not just its unit-level reasoning).
6. Repeat steps 2-4 against the empty scene (Camera + Directional Light
   only) - confirm exactly one leaf exists under `"GameView"` (the sky
   leaf alone) with a correct, sky-only preview image.
7. `stop_app_background` when done.

Record the exact requests/responses (or a faithful text description of each
screenshot) in `CAMPAIGN_COMPLETION_REPORT.md` (Step 3.5).

### 3.5 — `CAMPAIGN_COMPLETION_REPORT.md`

Write this file in `task_manager/frame-debugger-8/`, mirroring the exact
structure of `task_manager/frame-debugger-7/CAMPAIGN_COMPLETION_REPORT.md`
(read that file first as a template): a short recap of the original problem,
one paragraph per phase summarizing what actually shipped, the full build
(BOTH `GTE_ENABLE_EDITOR` configs) + `ctest` result, and the
live-verification findings from Step 3.4 above.

## Definition of Done (this phase, and the whole campaign)

- `docs/conventions/frame-debugger.md` (BOTH its updated "What is real today"
  section AND its new "What's new (`frame-debugger-8` campaign)" section) and
  `AGENTS.md` are all updated per Step 3.1/3.2.
- `cmake --build build` succeeds cleanly.
- `cmake --build build-editor-off` succeeds cleanly (v2 addendum).
- `ctest -C Debug --output-on-failure` shows 100% pass, including every new
  test from PHASE1/PHASE3.
- The live smoke test (Step 3.4) confirms every bullet in
  `PHASE0_MASTER_STRATEGY.md`'s own campaign-wide Definition of Done.
- `CAMPAIGN_COMPLETION_REPORT.md` exists and is accurate.

## Report

`git_add`/`git_commit` everything (docs, `CAMPAIGN_COMPLETION_REPORT.md`, and
this phase's own `PHASE4_COMPLETION_REPORT.md` if you choose to keep the two
separate, mirroring `frame-debugger-7`'s own precedent of a per-phase report
PLUS a final campaign-wide report).
