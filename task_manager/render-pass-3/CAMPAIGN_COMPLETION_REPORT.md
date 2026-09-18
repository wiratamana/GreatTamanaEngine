# CAMPAIGN_COMPLETION_REPORT — Generic RenderPass Declaration Layer Campaign (`render-pass-3`)

_Final record of the whole `render-pass-3` campaign, branch
`feature/render-pass-impl`. Mirrors the shape of
`task_manager/render-pass-1/CAMPAIGN_COMPLETION_REPORT.md` and
`task_manager/render-pass-2/CAMPAIGN_COMPLETION_REPORT.md`. Written at the
close of PHASE5
(`PHASE5_FINAL_INTEGRATION_FULL_BUILD_AND_LIVE_VERIFICATION.md`), the only
phase in this campaign permitted a full build + full `ctest` regression run
+ live HTTP-driven verification against the real running engine._

## Why this campaign existed

Two prior campaigns already existed on this exact branch: `render-pass-1`
gave the engine one official pass-declaration chokepoint,
`RenderGraphBuilder::AddRenderPass()`, and split the old monolithic
`"GameView"` pass into `"RenderOpaque"`/`"DrawSkyBackground"`/a scaffolded
`"RenderTransparent"`; `render-pass-2` added `RenderPassDrawKind` and fixed
the Frame Debugger's ownership tree so every real pass leaf owns a real
child event. Both were real, shipped, and untouched by this campaign.

But a real, confirmed structural problem remained exactly where this
campaign's own design brief, `GENERIC_RENDERPASS_SYSTEM_DESIGN_V2.md`,
describes it: `RenderPassCategory` (`General`/`AtmosphereLut`/`GpuSkinning`/
`Debug`) is a CORE enum that already has to know the NAME of every feature
that will ever exist above it — exactly the "core vocabulary carries
enumerated opinions about specific features" problem that design doc's own
goal statement calls out. Worse, every real pass declaration in
`Application.cpp`/`RenderPasses.cpp` was still a hand-written, hand-ordered
free-function call, Game View and Scene View were two almost-identical
blocks of hand-duplicated code, and GPU Skinning's per-model output buffer
reached the Opaque pass only via a manually-threaded
`gpuSkinningOutputBuffers` vector parameter passed through three call sites
by hand. See `PHASE0_MASTER_STRATEGY.md`'s own "Step 1"/"Step 2" for the
full original diagnosis, confirmed by direct source inspection before any
phase began.

## The five phases, in one line each

| # | Phase | One-line outcome |
|---|-------|-------------------|
| 1 | Core Vocabulary and Blackboard | Added `RenderPassId`/`RenderPassTag`/`RenderPassTagMask`/`RenderViewId`/`RenderPassEvent`/`RenderPassDesc`/`RenderPassProvider`/`RenderPipeline`/`RenderPassBlackboard`/`RenderPassFrameContext` purely additively (`src/Renderer/RenderGraph/RenderPipeline.h/.cpp`), plus one new, trailing, DEFAULTED `renderPassEvent` parameter on both `RenderGraphBuilder::AddRenderPass()` overloads. Zero real consumers — nothing under `src/Application/`/`src/Editor/` touched. 24 new `RenderPipelineTests.cpp` tests + 3 new `RenderPassEvent` tests. |
| 2 | GPU Skinning / Opaque Blackboard Proof | The FIRST real consumer: `Application` gained a real `m_offscreenRenderPipeline` with `"GpuSkinning"` (`Once`) and `"RenderOpaque"` (`PerActiveView`) providers, GPU Skinning's output buffers reaching `"RenderOpaque"` ENTIRELY through `RenderPassBlackboard::Publish`/`Fetch` — zero direct call or shared captured variable between the two providers. |
| 3 | Full Production Pass Migration and View Unification | The heavy phase: every remaining production pass (Atmosphere ×6, Sky, Transparent, Scene View, Present) migrated onto the provider system; `Application.cpp`'s hand-duplicated Game/Scene `if` blocks collapsed into ONE generic per-view loop; a second `m_presentRenderPipeline` stood up for the pipelined swapchain regime; `AddSceneViewPass()` deleted. A real, live-testing-confirmed correctness bug (immediate-declare vs. deferred-declare provider ordering silently culling `"RenderOpaque"`) was found and fixed via a new `ProviderTiming` two-phase declare model. 221/221 targeted tests passing (14 new). |
| 4 | Frame Debugger Event Pivot Fix | The one, narrow, already-committed Frame Debugger change: the literal `FindPassByName(..., "RenderOpaque")` pivot search replaced with a structural, name-free `FindViewRegionPivot()` using `RenderPassEvent`. A real bug this phase's own live verification caught (six Atmosphere passes silently defaulting to the same `RenderPassEvent::Opaques` value `"RenderOpaque"` itself carries) was found and fixed, user-approved via `ask_questions`, by stamping correct, explicit `RenderPassEvent` values onto all six `AtmosphereLutRenderer.cpp` call sites. 323/323 targeted tests passing (1 new). |
| 5 | Final Integration: Full Build, Full Regression, Live Verification | This phase. Full build, full `ctest` regression suite (1616 tests, 100% passing, one pre-existing environment-gated skip), and a live, HTTP-driven Frame Debugger + Scene View verification against the real running engine — see below. |

