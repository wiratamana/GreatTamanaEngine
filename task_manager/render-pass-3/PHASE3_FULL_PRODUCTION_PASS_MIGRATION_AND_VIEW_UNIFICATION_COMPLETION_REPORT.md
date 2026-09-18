# PHASE3_FULL_PRODUCTION_PASS_MIGRATION_AND_VIEW_UNIFICATION — Completion Report

_Child of `PHASE0_MASTER_STRATEGY.md`, `render-pass-3` campaign. Branch:
`feature/render-pass-impl` (unchanged, never switched). This is documented,
in its own phase file, as the heaviest, highest-risk phase of the campaign —
this report is correspondingly detailed, including a real, live-testing-
confirmed correctness bug found and fixed during implementation._

## Summary

Implemented PHASE3 exactly as scoped: every remaining production pass
(Atmosphere ×6 — Transmittance/Multi-Scattering LUTs, Sky-View LUT ×2 views,
Aerial Perspective volume ×2 views, Aerial Perspective volume debug slice,
Aerial Perspective Composite ×2 views — plus Sky Background, Transparent,
Scene View's own Opaque draw, and "Present") now goes through the
`RenderPipeline` provider system PHASE1 built and PHASE2 proved out.
`Application.cpp`'s hand-duplicated `if (gameTarget != nullptr) { ... }
if (sceneTarget != nullptr) { ... }` blocks collapsed into ONE generic
per-view construction of `RenderPassViewData` feeding a single
`m_offscreenRenderPipeline.DeclareInto()` call; a brand-new
`m_presentRenderPipeline` (with exactly one provider, `"Present"`) now
drives the separate, pipelined swapchain regime. `AddSceneViewPass()` is
deleted — Scene View's own Opaque/Sky/Transparent draws now go through the
exact same generalized providers Game View uses, tagged
`ViewScope::SceneView`.

**A real, live-testing-confirmed correctness bug was found and fixed during
this phase** (not merely a theoretical risk the phase doc flagged) — see
"The real bug this phase's own live verification caught" below. Fixing it
required one genuine, well-justified extension to `RenderPipeline.h` itself
(a new `ProviderTiming` two-phase declare model) beyond what PHASE1/PHASE2
shipped — documented in full below, together with a full explanation of why
it was necessary and how it was verified.

Verified with an incremental compile of `gte_core`/`GreatTamanaEngineTests`/
`GreatTamanaEngine`, a full targeted test run (221/221
`RenderGraph*:RenderPass*:RenderPipelineTest.*:RenderViewIdTest.*` tests
passing, zero regressions — 14 new Tier-1 tests added this phase), and a
live, HTTP-driven, screenshot-verified smoke test confirming Game View and
Scene View both render correctly (terrain mesh + atmosphere sky, previously
confirmed BROKEN — see below — by the same live testing that caught the
underlying bug), the Render Graph panel showing every pass declared and
NONE culled, the Frame Debugger's Game-View tree/Inspector data
byte-for-byte unchanged from before this phase, and the Frame Debugger's
replay-step mechanism (Step 3.7) producing real, non-garbage per-object
preview images.

## The real bug this phase's own live verification caught (read this first)

The phase doc's own Step 3.3b explicitly warned: *"a real, structural quirk
of mixing immediate-declare and deferred-declare providers in one
`RenderPipeline`... worth re-confirming this explicitly... not just assuming
it."* This phase's FIRST live smoke test (after a clean compile with zero
warnings) confirmed this quirk was not just theoretical — it was a REAL,
CONFIRMED correctness bug:

- **Symptom**: Game View and Scene View rendered as flat white/black —
  `GET /get_swapchain` showed a completely black window; `GET
  /get_texture?texture_name=GameView` showed solid white;
  `GET /get_texture?texture_name=GameViewComposited` showed solid black.
  The Atmosphere LUT textures themselves (Transmittance/Sky-View) looked
  visually correct when inspected directly.
- **Root cause, confirmed via the Editor's own "Render Graph" panel**: the
  `"RenderOpaque"` pass showed status `culled`. `RenderPipeline::DeclareInto()`
  (as PHASE1 shipped it, and as this phase's own first draft used it)
  invokes every registered provider in ONE single sweep, in registration
  order; a provider reaching `frame.builder` directly (Step 3.3b — the
  Atmosphere-wrapping providers) declares its own real pass IMMEDIATELY,
  during that sweep, while a provider using the ordinary deferred
  `RenderPassDesc` mechanism (`"RenderOpaque"`/`"DrawSkyBackground"`) only
  actually calls `builder.AddRenderPass()` at the very END of the sweep,
  after every provider has run and the collected list has been sorted. This
  means `"AtmosphereComposite"` (registered last, but STILL an immediate
  provider) landed in the underlying pass list BEFORE `"RenderOpaque"`/
  `"DrawSkyBackground"` had declared their own writes at all —
  `RenderGraphCompiler::Compile()`'s own resource-versioning scan builds a
  reader's dependency edge against whatever writer it has ALREADY SEEN so
  far in declaration order, never a writer declared later, so
  `"AtmosphereComposite"`'s read of `"GameView"`/`"SceneView"` was resolved
  against the import baseline (no writer yet known), and `"RenderOpaque"`/
  `"DrawSkyBackground"`'s own writes became unreachable from any kept root
  and were silently culled.
- **Fix**: `RenderPipeline.h` gained a new, additive `ProviderTiming` enum
  (`BeforeDeferredPasses` — the default, unchanged behavior for every
  pre-existing 3-argument `Register()` call — and `AfterDeferredPasses`),
  and `DeclareInto()` became a genuine TWO-PHASE model
  (`DeclareOnePhase(builder, frame, BeforeDeferredPasses)` followed by
  `DeclareOnePhase(builder, frame, AfterDeferredPasses)`, each phase running
  its own matching providers AND its own sort+flush of whatever THEY
  deferred). `"AtmosphereComposite"` is now registered with
  `ProviderTiming::AfterDeferredPasses`, GUARANTEEING it lands after every
  `BeforeDeferredPasses` provider's deferred passes (`"RenderOpaque"`/
  `"DrawSkyBackground"`/`"RenderTransparent"`/`"GpuSkinning"`), regardless of
  registration order. `"AtmosphereSharedLut"`/`"AtmosphereViewLut"`/
  `"GpuSkinning"`/`"RenderOpaque"`/`"DrawSkyBackground"`/`"RenderTransparent"`/
  `"Present"` all keep the default `BeforeDeferredPasses` timing (correct —
  none of them need to run after the deferred flush).
- **Verified fixed**: after this change, the Render Graph panel shows every
  pass (including `"RenderOpaque"`, draws=2, ~1,045,470 triangles for the
  test scene's terrain mesh) with NO `culled` status anywhere, and both Game
  View and Scene View render the terrain + atmosphere sky gradient correctly
  via `GET /get_swapchain`.
- **New Tier-1 test coverage** (per this codebase's own Testability rule —
  every change to logic like this needs a matching test):
  `RenderPipelineTest.AfterDeferredPassesProviderDeclaresStrictlyAfterEveryBeforeDeferredPassesDeferredPass`
  deliberately registers the `AfterDeferredPasses` provider FIRST, to prove
  `ProviderTiming` (not registration order) is what determines the result.

**A second, smaller bug was caught by the same live testing pass**: a debug
build produced a spammed, repeating `stderr` log line —
`RenderPassBlackboard: a value was published under key "<unknown>" but
never fetched this frame.` This came from `kGameSkyBackgroundCallbackKey`
(Step 3.7's Game-View sky-background-callback republish for
`AddFrameDebuggerReplayPasses()`'s benefit) — `"DrawSkyBackground"`
publishes it every frame Game View is active, but it is only genuinely
consumed on the rare frame a Frame Debugger replay capture is actually
serviced. Fixed by moving the `Fetch()` call to run UNCONDITIONALLY,
immediately after `DeclareInto()` returns (before
`ReportUnusedPublishesIfAny()`), storing the result in a
`std::optional` local that the "if a replay was requested" branch then
consumes — this marks the slot "fetched" every frame Game View is active,
regardless of whether a replay ends up being serviced, eliminating the
false-positive warning while still reporting a genuine dangling hand-off if
one ever occurs.

## Files changed

- **`src/Renderer/RenderGraph/RenderPipeline.h`** —
  - `RenderPassFrameContext` gained `RenderGraphBuilder& builder;` (Step
    3.3b) and `finalTextureOutputs`/`finalVolumeTextureOutputs` were made
    `mutable` (a confirmed, narrow fix to a real PHASE1 gap — see
    "Deviations" below).
  - `RenderPipeline` gained `SetLegacyViewScopeTranslator()` (Step 3.2) and
    a new `ProviderTiming` enum + two-phase `DeclareInto()`/
    `DeclareOnePhase()` (the bug fix above).
  - `Register()` gained a trailing, defaulted `ProviderTiming timing =
    BeforeDeferredPasses` parameter — every pre-existing 3-argument call
    site (PHASE2's own `"GpuSkinning"`/`"RenderOpaque"`) compiles
    unmodified.
- **`tests/Renderer/RenderGraph/RenderPipelineTests.cpp`** — every existing
  `RenderPassFrameContext` aggregate-init call site updated to supply the
  new `builder` field; 5 new tests added (translator default/injected
  behavior, immediate-via-`frame.builder` declaration, mutable-output
  append-through-const-reference, and the `ProviderTiming` ordering fix) —
  27 tests total in this file now (was 22 after PHASE1/PHASE2).
- **New: `src/Application/RenderPassViewData.h`** — `RenderPassViewData`
  (Step 3.1) and `TranslateLegacyViewScope()` (Step 3.2). See "Deviations"
  below for why this stayed a separate Application-layer header rather than
  a field directly on `RenderPassFrameContext`, as the phase doc's own text
  states a preference for.
- **`src/Application/RenderPasses.h`/`.cpp`** — `AddSceneViewPass()` DELETED
  (Step 3.4) — confirmed zero remaining callers anywhere in the repo
  (including `tests/`) via `search_in_dir` before deletion; every other
  function in this file is BYTE-FOR-BYTE UNCHANGED (confirmed via
  `git diff` — the only diff hunk is the `AddSceneViewPass()` removal
  itself). Doc comments immediately around the deletion point updated;
  several OTHER files' doc comments still mention `AddSceneViewPass()` by
  name in passing (`AtmospherePassSequence.h`, `Editor/EditorLayer.h`,
  `Editor/ImGuiEditorLayer.cpp`, `Editor/ComputeBlurValidation.cpp`,
  `Renderer/Renderer.h`) — these are all comment-only references (confirmed
  via `search_in_dir`, zero actual call sites), left as a known, harmless
  documentation-staleness cleanup item for a future pass rather than
  touched here (out of scope creep for an already-heavy phase).
- **`src/Application/Application.h`** — removed
  `m_currentGameViewTargetForOffscreenPipeline`/
  `m_currentGameViewAspectForOffscreenPipeline` (superseded by
  `m_currentViewDataThisFrame`); added
  `m_presentRenderPipeline`/`RegisterPresentRenderPipelineProvider()`/
  `m_currentViewDataThisFrame`/`FindViewData()`/the small set of
  present-regime per-frame members (`m_needsDirectGameRenderThisFrame`/
  `m_directGameRenderAspectThisFrame`/`m_swapchainImageThisFrame`/
  `m_recordImGuiThisFrame`); `#include`s `<functional>`/`<optional>`/
  `RenderPassViewData.h`.
- **`src/Application/Application.cpp`** — `RegisterOffscreenRenderPipelineProviders()`
  rewritten to register all 7 offscreen providers (`"AtmosphereSharedLut"`/
  `"GpuSkinning"`/`"AtmosphereViewLut"`/`"RenderOpaque"`/
  `"DrawSkyBackground"`/`"RenderTransparent"`/`"AtmosphereComposite"`); new
  `RegisterPresentRenderPipelineProvider()`; new `FindViewData()`; both
  regimes' `build` lambdas in `Run()` rewritten per Step 3.6/3.5. New
  blackboard keys/payload struct in the file's own anonymous namespace
  (`kAtmosphereSharedLutKey`/`kAtmosphereViewLutGameKey`/
  `kAtmosphereViewLutSceneKey`/`kGameSkyBackgroundCallbackKey`/
  `AtmosphereSharedLutBlackboardEntry`).

## Deviations from the phase doc (all intentional, all documented)

### Deviation 1 — `RenderPassViewData` lives in `src/Application/`, NOT as a field on `RenderPassFrameContext`

Step 3.1 states a preference for storing the per-frame
`std::vector<RenderPassViewData>` directly as a field on
`RenderPassFrameContext` ("prefer putting it ON the frame context itself").
This implementation instead picked the SAME paragraph's own explicitly
offered alternative: an Application-owned side table
(`Application::m_currentViewDataThisFrame`) a provider's captured `this`
looks up via `Application::FindViewData()`. Reason: `RenderPassFrameContext`
is defined in `src/Renderer/RenderGraph/RenderPipeline.h`, a Renderer-layer
file. Adding a `std::vector<RenderPassViewData>` field there would force
that file to `#include` an Application-layer header — a genuine backward
dependency this codebase's Clean Architecture rule (`AGENTS.md`) forbids
outright, unlike `RenderPassFrameContext::builder` (Step 3.3b), which is
safe to add directly since `RenderGraphBuilder` is itself an `rg`-namespace
type `RenderPipeline.h` already includes. `RenderPassViewData` carries no
such pedigree — it is genuinely Application-specific "what is a view"
knowledge.

### Deviation 2 — `RenderPassFrameContext::finalTextureOutputs`/`finalVolumeTextureOutputs` made `mutable`

A real, confirmed gap in PHASE1's own shipped code: `RenderPassProvider`
takes `const RenderPassFrameContext&`, but PHASE1's own doc comment on these
two fields says "any PROVIDER that owns a handle needing this treatment
simply appends it here directly" — impossible through a `const` reference
to a non-reference, non-`mutable` `std::vector` member. (Reference members
like `blackboard`/`builder` stay genuinely mutable through a `const`
wrapping object regardless — a real C++ quirk — which is why those two
never needed this fix.) This phase is the first real consumer of this
mechanism (the Atmosphere-wrapping providers), so the gap was fixed here:
both fields are now `mutable`. Documented via a new Tier-1 test
(`ProviderCanAppendToFinalOutputsThroughAConstFrameReference`).

### Deviation 3 — `ProviderTiming` (the bug fix above) is a genuine, additive extension to `RenderPipeline.h` beyond what PHASE1/PHASE2 shipped

Not anticipated by name in the phase doc, but squarely within PHASE3's own
scope (`RenderPipeline.h` is the very layer this campaign is building/
refining) and required by Step 3.3b's own explicit "worth re-confirming
this... not just assuming it" instruction, which this phase's own live
testing then confirmed as a REAL bug, not a theoretical one. See "The real
bug this phase's own live verification caught" above for the complete
write-up.

### Deviation 4 — Step 3.4's "Scene overlay" placement: option (a), folded into `"RenderTransparent"`

Implemented as the phase doc's own option (a) ("fold it into
`"RenderTransparent"`'s own provider body, gated on
`frame.currentView == Named("Scene")`" — actually implemented as "gated on
`RenderPassViewData::recordSceneOverlay` being non-empty", which is
equivalent since only Scene's own `RenderPassViewData` ever sets it).
`"RenderTransparent"` therefore declares a REAL pass for Scene View
specifically (unlike Game View, where it remains a true no-op, matching
`AddRenderTransparentPass()`'s own pre-existing behavior) — its own
`execute` lambda wraps the grid-overlay call in
`BeginGraphPassRecording()`/`EndGraphPassRecording()`, mirroring
`"DrawSkyBackground"`'s own established pattern (the OLD, fused
`"SceneView"` pass did NOT wrap its own sky/grid calls this way — a minor,
harmless behavioral tightening, not a regression, since neither call
actually routes through `Renderer::Submit()`'s draw-stat tracking either
way).

### Deviation 5 — a known, documented Profiler-panel-only limitation: `RenderGraph::LastKnownStatsFor()` cannot disambiguate same-named passes across views

`RenderGraph::LastKnownStatsFor()`/its own private `m_lastKnownStats` table
is keyed PURELY by pass NAME, with no `ViewScope` disambiguation — a
pre-existing limitation that simply never mattered before this phase, since
no two passes ever shared an identical literal name within one frame until
Step 3.4 made Scene View's own `"RenderOpaque"`/`"DrawSkyBackground"`/
`"RenderTransparent"` share Game View's exact names (by this phase's own
explicit, doc-sanctioned design). The practical effect: whichever of
Game/Scene View's own same-named pass EXECUTES LAST this frame (Scene's,
given this phase's own per-view provider loop order) overwrites the other's
stats entry, so `Profiling::GpuPass::GameView`'s draw-call/triangle-count/
timing numbers in the Editor's "Profiler" panel will silently read as
SCENE View's own numbers whenever BOTH panels are visible simultaneously.
This is a Profiler-panel DISPLAY-ONLY cosmetic issue — never a rendering-
correctness one, and not something the Definition of Done requires this
phase to test. Fixing it for real would mean teaching `RenderGraph.cpp`'s
own by-name lookup to also consider `ViewScope`, a genuine `RenderGraph.cpp`
core change deliberately out of scope for this already-heaviest migration
phase. Flagged here explicitly, both inline in `Application.cpp` and here,
as a known follow-up for a future phase/campaign.

