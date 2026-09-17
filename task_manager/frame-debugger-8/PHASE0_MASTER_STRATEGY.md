# PHASE0 — Master Strategy: "Sky Background" is invisible in the Frame Debugger

_Orchestrator document for the `frame-debugger-8` campaign. Every child phase
document (`PHASE1_*.md` .. `PHASE4_*.md`) in this same folder reports back to
this file. Read this file FIRST, always, before opening any child phase._

## Step 1 — The Goal (Where are we going?)

When a user opens the Frame Debugger on a scene containing a `SmokeTestCube`
and a `terrain` entity, the event tree currently shows:

```
Game View
  |-- Compute Dispatches (Pre-GameView)
  |     |-- AtmosphereTransmittanceLutPass
  |     |-- AtmosphereMultiScatteringLutPass
  |     |-- AtmosphereSkyViewLutPass
  |     |-- AtmosphereAerialPerspectiveVolumePass
  |     |-- AtmosphereAerialPerspectiveVolumeDebugSlicePass
  |-- GameView
  |     |-- SmokeTestCube (Entity 0)
  |     |-- Entity 2 (Entity 2)              <- this is "terrain"
  |-- Compute Dispatches (Post-GameView)
        |-- AtmosphereAerialPerspectiveCompositePass
```

There is NO row anywhere for the Sky Background draw, even though it
genuinely runs every frame, and even though clicking `Entity 2 (Entity 2)`
(the LAST object drawn) already visibly shows the full sky/mountain
background in its preview image (confirmed live, via the two screenshots the
user attached to this task).

**The goal of this campaign**: give the Sky Background pass a REAL,
individually selectable tree row/leaf under `GameView` - exactly one, real,
honestly-labeled row, positioned at its own true point in execution order -
with its own correct Shader/Pass/Blend/Z/Stencil inspector data and its own
correct, accurate "Game View accumulated as of this exact step" preview
image, using the SAME real capture/replay machinery every other leaf in this
tree already uses (never a fabricated data source, never a hardcoded
cosmetic label).

A second, closely-related, CONFIRMED bug found while investigating (and
independently spotted by the user in the two screenshots) is fixed as part of
this same campaign: today, the LAST object's own per-step preview image
secretly ALREADY includes the sky rendered on top of it, so there is no way
to see "Game View immediately after the last object, before sky" as a
distinct state from "Game View after sky". This campaign makes every single
step's preview image exactly, individually correct.

## Step 2 — The Situation (Where are we now? — full root-cause trail)

All of the following was CONFIRMED by direct code reading (`read_file`/
`read_line`/`search_in_dir`), not guessed:

1. **The Sky Background pass is a REAL, direct Vulkan graphics draw call -
   never a compute dispatch, never a blit.**
   `src/Renderer/Atmosphere/AtmosphereSkyBackgroundRenderer.cpp`'s `Draw()`
   method issues exactly one `vkCmdDraw(cmd, 3, 1, 0, 0)` (a synthesized,
   vertex-buffer-less full-screen triangle - see `AtmosphereSkyBackground.vert`),
   against its OWN hand-built `VkPipeline`/`VkPipelineLayout`
   (`EnsurePipeline()`), loaded from `shaders/AtmosphereSkyBackground.vert.spv`
   / `shaders/AtmosphereSkyBackground.frag.spv`. This mirrors
   `src/Editor/SceneGridRenderer.h/.cpp`'s own established pattern exactly
   (bypasses `Renderer::CreatePipeline()`/`Renderer::Submit()` entirely) - it
   is a DELIBERATE, documented, existing engineering pattern in this codebase
   for a full-screen-triangle effect, not a mistake to "fix" by rerouting it
   through the mesh pipeline. **(This directly answers the user's own Q2/Q4
   questions: it is neither a compute dispatch nor a blit, and yes, it
   deliberately does NOT go through the engine's normal
   `Renderer::CreatePipeline()`/`Pipeline` abstraction - by design, the same
   as `SceneGridRenderer`.)**