## Final, shipped architecture shape

```
Feature modules (Atmosphere / GpuSkinning / Opaque / Sky / Transparent / Present)
        |  each registers a RenderPassProvider once, at startup
        v
   RenderPipeline  (m_offscreenRenderPipeline: GpuSkinning, Atmosphere x6, Opaque,
                     Sky, Transparent, both Game+Scene views;
                     m_presentRenderPipeline: "Present" alone)
        |  DeclareInto(): loops active views, collects RenderPassDesc[],
        |  sorts by RenderPassEvent, translates ViewScope internally
        v
   RenderGraphBuilder::AddRenderPass()   <- UNCHANGED, byte-for-byte, since render-pass-2
        v
   RenderGraphCompiler / RenderGraphBarrierPlanner / RenderGraph::Execute()  <- UNCHANGED
```

Game View's own tree shape in the Frame Debugger (verified live in PHASE5,
identical to `render-pass-2`'s own final screenshot):

```
v Game View
  v Compute LUT
     v AtmosphereTransmittanceLutPass -> Compute Dispatch
     v AtmosphereMultiScatteringLutPass -> Compute Dispatch
     v AtmosphereSkyViewLutPass -> Compute Dispatch
     v AtmosphereAerialPerspectiveVolumePass -> Compute Dispatch
     v AtmosphereAerialPerspectiveVolumeDebugSlicePass -> Compute Dispatch
  v RenderOpaque                              (UNCHANGED shape)
     SmokeTestCube (Entity 1)
     Entity2 (Entity 2)
  v DrawSkyBackground -> Draw Quad
  v Compute Dispatches (Post-GameView)
     v AtmosphereAerialPerspectiveCompositePass -> Compute Dispatch
```

Scene View now produces the exact same three-pass shape
(`"RenderOpaque"`/`"DrawSkyBackground"`/`"RenderTransparent"`, tagged
`ViewScope::SceneView`) as a real, structurally-separate set of passes,
instead of one old fused `"SceneView"` pass — confirmed rendering correctly
via `GET /get_texture?texture_name=SceneViewComposited` in PHASE5's own live
verification.

## Campaign-wide verification performed (culminating in PHASE5)

### Full build

`cmake --build build`: `ninja: no work to do` — every phase's own
incremental compile check had already kept the tree fully built and up to
date at every step, so PHASE5's full build re-confirmed zero errors/warnings
across the entire campaign's accumulated changes with nothing left to
recompile. No cross-phase integration issue was found.

### Full regression test

`ctest -C Debug --output-on-failure`: **1616 tests run, 100% passing** (1615
passed outright, 1 — `PmxLoaderRealModelSmokeTest.LoadsAnMmdModelIfPresentOnThisMachine`
— correctly `GTEST_SKIP()`s, the same pre-existing, environment-gated skip
every prior campaign on this branch has documented). This is 27 tests higher
than `render-pass-2`'s own 1589 baseline. Zero regressions found anywhere in
the suite as a result of this campaign's five phases of changes.

### Live launch + verification

