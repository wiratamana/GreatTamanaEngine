# CAMPAIGN_COMPLETION_REPORT — Render Pass System (`render-pass-1`)

_Final record of the whole `render-pass-1` campaign, branch
`feature/render-pass-impl`. Mirrors the shape of
`task_manager/render_graphs/RENDERGRAPH_CAMPAIGN_COMPLETION_REPORT.md`. Written
at the close of PHASE7 (`PHASE7_FINAL_INTEGRATION_FULL_BUILD_AND_LIVE_VERIFICATION.md`),
the only phase in this campaign permitted a full build + full `ctest`
regression run + live HTTP-driven verification against the real running
engine._

## Why this campaign existed

Before this campaign, every render/compute/blit operation in this engine —
every Atmosphere LUT compute dispatch, the Sky Background draw, the
`"GameView"` mesh-drawing pass, the Aerial Perspective composite, GPU
Skinning, Present, the Frame Debugger's own replay passes, Compute Blur
Validation — was declared as an ad-hoc free function building two raw lambdas
and throwing them directly at `RenderGraphBuilder::AddPass()`/
`AddComputePass()`, scattered across `src/Application/RenderPasses.cpp`,
`src/Application/AtmospherePassSequence.cpp`, and
`src/Renderer/Atmosphere/AtmosphereLutRenderer.cpp`, with zero shared
structure beyond "eventually calls `builder.AddPass(name, ...)`". Worse, the
Sky Background draw was not even its own pass — it was hand-fused inside the
`"GameView"` pass's own `execute` lambda, made visible to the Frame Debugger
only via a hardcoded `isSkyBackgroundDraw`/`RecordSkyBackgroundDraw()` special
case. This was exactly the "naive render pass" design the user asked this
campaign to fix — see `PHASE0_MASTER_STRATEGY.md`'s own "Step 1"/"Step 2" for
the full original diagnosis, confirmed by direct source inspection before any
phase began.

The Render Graph orchestrator itself (`RenderGraphBuilder`/
`RenderGraphCompiler`/`RenderGraphBarrierPlanner`/`RenderGraph`, the product of
the separate, earlier `render_graphs` 9-phase campaign) was explicitly
out of scope to rewrite — this campaign only replaced the "Render Pass"
declaration layer sitting on top of it.

## The seven phases, in one line each