2. **Its real depth-test state is genuinely DIFFERENT from every mesh in this
   engine**, confirmed directly in `AtmosphereSkyBackgroundRenderer.cpp`'s
   `EnsurePipeline()`:
   - `depthStencil.depthTestEnable = VK_TRUE`, `depthCompareOp =
     VK_COMPARE_OP_EQUAL` (NOT `VK_COMPARE_OP_LESS`, which every mesh uses -
     see `Pipeline.cpp`) - it only ever survives at a pixel whose depth is
     STILL exactly the frame's own clear value (1.0), i.e. a pixel nothing
     real was drawn at yet.
   - `depthStencil.depthWriteEnable = VK_FALSE` (every mesh uses `VK_TRUE`).
   - `colorBlendAttachment.blendEnable = VK_FALSE`, `rasterizer.cullMode =
     VK_CULL_MODE_NONE`, `stencilTestEnable = VK_FALSE` - same as every mesh.

3. **The REAL execution order, confirmed in
   `src/Application/RenderPasses.cpp`'s `AddGameViewPass()`:**
   ```cpp
   [&game, &renderer, aspectWidthOverHeight, recordSkyBackground, frameDebuggerCapture](rg::PassContext& ctx) {
       renderer.BeginGraphPassRecording(ctx.cmd, ctx.recordDraw);
       game.Render(renderer, aspectWidthOverHeight, nullptr, frameDebuggerCapture); // every ECS entity, in order
       renderer.EndGraphPassRecording();
       if (recordSkyBackground) {
           recordSkyBackground(ctx.cmd); // Sky Background - drawn LAST, after every entity
       }
   }
   ```
   Sky is drawn LAST, after every entity, inside the SAME `"GameView"` render
   pass / command-buffer scope - a deliberate, real performance optimization
   (draw opaque geometry first, then let the sky's own `EQUAL` depth test
   cheaply skip every pixel a mesh already covered, instead of the sky
   painting the whole screen and then having meshes overdraw most of it).
   **This is why, in the user's own screenshot, selecting `Entity 2` (the
   LAST object) already shows the sky: the current, buggy replay-preview
   mechanism (see point 5 below) bundles the sky draw into that SAME replay
   step.** The REAL per-frame order (LUTs -> SmokeTestCube -> terrain -> sky
   -> Aerial Perspective composite) is correct, efficient, and is being kept
   as-is by this campaign - see "Locked Design Decision 1" below.

4. **The root cause of the invisibility itself**: every per-ENTITY draw is
   captured via `RenderSystem::Draw()`'s own existing call to
   `FrameDebuggerCaptureContext::RecordEntityDraw()` (see
   `src/Editor/FrameDebuggerCapture.h`). The Sky Background draw does NOT go
   through `RenderSystem::Draw()` at all (point 1 above) - it is invoked
   directly as a bare `std::function<void(VkCommandBuffer)>` callback
   (`recordSkyBackground`, built by
   `src/Application/AtmospherePassSequence.cpp`'s
   `MakeRecordSkyBackgroundCallback()`) with ZERO Frame-Debugger
   instrumentation anywhere on its path. It is a real, silent, structural
   gap - not a rendering bug, a CAPTURE/OBSERVABILITY bug.

5. **The second, confirmed bug** (`AddFrameDebuggerReplayPasses()`,
   `src/Application/RenderPasses.cpp`, lines ~139-237): this function
   declares exactly `objectCount` (one per real entity) debug-only replay
   passes, each redrawing objects `[0..i]` from scratch. Its own existing
   code comment admits the bug outright:
   ```cpp
   // Only the VERY LAST replay pass also draws the sky
   // background, exactly mirroring AddGameViewPass()'s own
   // real ordering (sky is drawn AFTER every object, once,
   // not per-object) - this makes replay step N-1's own image
   // pixel-identical to the existing entry.preview snapshot ...
   if (i + 1 == objectCount && recordSkyBackground) {
       recordSkyBackground(ctx.cmd);
   }
   ```
   So today, selecting the LAST entity's own leaf (`Entity 2` in the user's
   screenshot) shows an image that secretly already includes the sky -
   there is no way to see "right after the last object, before sky" as its
   own distinct, correct state.