1. `GET /frame_debugger/open` → `200`; `GET /frame_debugger/enable?value=true`
   → `200`; `GET /frame_debugger/capture` → `hasCapturedFrame: true`,
   `totalEventCount: 15` (default scene) — identical baseline to every prior
   campaign.
2. `GET /get_swapchain` confirmed the Game-View tree shape is COMPLETELY
   UNCHANGED from `render-pass-2`'s own final screenshot.
3. Spawned two test primitives (`POST /instantiate_primitive` ×2),
   re-captured (deferred by one frame, per documented convention) —
   `totalEventCount: 17`, `"RenderOpaque"` now correctly showing two
   per-entity children.
   - **One real, user-caused interruption**: the engine crashed after being
     manually minimized mid-session (confirmed directly by the user, not a
     tool malfunction — no `bug_report` filed). The engine was relaunched
     and the entire live-verification sequence was re-run cleanly from
     scratch with no further issue — see PHASE5's own completion report for
     the full account.
4. `GET /frame_debugger/select_event?index=10` (`"RenderOpaque"`) + `GET
   /get_swapchain`: Inspector showed `Pass: RenderOpaque`, `Blend: Opaque
   (no blend)`, `ZTest: Less`, `ZWrite: On`, `Cull: None` — real, correct,
   unchanged pipeline-state data.
5. `GET /activate_tab?name=Scene` + `GET
   /get_texture?texture_name=SceneViewComposited`: confirmed Scene View's
   own newly-split Opaque/Sky/Transparent passes render correct geometry
   (the spawned test cube) and sky end-to-end, matching `GET
   /get_texture?texture_name=GameViewComposited`'s equivalent, correctly
   rendered Game View image. The ground-grid overlay's own pixel-level
   visual confirmation remained inconclusive at this small thumbnail
   resolution in this specific test scene — the EXACT SAME limitation
   `render-pass-3`'s own PHASE3 completion report already documented
   explicitly ("Deviation 6"), not a new gap.
6. GPU-skinned/animated model verification remained unavailable in this
   environment (`PmxLoaderRealModelSmokeTest` skipped — no MMD model asset
   present on this machine), the same environment-gated limitation PHASE2's
   own completion report already documented explicitly.
7. Engine stopped cleanly (`stop_app_background`).

Every check in PHASE5's own Definition of Done passed. No `bug_report` was
filed during this campaign for an actual tool malfunction — the one engine
crash during PHASE5's live session was directly confirmed by the user as
their own action (manually minimizing the window), not a tool defect.

## What shipped (cumulative)

- `src/Renderer/RenderGraph/RenderPipeline.h/.cpp` — `RenderPassId`/
  `RenderPassTag`/`RenderPassTagMask`/`RenderViewId`/`RenderPassEvent`/
  `RenderPassDesc`/`RenderPassProvider`/`ProviderScope`/`ProviderTiming`/
  `RenderPassBlackboard`/`RenderPassFrameContext`/`RenderPipeline` — a
  brand-new, generic declaration layer sitting strictly ABOVE the unchanged
  `RenderGraphBuilder::AddRenderPass()`/`AddPass()`/`AddComputePass()`
  orchestrator, which itself was never touched.
- `Application` now owns two `RenderPipeline` instances —
  `m_offscreenRenderPipeline` (GPU Skinning, Atmosphere ×6, Opaque, Sky,
  Transparent — both Game and Scene views) and `m_presentRenderPipeline`
  (`"Present"` alone) — replacing the old hand-duplicated `if (gameTarget
  != nullptr) {...} if (sceneTarget != nullptr) {...}` blocks with ONE
  generic per-view provider loop.