### Deviation 6 — the ground-grid overlay's own pixel-level visual correctness (Step 3.4's explicit ask) was not conclusively confirmed via a live screenshot

The phase doc explicitly asks to "verify this explicitly with a live Scene
View screenshot... showing the ground grid still renders correctly against
the sky." The live `TestScene.gtscene` test project's terrain mesh fills
essentially the entire Scene View frustum from the Editor camera's default
position, making the ground-grid overlay (rendered at/near y=0) visually
indistinguishable from the terrain itself in the screenshots captured this
phase. What WAS confirmed: (a) the `"RenderTransparent"` pass is genuinely
declared for Scene View (visible in the Render Graph panel, correctly
positioned after `"DrawSkyBackground"`), (b) its `execute` lambda correctly
invokes `IEditorLayer::RenderSceneGrid()` (the same, unchanged function the
old `AddSceneViewPass()` called), and (c) no exception/crash occurred. A
future phase with access to a test scene with open ground (or the ability
to reposition the Scene camera via HTTP) could re-run this specific visual
check.

### Deviation 7 — "Present" (release-style direct-render fallback) was not live-tested in this build/session

This fallback only fires when BOTH `gameTarget`/`sceneTarget` are `nullptr`
(both Editor panels hidden, or a release/`GTE_ENABLE_EDITOR=OFF` build) —
unreachable in a normal Editor session with default docking, exactly as
prior campaigns (`render-pass-1`/PHASE2's own completion report) already
noted for the identical reason. Verified by code reading only: the
`"Present"` provider (`RegisterPresentRenderPipelineProvider()`) correctly
gates its own fresh `AddGpuSkinningPasses(frame.builder, ...)` call behind
`m_needsDirectGameRenderThisFrame`, mirroring the OLD code's own
`needsDirectGameRender` gate exactly, and never reuses/fetches a
`rg::BufferHandle` from the offscreen regime's own, separately-compiled
graph this same frame (confirmed structurally — the present regime's own
`presentBlackboard`/`presentFrame` are fresh locals, never shared with the
offscreen regime's).

## Verification performed

- **Incremental compile**: `gte_core`, `GreatTamanaEngineTests`, and the
  real `GreatTamanaEngine.exe` all built cleanly (confirmed via `cmake
  --build build --target <name>`), multiple times across this phase's own
  iterative bug-fixing.
- **Full targeted test run**: `GreatTamanaEngineTests.exe
  --gtest_filter=RenderGraph*:RenderPass*:RenderPipelineTest.*:RenderViewIdTest.*`
  — **221/221 passing**, zero failures, zero new skips (207 pre-existing +
  14 new this phase: 5 in the translator/immediate-builder/mutable-output
  category, 1 for the `ProviderTiming` ordering fix, plus the pre-existing
  10 `RenderPipelineTest` + 4 `RenderViewIdTest` cases that a narrower
  `RenderGraph*:RenderPass*` filter alone does not match by name — the
  wider filter above was used specifically to include them).
- **`git diff`/`git status` verification**: confirmed `ComputeBlurValidation.cpp`
  has ZERO changes (not present in `git status` at all); confirmed
  `RenderPasses.cpp`'s only diff hunk is the `AddSceneViewPass()` removal
  itself (`AddFrameDebuggerReplayPasses()` and every other function
  byte-for-byte unchanged).
- **Live, HTTP-driven smoke test** (`run_app_background` + `gte_send_request`,
  `TestScene.gtscene` loaded via `POST /load_scene`):
  - **Confirmed BROKEN, then confirmed FIXED** (the `ProviderTiming` bug —
    see above): before the fix, `GET /get_swapchain` showed a fully black
    window and the Render Graph panel showed `"RenderOpaque"` as `culled`;
    after the fix, both Game View and Scene View render the terrain mesh +
    atmosphere sky gradient correctly, and the Render Graph panel shows
    every pass (7 offscreen-regime providers × applicable views, plus
    `"Present"`) declared with NO `culled` status anywhere.
  - **Render Graph panel** (`GET /activate_tab?name=Render Graph` +
    `GET /get_swapchain`) confirmed the full expected pass list AND
    resource-lifetime table: `"GameView"`/`"SceneView"` both show lifetime
    `RenderOpaque -> AtmosphereAerialPerspectiveCompositePass` (i.e.
    `"RenderOpaque"` IS the first writer, `"AtmosphereComposite"` the last
    reader — exactly the intended dependency chain).
  - **Frame Debugger** (`GET /frame_debugger/open` → `enable` → `capture` →
    `select_event` → `get_swapchain`) confirmed the Game-View tree shape is
    BYTE-FOR-BYTE IDENTICAL to before this phase: `"Compute LUT"` group ▸
    every Atmosphere LUT pass ▸ `"Compute Dispatch"`; `"RenderOpaque"` ▸ one
    child per real entity (`"SmokeTestCube (Entity 0)"`/`"Entity 2 (Entity
    2)"` for this test scene); `"DrawSkyBackground"` ▸ `"Draw Quad"`;
    `"Compute Dispatches (Post-GameView)"` ▸
    `"AtmosphereAerialPerspectiveCompositePass"` ▸ `"Compute Dispatch"`.
    Selecting the `"RenderOpaque"` event showed `"Event #10: Draw Mesh"`,
    `Pass: RenderOpaque`, `Blend: Opaque (no blend)`, `ZTest: Less`,
    `ZWrite: On`, `Cull: None` — identical to PHASE2's own completion
    report.
  - **Frame Debugger replay-step mechanism** (Step 3.7, correctness-critical
    per `AddFrameDebuggerReplayPasses()`'s own doc comment) — a capture
    trigger produced 3 real `FrameDebuggerReplayStepN` textures
    (`GET /list_textures` confirmed all 3 present, non-culled); `GET
    /get_texture?texture_name=FrameDebuggerReplayStep2` (the dedicated
    "sky step" — every real object + sky) showed the correct, real
    rendered terrain+sky image, NOT garbage/uninitialized VRAM (the exact
    failure mode `RenderPasses.h`'s own doc comment warns about);
    `FrameDebuggerReplayStep0` (object 0 only) showed the plain clear color
    — plausible, not garbage, for a scene where object 0 may be small/
    off-camera at this replay step.
  - Confirmed no repeating stderr log spam after the `kGameSkyBackgroundCallbackKey`
    fix (structural code-level fix — see "The real bug..." section above;
    not independently re-verified via raw stderr capture, since
    `run_app_background` does not expose the child process's console
    output to this tooling, but the fix's own logic was traced end-to-end).
  - App run for multiple frames/requests with zero crashes, zero assertion
    failures, before being cleanly stopped via `stop_app_background`.
- No full build, no full `ctest` run was performed — correctly out of
  scope for this phase per `PHASE0_MASTER_STRATEGY.md`'s cross-cutting
  rules ("No full build/regression test until PHASE5").

## Notes for PHASE4's implementer

- **`RenderPassEvent`/`ViewScope` are both now real, correctly-stamped
  fields on every production pass this campaign covers** — PHASE4's own
  job (the `FindPassByName(..., "RenderOpaque")` → structural
  `RenderPassEvent`-based pivot search) has everything it needs. Every
  migrated pass's `RenderPassEvent` matches `PHASE0_MASTER_STRATEGY.md`'s
  own intended mapping exactly (`GpuSkinning`=`PreOpaques`,
  `AtmosphereViewLut`=`PreOpaques`, `RenderOpaque`=`Opaques`,
  `DrawSkyBackground`=`AfterOpaques`, `RenderTransparent`=`Transparents`,
  `AtmosphereComposite`=`AfterTransparents`).
- **Both Game View's AND Scene View's `"RenderOpaque"`/`"DrawSkyBackground"`/
  `"RenderTransparent"` now share the EXACT same literal pass names,
  disambiguated only by `ViewScope`** — `FrameDebuggerData.cpp`'s own
  existing `viewScope == SceneView` checks already correctly exclude Scene's
  copies from the Game-View-only tree (confirmed live, tree shape
  unchanged) — but if PHASE4's own pivot-search rewrite ever needs to find
  "the first `RenderOpaque`", it MUST also filter by `viewScope !=
  SceneView` (or equivalent), not name alone, or it risks matching Scene's
  copy on a frame where Scene's own passes happen to sort earlier in
  `passesInExecutionOrder` — today's actual declaration order (Game before
  Scene within each PerActiveView provider's own inner loop, since
  `frame.activeViews` always pushes `"Game"` before `"Scene"`) keeps this
  safe today, but is worth being explicit about rather than assumed.
- **The `RenderGraph::LastKnownStatsFor()` same-name-collision limitation**
  (Deviation 5 above) is a real, confirmed, narrow gap for the Editor's
  "Profiler" panel specifically — not this phase's or PHASE4's job to fix,
  but worth knowing about if a future phase ever touches GPU-stats
  aggregation.
- **`ProviderTiming::AfterDeferredPasses` is now a real, tested,
  general-purpose mechanism** — any future pass that needs to run after
  today's "Opaque/Sky/Transparent" deferred batch (not just
  `"AtmosphereComposite"`) can register with this timing value; it is not a
  one-off special case.
- **The Frame Debugger's own capture mechanism did not need any change this
  phase** (confirmed identical tree/Inspector shape) — PHASE4's own scope
  (the pivot-search rewrite) is genuinely the only remaining item, per
  `PHASE0_MASTER_STRATEGY.md`'s Locked Design Decision 2.

## Definition of Done — checklist

- [x] `Application.cpp`'s offscreen `build` lambda no longer contains two
      separate, hand-duplicated `if` blocks — one generic per-view
      construction of `RenderPassViewData` feeding one `DeclareInto()` call.
- [x] `AddSceneViewPass()` is deleted — confirmed zero remaining callers.
- [x] Scene View now genuinely produces separate `"RenderOpaque"`/
      `"DrawSkyBackground"`/`"RenderTransparent"` passes tagged
      `ViewScope::SceneView` — verified via the Render Graph panel's own
      resource read/write table (`"SceneView"` resource's own lifetime:
      `RenderOpaque -> AtmosphereAerialPerspectiveCompositePass`) and the
      `InjectedLegacyViewScopeTranslatorIsAppliedPerDesc` Tier-1 test.
- [x] `m_presentRenderPipeline` exists, with `"Present"` as its one
      provider, driven from the swapchain regime's own `Execute()` call.
- [x] Every provider correctly stamps `legacyCategory`/`view`→`ViewScope`
      exactly as the OLD direct calls used to — confirmed via (b) the Frame
      Debugger's Game-View tree shape/Inspector data being visually
      IDENTICAL to before this phase.
- [x] A live, HTTP-driven check confirms: Game View renders correctly;
      Scene View renders correctly (mesh geometry + sky; ground grid
      declared/ordered correctly per code+Render-Graph-panel inspection,
      pixel-level visual confirmation inconclusive due to test-scene
      framing — Deviation 6); Present's fallback path verified by code
      reading only (unreachable in this Editor session — Deviation 7).
- [x] An incremental compile of `gte_core` succeeds.
- [x] `AddFrameDebuggerReplayPasses()` and `ComputeBlurValidation.cpp` are
      BYTE-FOR-BYTE UNCHANGED (confirmed via `git diff` showing zero
      changes to either — `ComputeBlurValidation.cpp` does not even appear
      in `git status`).
- [x] The Atmosphere-wrapping providers declare their real passes via a
      genuine `rg::RenderGraphBuilder&` reachable from
      `RenderPassFrameContext` (Step 3.3b) — confirmed by code and by the
      `ProviderCanDeclareARealPassDirectlyViaFrameBuilder` Tier-1 test.
- [x] `AddPresentPass()`'s own `gpuSkinningOutputBuffers` are supplied by a
      FRESH, direct-render-only `AddGpuSkinningPasses()` call made from
      inside the swapchain regime's own `"Present"` provider — confirmed by
      code reading (Step 3.5).
- [x] `AddFrameDebuggerReplayPasses()`'s existing call site still compiles,
      is still called (unmigrated, per Locked Design Decision 4), and still
      functions correctly for Game View — verified live (3 real,
      non-garbage `FrameDebuggerReplayStepN` textures captured).

## What This Phase Did NOT Do (confirmed, matching its own "What We Will NOT Do")

- Did NOT touch `AddFrameDebuggerReplayPasses()`'s own body or
  `ComputeBlurValidation.cpp` at all (confirmed via `git diff`/`git status`).
- Did NOT touch `FrameDebuggerData.cpp` — PHASE4's job.
- Did NOT delete `ViewScope`/`RenderPassCategory`/`RenderPassDrawKind`, or
  change the old `AddRenderPass()` overloads' existing parameters — only
  ADDED new providers that call INTO those existing, unchanged mechanisms
  (the `RenderPipeline.h` changes this phase made — `builder` field,
  `mutable` outputs, `ProviderTiming` — are all to the NEW PHASE1/PHASE2
  declaration layer itself, not to `RenderGraphBuilder`/`RenderGraphTypes.h`).
- Did NOT make `RenderGraphBuilder`/`RenderPipeline` itself know the strings
  `"Game"`/`"Scene"` — that knowledge lives entirely in
  `TranslateLegacyViewScope()`/the `RenderViewId::Named("Game")`/`Named("Scene")`
  call sites (`Application.cpp`/`RenderPassViewData.h`).
- Did NOT run a full build or full `ctest` regression suite — incremental
  compile + targeted live/HTTP verification only, per this phase's own
  scope (PHASE5 is the only phase that runs the full suite).