6. **A related, but explicitly OUT-OF-SCOPE, discovery**: the parent
   `"GameView"` leaf's own aggregate "Draw Stats (Calls, Tris)" row is
   sourced from `RenderGraphPassSnapshot::stats.drawStats`
   (`FrameDebuggerData.cpp`'s `BuildGameViewLeaf()`), which is only ever fed
   by `Renderer::Submit()`'s own draw-call bookkeeping (`ctx.recordDraw`,
   `src/Renderer/RenderGraph/RenderGraph.h`). Since the sky's own
   `vkCmdDraw()` call happens OUTSIDE any `Renderer::Submit()` call (and
   even outside the `BeginGraphPassRecording()`/`EndGraphPassRecording()`
   bracket - see point 3's code snippet), it is NOT counted in that
   aggregate today, and will STILL not be counted after this campaign (see
   "What We Will NOT Do" below) - this is a pre-existing, narrow,
   documented gap this campaign deliberately does not fix, to keep this
   campaign's blast radius tight. It does not affect correctness of
   anything this campaign DOES fix.

## Step 3 — The Plan (concrete phases)

Four phases, in strict dependency order. Every phase ends with a real,
compiling code change plus a quick incremental build/compile check (never a
full build - see Phase 4 for the one, final full build).

| Phase | File(s) touched | What it does |
|---|---|---|
| **PHASE1** | `src/Editor/FrameDebuggerCapture.h/.cpp`, `src/Renderer/Atmosphere/AtmosphereSkyBackgroundRenderer.h`, `src/Application/RenderPasses.cpp`, `src/Game/Game.h` (doc-comment-only), `tests/Editor/FrameDebuggerCaptureTests.cpp` | Adds the missing CAPTURE instrumentation - a new `FrameDebuggerCaptureContext::RecordSkyBackgroundDraw()`, a new `isSkyBackgroundDraw` flag on `FrameDebuggerDrawRecord`, a new `DescribeSkyBackgroundPipelineState()` (the sky's own REAL Equal/Off depth state), a new real, permanent shader-name identifier on `AtmosphereSkyBackgroundRenderer`, the one, real call site that wires it all together in `AddGameViewPass()`, and a stale-doc-comment fix (v2 addendum, see below). |
| **PHASE2** (HEAVY - gets its own dedicated double-check pass, see below) | `src/Application/RenderPasses.cpp` (`AddFrameDebuggerReplayPasses()`), `src/Editor/FrameDebuggerCapture.h`, `src/Editor/FrameDebuggerHistory.h` (doc-comment-only) | Fixes the "last object secretly already includes sky" bug: stops ever drawing sky inside a per-object replay step, and adds exactly ONE new, dedicated "sky step" replay pass so every step's own accumulated preview image is genuinely, individually correct. |
| **PHASE3** | `src/Editor/FrameDebuggerData.h/.cpp` (`BuildGameViewDrawRecordLeaf()`), `tests/Editor/FrameDebuggerDataTests.cpp`, `tests/Editor/FrameDebuggerSnapshotBuilderTests.cpp` | Makes the new sky draw record actually show up as its own distinct, correctly-labeled tree leaf (real shader name, real distinct pass name, its own real blend/Z/stencil rows), wired into the existing, unchanged `BuildRealFrameDebuggerSnapshot()` loop with zero structural changes to that function. |
| **PHASE4** | `docs/conventions/frame-debugger.md`, `AGENTS.md`, full build (BOTH `GTE_ENABLE_EDITOR` configs), live HTTP-driven screenshot verification, `CAMPAIGN_COMPLETION_REPORT.md` | Documents the fix (mirroring every prior `frame-debugger-N` campaign's own doc convention, including updating the EXISTING "What is real today" section, not just appending a new "What's new" one - v2 addendum, see below), does the one full build + live smoke test this whole campaign earns, and closes out the campaign. |

### Cross-phase invariant (read this before touching ANY phase)

`FrameDebuggerCaptureContext::DrawRecords()` (populated during the REAL
`"GameView"` pass's execution) and `AddFrameDebuggerReplayPasses()`'s own
replay-destination ordering (populated during the DEBUG-ONLY replay passes'
execution, same frame) MUST stay in the exact same order:

```
index 0            .. index N-1        : one per real ECS entity, in the
                                          same order Game::Render() draws them
index N (only when
 a sky callback
 exists this frame) : the Sky Background draw, always LAST
```

`FrameDebuggerData.cpp`'s `BuildRealFrameDebuggerSnapshot()` already assigns
`stepPreviewIndex` as `0, 1, 2, ...` by walking `capture.DrawRecords()` in
order (unchanged code, see PHASE3) - so as long as PHASE1's capture-side
`RecordSkyBackgroundDraw()` call happens strictly AFTER every entity's
`RecordEntityDraw()` call this frame (it does - see Step 2, point 3), and
PHASE2's replay-pass loop builds its own destinations in the exact same
"entities first, sky last" order, everything lines up automatically with
ZERO index-plumbing changes required in PHASE3's own snapshot builder. This
is the single most important invariant to keep straight across PHASE1/PHASE2
- re-read this paragraph before starting either.

### v2 addendum — additional findings from this campaign's own second-pass double-check (read this too)

This section was added when the whole `frame-debugger-8` strategy was
re-reviewed a second time, against the REAL, current source code (never just
the first pass's own reasoning, and never just this campaign's own `.md`
files in isolation - `src/Editor/FrameDebuggerCapture.h/.cpp`,
`src/Editor/FrameDebuggerData.h/.cpp`, `src/Editor/FrameDebuggerHistory.h`,
`src/Application/RenderPasses.h/.cpp`, `src/Game/Game.h`, `CMakeLists.txt`,
and every existing Frame Debugger test file were all re-read fresh). Two real,
concrete gaps were found; both are now folded into PHASE1/PHASE2/PHASE3/
PHASE4's own instructions below (look for "v2 addendum" markers in each phase
file) rather than left here as unassigned commentary:

1. **Every phase touching `src/Application/RenderPasses.cpp` must ALSO
   confirm the pre-existing `build-editor-off` directory
   (`-DGTE_ENABLE_EDITOR=OFF`) still compiles - not only the final PHASE4 full
   build.** This is not optional caution: EVERY single prior
   `frame-debugger-N` campaign (1 through 7) explicitly built
   `build-editor-off` at multiple phases along the way, never only at the
   very end - see e.g.
   `task_manager/frame-debugger-3/PHASE1_RENDERER_CAPTURE_INSTRUMENTATION.md`'s
   own "a build directory, e.g. `build-editor-off`, is fine" note, and
   `task_manager/frame-debugger-7/PHASE3_COMPLETION_REPORT.md`'s own
   `cmake --build build-editor-off --target gte_core` step. This campaign's
   own PHASE1 adds a brand-new `#if GTE_ENABLE_EDITOR` guarded dereference of
   `frameDebuggerCapture` inside `src/Application/RenderPasses.cpp` - a CORE,
   always-compiled file that must compile AND LINK cleanly with
   `GTE_ENABLE_EDITOR=OFF` too (see `FrameDebuggerCapture.h`'s own header
   comment: "a `GTE_ENABLE_EDITOR=OFF` build would compile fine... but FAIL TO
   LINK" if this rule is ever violated) - exactly the failure mode this
   convention exists to catch. PHASE1's own "Compile check for this phase"
   section now includes an explicit `build-editor-off` step; PHASE2 (also
   touching `RenderPasses.cpp`) does too, as cheap extra insurance; PHASE3
   does NOT need one (`FrameDebuggerData.cpp` only ever compiles under
   `GTE_ENABLE_EDITOR=ON` - see the root `CMakeLists.txt`'s own
   `if(GTE_ENABLE_EDITOR)` block around its file list - so it structurally
   cannot affect the OFF configuration at all); PHASE4's own final full build
   now explicitly rebuilds BOTH `build` and `build-editor-off`, matching every
   prior campaign's own `CAMPAIGN_COMPLETION_REPORT.md` "full clean build
   (both `GTE_ENABLE_EDITOR` configs)" convention.
2. **Four small, real, pre-existing doc-comment/prose staleness issues this
   campaign's own code changes would otherwise silently introduce** - each now
   has its own concrete fix folded into the relevant phase below:
   - `src/Game/Game.h`'s `CountGameViewDrawCommandsThisFrame()` doc comment
     currently claims `objectCount == capture.DrawRecords().size()` "holds in
     practice" - true today, but FALSE the moment PHASE1 lands (`DrawRecords()`
     gains one extra, non-entity sky record whenever a sky callback exists) -
     fixed in PHASE1 (its own Step 3.6).
   - `src/Editor/FrameDebuggerCapture.h`'s `SetReplayStepPreviews()`/
     `ReplayStepPreviews()` doc comments, and
     `src/Editor/FrameDebuggerHistory.h`'s `perObjectStepPreviews` field doc
     comment, both currently describe this vector as strictly "one [entry] per
     real object" - no longer the complete picture once PHASE2 adds the one
     extra, dedicated sky step to it - fixed in PHASE2 (its own Step 3.4).
   - The existing doc comment directly above `BuildGameViewDrawRecordLeaf()`
     in `FrameDebuggerData.cpp` still only describes the (unchanged) per-entity
     leaf shape, not the new sky-leaf branch this campaign adds alongside it -
     fixed in PHASE3 (its own Step 3.5).
   - `docs/conventions/frame-debugger.md`'s own "What is real today" section
     (not just a brand-new "What's new (`frame-debugger-8` campaign)" section)
     needs updating in two places, mirroring the EXACT precedent
     `frame-debugger-6` already set when it updated that same section in place
     for its own per-entity-leaf change: its own ASCII tree diagram (shows no
     `"GameView"` child leaf for the sky at all today) and its "this engine has
     exactly ONE Pipeline configuration today... nothing to fabricate per-mesh
     here" claim (no longer accurate once the Sky Background leaf's own
     genuinely different Equal/Off state exists) - fixed in PHASE4 (its own
     Step 3.1).

### Locked Design Decisions (do not re-litigate these mid-implementation)

1. **The real GPU draw order does NOT change.** Sky stays drawn LAST (after
   every entity), using its existing `EQUAL`-depth-test optimization. This
   was an explicit question put to the user; their own answer ("look at the
   terrain step... it looks like the engine draws terrain and sky at the
   same time, please double check") confirmed they were pointing at the
   REPLAY-PREVIEW bug (Step 2, point 5), not asking for a real reordering.
   Only the DEBUGGER's own visibility of this existing, correct order is
   being fixed.
2. **The new leaf's identifying name/shaderName is a REAL, hand-verified
   fact - the actual `AtmosphereSkyBackground.vert`/`AtmosphereSkyBackground.frag`
   shader file pair the pass genuinely loads - never an invented cosmetic
   label like `"Sky Background"`.** This directly follows the user's own
   explicit instruction ("do not use naming sky background internally, it
   must derived by compute shader name, or render pass name") and matches
   this codebase's own established, hard-learned lesson: the
   `frame-debugger-5` campaign explicitly REMOVED the old hardcoded
   "GPU Skinning"/"Aerial Perspective Composite" cosmetic-label special
   cases for exactly this reason (see `docs/conventions/frame-debugger.md`'s
   own "Known limitation, now fixed (frame-debugger-5 campaign)" section).
   The tree row's `passName` field additionally reads `"GameView (Sky
   Draw)"` (mirroring the existing, pre-approved `"GameView (Entity Draw)"`
   convention for per-entity leaves) - a real, structural fact (which real
   pass this draw happened inside), never a second cosmetic label.
3. **Scope is Game View ONLY - the Frame Debugger tool's own existing,
   permanent rule** (`docs/conventions/frame-debugger.md`: "Scope is Game
   View ONLY, permanently"). The user's own clarification ("game and scene
   view should follow same render pipeline... the core rendering is same
   for both panel") is CORRECT and is not in conflict with this: the
   underlying `AtmosphereSkyBackgroundRenderer::Draw()` function IS
   genuinely shared/identical for both views (same code, same real Vulkan
   pipeline) - only the Frame Debugger's own CAPTURE call site differs, and
   it only ever existed for the Game View pass (`AddGameViewPass()`) to
   begin with, never for `AddSceneViewPass()`. This campaign adds the new
   `RecordSkyBackgroundDraw()` call to `AddGameViewPass()` only - `
   AddSceneViewPass()` is never touched.
4. **The sky leaf's own blend/Z/stencil inspector rows show its REAL,
   distinct pipeline state** (`Depth Test = Equal`, `Depth Write = Off`),
   never a copy-paste of the generic per-mesh `DescribeStandardPipelineState()`
   (`Depth Test = Less`, `Depth Write = On`) - this was an explicit question
   put to the user, who confirmed wanting the real, distinct values shown.
5. **`FrameDebuggerDrawRecord::isSkyBackgroundDraw` is appended at the END of
   that struct's field list, never inserted in the middle** - mirrors this
   same struct's own pre-existing "never insert a field in the middle of a
   struct some call site might positionally aggregate-initialize" precedent
   (see `FrameDebuggerEventDetails::eventLabel`'s own identical comment,
   `FrameDebuggerData.h`).
6. **`RecordSkyBackgroundDraw()` internally reuses `RecordDraw()`'s own
   existing dedup/draw-call-count/last-view-projection bookkeeping** (never
   a second, parallel, hand-rolled copy of that logic) - see PHASE1 for the
   exact code. This is what makes the PARENT `"GameView"` leaf's own
   aggregate `shaderName` (built by joining `capture.PipelineDebugNames()`)
   correctly include the sky's own real shader pair too, alongside whatever
   mesh shaders ran that frame.

### What We Will NOT Do (explicit non-goals)

- **Not fixing the `"GameView"` leaf's own aggregate "Draw Stats (Calls,
  Tris)" undercount-by-one** (Step 2, point 6 above) - this is a real,
  narrow, pre-existing, DIFFERENT gap (`RenderGraphPassSnapshot::stats`
  never counted the sky's raw `vkCmdDraw()` call, before or after this
  campaign) that is out of scope here. Left as a documented, known gap in
  PHASE4's doc update - not silently ignored, just not fixed by this
  campaign.
- **Not adding a "Sky-View LUT" texture-read row** to the new leaf's
  Textures list. This would be a genuinely nice-to-have (the sky's own
  fragment shader really does sample a texture named e.g.
  `"AtmosphereSkyViewLut_GameView"`), but doing it honestly would require
  threading a brand-new string parameter all the way from
  `Application.cpp`'s build lambda, through `AddGameViewPass()`'s own
  signature, purely to serve one inspector row - a disproportionate blast
  radius for this campaign's actual goal. Explicitly deferred, not
  forgotten - a clean, well-isolated future addition if ever wanted.
- **Not reordering the real GPU draw sequence** - see Locked Design
  Decision 1.
- **Not touching Scene View** - see Locked Design Decision 3.
- **Not adding a multi-frame history, a breakpoint/stop mechanism, or
  anything else already explicitly out of scope per
  `docs/conventions/frame-debugger.md`'s own "Still-deferred future work"
  section** - this campaign is scoped narrowly to the Sky Background
  visibility gap and its one closely-related replay-preview bug.

### Definition of Done (whole campaign)

- A live capture (Enable -> auto-capture, or explicit Capture) on a scene
  with at least one mesh entity shows a NEW, real, selectable leaf under
  `GameView`, positioned AFTER every entity leaf, named after the real
  `AtmosphereSkyBackground.vert/AtmosphereSkyBackground.frag` shader pair.
- Selecting that new leaf shows: its own real `passName` (`"GameView (Sky
  Draw)"`), its own real `shaderName` (the same shader pair), its own real,
  DISTINCT blend/Z/stencil rows (`Equal`/`Off`, not `Less`/`On`), and a
  preview image that is the true, accumulated Game View exactly as it looked
  after every entity AND the sky were drawn (pixel-identical to the
  `"GameView"` leaf's own pre-composite preview).
- Selecting the LAST entity's own leaf (e.g. `terrain (Entity 2)`) now shows
  the accumulated image WITHOUT the sky - a real, visible difference versus
  selecting the new Sky Background leaf immediately after it in the tree.
- A capture on a scene with ZERO mesh entities (e.g. Camera + Directional
  Light only - the exact scenario from the earlier
  `atmosphere-scattering-4` bug report) still shows exactly one leaf under
  `GameView`: the Sky Background leaf, with a correct preview image (a pure
  sky, no entities).
- Every new piece of pure logic (the new capture method, the new pipeline-
  state describer, the new leaf builder branch) has a passing Tier-1 test in
  the same style as its neighbors.
- Every stale doc-comment identified in this campaign's own "v2 addendum"
  above has been corrected (Game.h, FrameDebuggerCapture.h,
  FrameDebuggerHistory.h, FrameDebuggerData.cpp,
  docs/conventions/frame-debugger.md's "What is real today" section).
- Both `cmake --build build` AND `cmake --build build-editor-off` succeed at
  the very end of PHASE4, and a live, HTTP-driven, screenshot-verified smoke
  test confirms every bullet above against the real running engine.

### Delegation plan for THIS campaign's own follow-up automation

Per this task's own top-level instructions: PHASE2 above is flagged HEAVY
(it is the single riskiest, most surgical change - restructuring an existing,
already-subtle render-graph-declaration function with a real, confirmed
history of getting this exact kind of ordering/culling bug wrong before, per
`docs/conventions/frame-debugger.md`'s own `frame-debugger-7` "two real bugs
were found... during this campaign's own mandatory visual spot-check"
section) - it gets its OWN dedicated double-check pass before the full,
whole-campaign double-check pass runs. See the very end of this campaign's
own orchestration flow (outside these `.md` files - already arranged as
separate `delegate_task` calls) for the exact order: PHASE2-focused
double-check -> full-campaign double-check (which itself fans out into one
real implementation `delegate_task` per phase file below).