| # | Phase | One-line outcome |
|---|-------|-------------------|
| 1 | Render Pass Core Abstraction | New `PassKind` (`Graphics`/`Compute`, replacing the old plain `bool isComputePass`) and `RenderPassCategory` (`General`/`AtmosphereLut`/`GpuSkinning`/`Debug`) vocabulary in `RenderGraphTypes.h`, plus the one new, official chokepoint, `RenderGraphBuilder::AddRenderPass()` — thin, lambda-based, built directly on the pre-existing `AddPass()`/`AddComputePass()`, no polymorphic pass hierarchy. Nothing in production yet calls it. |
| 2 | Render Opaque/Sky Split + Transparent Stub | The old monolithic `"GameView"` pass (which drew every mesh AND hand-fused the sky background into the same `vkCmdBeginRendering` bracket) split into three real, separate, back-to-back passes on the same render target: `"RenderOpaque"` (renamed from `AddGameViewPass()`), a brand-new `"DrawSkyBackground"` (LOADs both attachments, relies on `AtmosphereSkyBackgroundRenderer`'s own `EQUAL`-depth-test pipeline), and a brand-new, permanently-empty `"RenderTransparent"` scaffold (`RenderSystem::CollectTransparentRenderables()` always returns `{}` today — no transparency concept exists on `MeshRenderer` yet). |
| 3 | Atmosphere Passes Migration | All six Atmosphere compute pass declarations in `AtmosphereLutRenderer.cpp` (Transmittance/MultiScattering/SkyView/AerialPerspectiveVolume/AerialPerspectiveVolumeDebugSlice/AerialPerspectiveComposite LUTs) migrated onto `AddRenderPass()`, the five LUT passes tagged `RenderPassCategory::AtmosphereLut`, the Composite pass tagged `General`. Pure metadata migration — zero change to any pass's name/reads/writes/execute behavior. |
| 4 | Frame Debugger Generic Tree Rework | `BuildRealFrameDebuggerSnapshot()` (`FrameDebuggerData.cpp`) rewritten to build the whole event tree GENERICALLY from real `PassKind`/`RenderPassCategory`/`ViewScope`/execution order — the hardcoded `"GameView"`-literal search is gone (replaced by a single `"RenderOpaque"` pivot lookup, the only pass-name string literal left in the function), and the `isSkyBackgroundDraw`/`RecordSkyBackgroundDraw()` hack is deleted outright. `"DrawSkyBackground"` becomes its own real, generically-discovered, individually selectable leaf. |
| 5 | Remaining Passes Migration | GPU Skinning (`AddGpuSkinningPasses()`, tagged `GpuSkinning`), `"Present"` (tagged `General`), and Compute Blur Validation (tagged `Debug`) migrated onto `AddRenderPass()`. `AddFrameDebuggerReplayPasses()` confirmed already migrated by PHASE4's own pulled-forward fix — nothing to redo. A full `src/`-wide grep audit confirmed only two permanent, expected `builder.AddPass()` survivors: `AddSceneViewPass()`'s own `"SceneView"` pass (explicitly out of scope for the whole campaign) and one unrelated-method-name false-positive grep hit in `ImGuiEditorLayer.cpp`. |
| 6 | Application Orchestration Cleanup + Docs | Stale present-tense doc comments in `Application.cpp`/`RenderPasses.h`/`TODO.md` that still claimed a pass literally named `"GameView"` exists were corrected (texture/render-target/enum/variable names of the same literal string were correctly left untouched). `docs/conventions/frame-debugger.md` and `AGENTS.md` rewritten to accurately describe the shipped `"Compute LUT"`/`"RenderOpaque"`/`"DrawSkyBackground"`/`"RenderTransparent"` tree shape and the `AddRenderPass()`/`PassKind`/`RenderPassCategory` vocabulary. Confirmed (via independent `search_in_dir` sweep) that `AddGameViewPass()`/`isSkyBackgroundDraw`/`RecordSkyBackgroundDraw()` were already fully deleted by PHASE2/PHASE4 — nothing left to clean up in code. |
| 7 | Final Integration: Full Build, Full Regression, Live Verification | This phase. Full clean-equivalent build, full `ctest` regression suite, and a live, HTTP-driven Frame Debugger screenshot verification against the real running engine — see below. |

## Final, shipped tree shape (verified live in PHASE7)

```
"Game View"
  |-- "Compute LUT"                          (AtmosphereTransmittanceLutPass, AtmosphereMultiScatteringLutPass,
  |                                            AtmosphereSkyViewLutPass, AtmosphereAerialPerspectiveVolumePass,
  |                                            AtmosphereAerialPerspectiveVolumeDebugSlicePass)
  |-- "Compute Dispatches (Pre-GameView)"    (GPU Skinning — only present if it ran that frame; absent in this
  |                                            verification run, since no skinned entity was in the scene)
  |-- "RenderOpaque"                          (per-entity children: SmokeTestCube (Entity 1), Entity 2 (Entity 2))
  |-- "DrawSkyBackground"                     (real, separate, selectable leaf — no more hack)
  |-- "RenderTransparent"                     (correctly absent — still a real no-op scaffold today)
  |-- "Compute Dispatches (Post-GameView)"   (AtmosphereAerialPerspectiveCompositePass)
```

This matches `PHASE0_MASTER_STRATEGY.md`'s own Step 1 target diagram exactly,
including the "Compute LUT" passes staying separate/individually selectable
leaves grouped only under a clearer heading (Locked Design Decision #5), and
`"RenderTransparent"` correctly never appearing as a visible row since it
declares zero passes (Locked Design Decision #6).

## PHASE7 verification performed

### Full build

`cmake --build build` (working directory
`C:\Users\F5954\Documents\TAMANA\GreatTamanaEngine`) completed with
`ninja: no work to do` — every phase's own incremental compile check had
already kept the tree fully built and up to date at every step, so this full
build re-confirmed zero errors/warnings across the entire campaign's
accumulated changes with nothing left to recompile. No cross-phase
integration issue was found (the risk this phase's own strategy document
specifically exists to catch).

### Full regression test

`ctest -C Debug --output-on-failure` from `build/`: **1578 tests run, 100%
passing** (1577 passed outright, 1 test —
`PmxLoaderRealModelSmokeTest.LoadsAnMmdModelIfPresentOnThisMachine` —
correctly `GTEST_SKIP()`s, since it's gated on an optional, non-vendored MMD
model file not present on this machine; this is expected, pre-existing
behavior, unrelated to this campaign). Zero regressions found anywhere in the
suite as a result of this campaign's seven phases of changes.

### Live launch + Frame Debugger verification

1. Launched the built `GreatTamanaEngine.exe` in the background
   (`run_app_background`).
2. `GET /frame_debugger/open` — `200`, window opened.
3. `GET /frame_debugger/enable?value=true` — `200`, enabled.
4. `GET /frame_debugger/capture` — `200`, `hasCapturedFrame: true`,
   `totalEventCount: 8` (default scene: Camera + Directional Light only, no
   mesh entities yet).
5. `GET /get_swapchain` — screenshot confirmed the exact target tree shape:
   `"Compute LUT"` (5 real Atmosphere LUT leaves) → `"RenderOpaque"` (no
   children yet, an empty scene) → `"DrawSkyBackground"` → `"Compute
   Dispatches (Post-GameView)"` (`AtmosphereAerialPerspectiveCompositePass`).
   No `"RenderTransparent"` row, exactly as expected.
6. To also exercise `"RenderOpaque"`'s per-entity children exactly as the
   original task's reference screenshot showed, two primitives were spawned
   via `POST /instantiate_primitive` (`"SmokeTestCube"`, `"Entity 2"`), then
   re-captured. `GET /get_swapchain` now showed `totalEventCount: 10`, with
   `"RenderOpaque"` expanding to two real per-entity child leaves
   (`"SmokeTestCube (Entity 1)"`, `"Entity 2 (Entity 2)"`) — confirming the
   per-entity draw-record mechanism survived this whole campaign's rework
   unchanged.
7. `GET /frame_debugger/select_event?index=5` (the `"RenderOpaque"` row) +
   `GET /get_swapchain`: Inspector showed `Pass = RenderOpaque`, `Blend =
   Opaque (no blend)`, `ZTest = Less`, `ZWrite = On`, `Cull = None` — matching
   `DescribeStandardPipelineState()`'s documented values.
8. `GET /frame_debugger/select_event?index=8` (the `"DrawSkyBackground"`
   row) + `GET /get_swapchain`: Inspector showed `Pass = DrawSkyBackground`,
   `Blend = Opaque (no blend)`, `ZTest = Equal`, `ZWrite = Off`, `Cull = None`
   — matching `DescribeSkyBackgroundPipelineState()`'s documented values
   exactly, and visibly distinct from `"RenderOpaque"`'s own row above,
   confirming both leaves report real, correctly-distinct pipeline facts.
9. Stopped the engine (`stop_app_background`).

Every check in PHASE7's own Definition of Done passed. No bug reports were
filed during this campaign — every tool call across all seven phases behaved
as documented; where a live-smoke-test cleanup call returned an unrelated
`404` (a never-persisted test entity, deleted alongside process teardown
anyway) in earlier phases, this was correctly diagnosed as harmless and not a
tool malfunction, per each phase's own completion report.

## What shipped (cumulative)

- `PassKind` (`Graphics`/`Compute`) and `RenderPassCategory`
  (`General`/`AtmosphereLut`/`GpuSkinning`/`Debug`), both surviving into
  `RenderGraphPassSnapshot` for downstream consumption.
- `RenderGraphBuilder::AddRenderPass()` — the single, official chokepoint
  every pass declaration in this engine now goes through (two overloads: a
  4-argument default-`ViewScope::Shared`/`RenderPassCategory::General` form,
  and a 5-argument form stamping both explicitly).
- `"RenderOpaque"` + `"DrawSkyBackground"` as two real, separate passes
  (replacing the old monolithic `"GameView"`), correctly ordered (Opaque
  before Sky, since Sky relies on an `EQUAL` depth-test against the depth
  buffer Opaque just wrote) — plus a genuine, permanent `"RenderTransparent"`
  extension point for a future transparency campaign.
- Every real pass in the engine — 5 Atmosphere LUT passes + the Aerial
  Perspective Composite pass, `"RenderOpaque"`/`"DrawSkyBackground"`/
  `"RenderTransparent"`, GPU Skinning, `"Present"`, the Frame Debugger's own
  replay passes, and Compute Blur Validation — declared through
  `AddRenderPass()`, correctly tagged. The only two permanent, deliberate
  `builder.AddPass()` survivors are `AddSceneViewPass()`'s own `"SceneView"`
  pass (explicitly, permanently out of scope — the Scene View is not part of
  the Frame Debugger's Game-View-only tree) and one unrelated-method-name
  grep false positive.
- A Frame Debugger tree-building rework that discovers every one of the
  above GENERICALLY from `PassKind`/`RenderPassCategory`/`ViewScope`/real
  execution order — never a hand-maintained name list or hardcoded string
  match — with the old `isSkyBackgroundDraw`/`RecordSkyBackgroundDraw()`
  hack removed entirely.
- Updated documentation (`AGENTS.md`'s "Render Pass System" section,
  `docs/conventions/frame-debugger.md`'s "What's new"/"What is real today"
  sections) accurately describing the shipped architecture for future
  readers.
- Tier-1 test coverage added or updated at every phase for every piece of new
  pure logic (`PassKind`/`RenderPassCategory` `ToString()`, `AddRenderPass()`
  itself, `CombinePassGpuStats()`, `CollectTransparentRenderables()`, and the
  Frame Debugger's whole new generic-grouping algorithm) — the final `ctest`
  run in this phase confirms all of it passing, 100%, alongside the rest of
  the pre-existing suite.

## Explicit breaking changes (as pre-approved by `PHASE0_MASTER_STRATEGY.md`'s
own Locked Design Decision #4)

- The Frame Debugger pass literally named `"GameView"` no longer exists —
  replaced by `"RenderOpaque"` (which takes over the per-entity children) as
  a sibling to the new, real `"DrawSkyBackground"` and the scaffolded
  `"RenderTransparent"`.
- `FrameDebuggerDrawRecord::isSkyBackgroundDraw` and
  `FrameDebuggerCaptureContext::RecordSkyBackgroundDraw()` no longer exist —
  the Sky Background draw is now a real, separate, generically-discovered
  Render Graph pass instead of a fabricated draw-record hack.
- The Frame Debugger tree gained a new `"Compute LUT"` grouping heading,
  distinct from the pre-existing generic `"Compute Dispatches
  (Pre-GameView)"` bucket (which now only ever contains non-Atmosphere
  passes, e.g. GPU Skinning, when present).

Every one of these was called out loudly in its own originating phase's
completion report and is now reflected in `docs/conventions/frame-debugger.md`
and `AGENTS.md`.

## What was explicitly NOT done (out of scope, by design)

- **Real transparency rendering.** `"RenderTransparent"`/
  `RenderSystem::CollectTransparentRenderables()` are a genuine, permanent,
  currently-always-empty scaffold — no `isTransparent`/`renderQueue` concept
  was added to `MeshRenderer`, and no sorting/blending logic was written. This
  is a clean drop-in point for a future, dedicated transparency campaign, not
  a speculative implementation.
- **Async compute, memory aliasing, or any other Render Graph orchestrator
  feature.** The orchestrator itself (`RenderGraphCompiler`/
  `RenderGraphBarrierPlanner`/`RenderGraphResourcePool`) was never touched —
  this campaign only replaced the declaration layer sitting on top of it.
- **True per-pass breakpoint/stop execution** (pausing the GPU mid-frame at a
  specific pass boundary) — still a documented future item in `TODO.md`'s own
  "Frame Debugger" section, unrelated to this campaign's own scope.
- **Merging `feature/render-pass-impl` into any other branch** — outside this
  campaign's own authority, per PHASE7's own "What We Will NOT Do".

## Recommendation for whoever picks up the next session

1. If a real transparency system is ever built, `"RenderTransparent"`'s
   already-wired call site (`Application::Run()`, right after
   `"DrawSkyBackground"`, right before the Aerial Perspective Composite pass)
   and `RenderSystem::CollectTransparentRenderables()`'s already-correct
   signature are the natural, zero-risk starting points — no Render Pass
   System plumbing needs to change, only the currently-always-empty body.
2. Any FUTURE new pass anywhere in this engine should be declared through
   `RenderGraphBuilder::AddRenderPass()` from day one — this is now the
   permanent, documented, single official chokepoint (`AGENTS.md`'s "Render
   Pass System" section), not a temporary migration target.
3. No further action is required to close out this campaign itself — every
   phase's own Definition of Done is met, the full regression suite is green,
   and the live verification in this report confirms the real, running engine
   matches the originally-requested tree shape end to end.

## Final state

- `cmake --build build`: succeeds, zero errors (`ninja: no work to do` — the
  tree was already fully built from every prior phase's own incremental
  check).
- `ctest -C Debug --output-on-failure`: **1578/1578 tests run, 100% passing**
  (1 correctly-skipped optional smoke test aside).
- Live, HTTP-driven Frame Debugger verification: confirmed the exact target
  tree shape, per-entity `"RenderOpaque"` children, a real separate
  `"DrawSkyBackground"` leaf with its own correct, distinct pipeline-state
  Inspector data, and the correct absence of `"RenderTransparent"` as a
  visible row.
- `git status` on `feature/render-pass-impl`: clean after this report and its
  accompanying commit.