- A real, working cross-provider hand-off: GPU Skinning publishes its
  output buffers onto a `RenderPassBlackboard`; `"RenderOpaque"` (and, since
  PHASE3, Scene View's own copy and `"Present"`) fetches them back — zero
  shared captured variable or direct call between the two features.
- Scene View's own Opaque/Sky/Transparent draws are now real, separate
  passes (tagged `ViewScope::SceneView`), mirroring Game View's own
  already-shipped shape, instead of one old fused `"SceneView"` pass built
  via plain `builder.AddPass()`. `AddSceneViewPass()` itself is deleted.
- The Frame Debugger's own `"where does the view region start"` pivot
  search is now a structural, name-free `FindViewRegionPivot()` lookup
  using `RenderPassEvent`, replacing the literal `FindPassByName(...,
  "RenderOpaque")` string match — every OTHER Frame Debugger mechanism
  (tree shape, per-entity children, Compute LUT/Post-GameView grouping,
  Inspector data) is completely unchanged.
- `AddFrameDebuggerReplayPasses()`'s replay steps and
  `ComputeBlurValidation.cpp`'s own pass remain BYTE-FOR-BYTE UNCHANGED —
  still declared via the OLD, direct `AddRenderPass()` call style, still
  read by the Frame Debugger via the OLD `ViewScope`/`RenderPassCategory`
  fields, forever, by explicit design.
- Tier-1 test coverage added at every phase for every piece of new pure
  logic (`RenderPipelineTests.cpp`, `RenderPassEvent`/`RenderViewId`
  coverage in `RenderGraphTypesTests.cpp`, the `ProviderTiming`
  ordering-correctness regression test, the `FindViewRegionPivot()`
  name-free regression test) — the final `ctest` run in PHASE5 confirms all
  of it passing, 100%, alongside the rest of the pre-existing suite.
- Updated documentation: `AGENTS.md`'s "Render Pass System" section gained
  a new paragraph describing the new layer and loudly flagging every
  deviation from the original design brief; `README.md`'s "Status" section
  gained a new summary bullet in the same style as `render-pass-1`/
  `render-pass-2`'s own entries.

## Explicit deviations from `GENERIC_RENDERPASS_SYSTEM_DESIGN_V2.md`'s own original defaults

This is the dedicated, clearly-labeled section this campaign's own PHASE5
instructions require: every place `PHASE0_MASTER_STRATEGY.md`'s "Locked
Design Decisions" REVERSED or AMENDED the original design doc's own stated
defaults, so a future reader of that original design doc is not misled into
thinking it was implemented exactly as originally written.

1. **Scene View WAS brought into the new per-view system** — the design
   doc itself never says otherwise (Scene View isn't named in it at all),
   but `render-pass-1`/`render-pass-2`'s own prior rule ("`\"SceneView\"`
   stays on plain `builder.AddPass()` forever, out of scope for the whole
   campaign") was EXPLICITLY REVERSED for this campaign, by direct user
   instruction: `AddRenderOpaquePass()`/`AddSceneViewPass()` were confirmed
   near-identical twins, and `Application.cpp` already hand-duplicated the
   entire per-view sequence — precisely the duplication the design doc's
   own `ProviderScope::PerActiveView` mechanism (Section 3) exists to
   remove. See PHASE3.
2. **The Frame Debugger's own reliance on the OLD `ViewScope`/
   `RenderPassCategory`/`RenderPassDrawKind` fields was NOT reworked onto
   the new tag/view system** — the design doc's own Section 7 states "the
   core namespace never defines what a tag or a view id MEANS" and implies
   downstream consumers eventually group by the new opaque tags instead.
   This campaign's `FrameDebuggerData.cpp`/`ComputeBlurValidation.cpp`
   keep using the OLD, pre-existing fields, unchanged, FOREVER — the ONE
   exception, already committed to and delivered (PHASE4), is the single,
   narrow `FindPassByName(..., "RenderOpaque")` → `FindViewRegionPivot()`
   pivot-search rewrite, using the new `RenderPassEvent` field exactly as
   the design doc's own Section 6 suggested ("the first pass with `order >=
   Opaques`"). Nothing else in the Frame Debugger changed.
3. **The design doc's own Phase B (Section 12) describes migrating "one
   self-contained feature... as a proof" with no name specified** — this
   campaign locked that proof case, by direct user Q&A, to be specifically
   GPU Skinning publishing its output buffers and `"RenderOpaque"` fetching
   them from the blackboard (PHASE2), a real, already-shipping,
   hand-threaded cross-feature dependency identified by direct source
   inspection before any phase began — not an arbitrary/toy example.
4. **Migration scope was explicitly bounded to FULL for production passes,
   EXPLICITLY EXCLUDING Editor-only/debug passes forever** — the design
   doc's own Section 12 "Phase C" describes migrating "every remaining call
   site" with no such carve-out named. This campaign's own Locked Design
   Decision explicitly, permanently excludes `AddFrameDebuggerReplayPasses()`'s
   replay steps and `ComputeBlurValidation.cpp`'s own pass from ever moving
   onto the new provider system — they exist purely to feed Frame Debugger
   tooling that itself never moved off the old fields (Deviation 2 above),
   so migrating them would only add dual-maintenance cost for zero benefit.
5. **DIRECT, LOUD DEVIATION from the design doc's own Decision 1 (Section
   0)**: that document says the render-graph BUILDER's own pass-adding
   entry point signature changes to accept the new opaque types, "additively
   first... the old overload plus the old enums themselves are deleted only
   once every real call site has migrated." **This campaign does NOT do
   that, ever, by explicit user decision.** Because of Deviations 2 and 4
   above (Frame Debugger stays on the old fields forever; Debug-only passes
   stay on the old call style forever), `RenderGraphBuilder`'s existing
   `AddRenderPass()` overloads, and `ViewScope`/`RenderPassCategory`/
   `RenderPassDrawKind` themselves, are NEVER deleted, and
   `RenderGraphBuilder.h`'s public surface for those specific parameters
   never changes at all beyond one new, trailing, DEFAULTED
   `RenderPassEvent renderPassEvent = RenderPassEvent::Opaques` parameter
   (PHASE1) — mirroring the exact trailing-defaulted-parameter technique
   `render-pass-2`'s own `drawKind` parameter already used. Instead,
   `RenderPipeline` (the new declaration layer, living strictly above the
   builder) is the ONLY thing that ever sees the new opaque `RenderPassId`/
   `RenderPassTagMask`/`RenderViewId`/`RenderPassEvent` types, and it
   internally TRANSLATES them into the OLD `ViewScope`/`RenderPassCategory`
   values before making its own, ordinary call into the EXISTING, byte-
   for-byte-unchanged `AddRenderPass()` chokepoint.
6. **Two separate `RenderPipeline` instances, not one** — the design doc
   itself describes a single `m_renderPipeline`/`RenderPipeline` instance
   throughout (Section 3's code sample: `m_renderPipeline.DeclareInto(b,
   frame);`). This engine's real `Application` issues TWO separate
   `RenderGraph::Execute()` calls per frame (one synchronous offscreen call
   for Game+Scene View, one pipelined swapchain call for `"Present"`
   alone), and a single shared `RenderPipeline` instance would have let a
   `ProviderScope::Once` provider (e.g. GPU Skinning) fire during BOTH
   `DeclareInto()` calls, silently duplicating those passes into the
   swapchain-regime graph too — a real, confirmed structural quirk PHASE3's
   own live testing caught. This campaign therefore introduced TWO
   `Application` member instances, `m_offscreenRenderPipeline` and
   `m_presentRenderPipeline`, instead of the design doc's single shared
   instance.

A seventh, smaller, ADDITIVE-ONLY extension beyond the design doc's own
text (not a reversal of a stated default, but worth naming here since it
was genuinely new mechanism the design doc never described): PHASE3
introduced `ProviderTiming` (`BeforeDeferredPasses`/`AfterDeferredPasses`)
to `RenderPipeline::Register()`, a real, tested, general-purpose two-phase
declare model needed to fix a genuine correctness bug (an immediate-declare
provider registered after a deferred-declare provider could otherwise land
in the underlying pass list before the deferred provider's own writes were
declared, silently culling them) that PHASE3's own live testing confirmed
was real, not theoretical. This is additive machinery inside the new layer
itself, not a deviation from a stated design-doc default.

## What was explicitly NOT done (out of scope, by design)

- **Deleting or renaming `ViewScope`/`RenderPassCategory`/
  `RenderPassDrawKind`** — permanently out of scope for this campaign (see
  Deviation 5 above).
- **Reworking the Frame Debugger beyond PHASE4's one named fix** — its
  whole tree-building/Inspector mechanism, per-entity children, Compute
  LUT/Post-GameView grouping, and every `ViewScope`/`RenderPassCategory`/
  `RenderPassDrawKind` consumer are completely unchanged.
- **Migrating `AddFrameDebuggerReplayPasses()`/`ComputeBlurValidation.cpp`
  onto the new provider system** — explicitly, permanently excluded (see
  Deviation 4 above).
- **Any Render Graph orchestrator change** —
  `RenderGraphCompiler`/`RenderGraphBarrierPlanner`/`RenderGraphResourcePool`/
  `RenderGraph::Execute()` were never touched by any phase of this
  campaign; only the declaration layer sitting above them was built.
- **A conclusive, pixel-level live confirmation of Scene View's ground-grid
  overlay** — inconclusive at the available test scene's small thumbnail
  resolution, an explicitly-documented limitation carried from PHASE3
  through PHASE5 (see "Live launch + verification" step 5 above).
- **A live confirmation of the blackboard hand-off with a real, non-empty
  GPU-skinned/animated model** — no MMD-rigged model asset was available in
  this environment at any point across this campaign's five phases (the
  same `PmxLoaderRealModelSmokeTest` skip every phase's own ctest run
  reported).
- **Merging `feature/render-pass-impl` into any other branch** — outside
  this campaign's own authority entirely, per every phase's own "What We
  Will NOT Do".

## Recommendation for whoever picks up the next session

1. Any FUTURE new render/compute/blit pass should still be declared through
   `RenderGraphBuilder::AddRenderPass()` — this remains the permanent,
   single official chokepoint (unchanged by this campaign). A NEW
   PRODUCTION feature (not Editor/debug-only) should ALSO register a
   `RenderPassProvider` with the appropriate `RenderPipeline`
   (`m_offscreenRenderPipeline` for anything in the synchronous
   Game+Scene-View regime, `m_presentRenderPipeline` for anything in the
   pipelined swapchain regime) rather than calling `AddRenderPass()`
   directly from `Application.cpp`, to keep enjoying the generic per-view
   loop and blackboard mechanism this campaign built.
2. Any future NEW immediate-declare `RenderPipeline` provider (one that
   reaches `frame.builder` directly rather than returning a deferred
   `RenderPassDesc`) MUST explicitly set its own real passes'
   `renderPassEvent` — PHASE4's own root-cause bug (six Atmosphere passes
   silently defaulting to the same value `"RenderOpaque"` carries) is a
   concrete, confirmed example of what happens if this is skipped.
3. If a real transparency system is ever built, `"RenderTransparent"`'s
   already-wired provider call site (both Game View's true no-op and Scene
   View's real grid-overlay body) is a natural, zero-risk starting point —
   no Render Pass System plumbing needs to change.
4. If a GPU-skinned/animated model asset becomes available in a future
   session's environment, re-running this campaign's own live-verification
   recipe (spawn/load it, enable GPU Skinning mode, capture a Frame
   Debugger frame, confirm it renders correctly in BOTH Game View and Scene
   View) would close the one verification gap every phase of this campaign
   had to document as an environment limitation rather than a confirmed
   pass.
5. No further action is required to close out this campaign itself — every
   phase's own Definition of Done is met, the full regression suite is
   green, and the live verification in this report confirms the real,
   running engine works end-to-end with the newly-shipped declaration
   layer.

## Final state

- `cmake --build build`: succeeds, zero errors (`ninja: no work to do` —
  the tree was already fully built from every prior phase's own incremental
  check).
- `ctest -C Debug --output-on-failure`: **1616/1616 tests run, 100%
  passing** (1 correctly-skipped optional smoke test aside) — higher than
  `render-pass-2`'s own 1589 baseline, as required.
- Live, HTTP-driven verification: confirmed the Game-View Frame Debugger
  tree shape is completely unchanged from `render-pass-2`'s own final
  screenshot, `"RenderOpaque"` still reports correct per-entity children
  and pipeline-state data, and Scene View's own newly-split Opaque/Sky/
  Transparent passes genuinely render correct geometry + sky end-to-end
  (confirmed via `SceneViewComposited`).
- `git status` on `feature/render-pass-impl`: clean after this report's own
  commit (aside from any pre-existing, unrelated untracked files, if any —
  none were found this session).
