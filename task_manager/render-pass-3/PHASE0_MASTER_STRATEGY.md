# PHASE0_MASTER_STRATEGY — Generic RenderPass Declaration Layer Campaign (`render-pass-3`)

_Part of the `feature/render-pass-impl` branch. Lives under
`task_manager/render-pass-3/`. This is the ORCHESTRATOR document — every
other `PHASEn_*.md` file in this same folder is a child of this one. Read
this file FIRST, always, before touching any child phase. Read
`GENERIC_RENDERPASS_SYSTEM_DESIGN_V2.md` (same folder) SECOND — it is the
original design brief every phase below implements; this document is the
"how we actually build it against the real, current codebase" translation
of that brief, resolved against a real Q&A session with the user (see
"Locked Design Decisions" below — several of these DEVIATE from the design
doc's own defaults, on purpose, by explicit user instruction)._

## Step 1: The Goal (Where are we going?)

Two prior campaigns already exist on this exact branch:

- `render-pass-1` (`task_manager/render-pass-1/`) gave the engine ONE
  official pass-declaration chokepoint,
  `RenderGraphBuilder::AddRenderPass(name, kind, viewScope, category,
  setup, execute, drawKind)`, and split the old monolithic `"GameView"`
  pass into `"RenderOpaque"` / `"DrawSkyBackground"` / a scaffolded
  `"RenderTransparent"`.
- `render-pass-2` (`task_manager/render-pass-2/`) added
  `RenderPassDrawKind` and fixed the Frame Debugger's ownership tree so
  every real pass leaf (not just `"RenderOpaque"`) owns a real child event.

Both of those campaigns are real, shipped, and NOT rewritten by this one.
But they left a real, confirmed structural problem exactly where
`GENERIC_RENDERPASS_SYSTEM_DESIGN_V2.md` describes it: `RenderPassCategory`
(`General`/`AtmosphereLut`/`GpuSkinning`/`Debug`) is a CORE enum that
already has to know the NAME of every feature that will ever exist above
it — the exact "core vocabulary carries enumerated opinions about specific
features" problem the design doc's own goal statement calls out. Worse,
every real pass declaration in `Application.cpp`/`RenderPasses.cpp` is
still a hand-written, hand-ordered free-function call, Game View and Scene
View are two almost-identical blocks of hand-duplicated code
(`AddRenderOpaquePass()`/`AddSceneViewPass()`, and the entire
atmosphere+opaque+sky+transparent sequence in `Application.cpp`'s own
`build` lambda, once per view, by hand), and one feature's output (GPU
Skinning's per-model output buffer) reaches an unrelated feature (Opaque)
only via a manually-threaded `gpuSkinningOutputBuffers` vector parameter
passed through THREE call sites by hand.

**The goal of this campaign is to add a new, generic DECLARATION LAYER
that sits strictly ABOVE the existing, untouched
`RenderGraphBuilder::AddPass()`/`AddComputePass()`/`AddRenderPass()`
orchestrator**, exactly as `GENERIC_RENDERPASS_SYSTEM_DESIGN_V2.md`
describes: a `RenderPipeline` that owns a list of registered `RenderPassProvider`
functions, collects `RenderPassDesc` values from them once per
frame-declaration call, sorts by a `RenderPassEvent` order hint, and feeds
each one into the render graph — turning "add a new pass" into "call
`Register()` once at startup," turning the Game/Scene View duplication
into ONE generic per-view loop instead of two hand-written blocks, and
turning GPU Skinning's manually-threaded buffer parameter into a real,
generic, opaque-keyed "blackboard" publish/fetch hand-off.

This is a NEW LAYER, not a rewrite: `RenderGraphCompiler`,
`RenderGraphBarrierPlanner`, `RenderGraphResourcePool`, and
`RenderGraph::Execute()` itself are never touched. `RenderGraphBuilder`'s
EXISTING `AddPass()`/`AddComputePass()`/`AddRenderPass()` methods are never
removed, never deprecated, and (per this campaign's own locked decisions
below) never even have their signatures changed — the new layer calls them
exactly the way `RenderPasses.cpp` already does today.

## Step 2: The Situation (Where are we now?)

Confirmed by direct source inspection — this document is the single
source of truth for every fact quoted in every child phase; do not
re-derive these from scratch.

1. **The render graph orchestrator and its one official chokepoint are
   real, tested, and untouched by this campaign.**
   `src/Renderer/RenderGraph/RenderGraphBuilder.h`'s
   `AddRenderPass(name, PassKind, ViewScope, RenderPassCategory, setup,
   execute, RenderPassDrawKind = DrawMesh)` (plus its 4-argument
   convenience overload defaulting `viewScope`/`category`) is the ONE
   entry point every real pass in the engine already goes through.
   `PassRecord` (`RenderGraphTypes.h`) carries `name`, `reads`, `writes`,
   `isCulled`, `kind` (`PassKind::Graphics`/`Compute`), `viewScope`
   (`ViewScope::Shared`/`GameView`/`SceneView`), `category`
   (`RenderPassCategory::General`/`AtmosphereLut`/`GpuSkinning`/`Debug`),
   `drawKind` (`RenderPassDrawKind::DrawMesh`/`DrawQuad`/`Blit`), `execute`,
   and optional clear values. `RenderGraphPassSnapshot`
   (`RenderGraphSnapshot.h`/`.cpp`) copies every one of these through,
   for both surviving AND culled passes.
2. **Every real pass today is still a free function that calls
   `AddRenderPass()`/`AddPass()` by hand**, scattered across
   `src/Application/RenderPasses.cpp` (`AddRenderOpaquePass()`,
   `AddDrawSkyBackgroundPass()`, `AddRenderTransparentPass()`,
   `AddSceneViewPass()` — still plain `builder.AddPass()`, never
   `AddRenderPass()` — `AddPresentPass()`, `AddFrameDebuggerReplayPasses()`,
   `AddGpuSkinningPasses()`), `src/Application/AtmospherePassSequence.cpp`
   (`AddAtmosphereSharedLutPasses()`, `AddAtmosphereViewLutPasses()`,
   `AddAtmosphereCompositePass()`), and
   `src/Renderer/Atmosphere/AtmosphereLutRenderer.cpp` (the actual
   `AddTransmittanceLutPass()`/`AddMultiScatteringLutPass()`/
   `AddSkyViewLutPass()`/`AddAerialPerspectiveVolumePass()`/
   `AddAerialPerspectiveVolumeDebugSlicePass()`/
   `AddAerialPerspectiveCompositePass()` methods).
3. **`Application::Run()` (`src/Application/Application.cpp`, ~line 540
   onward) hand-duplicates the whole atmosphere+opaque+sky+transparent
   sequence once per view**, inside two nested `if (gameTarget != nullptr)`
   / `if (sceneTarget != nullptr)` blocks that are structurally near-twins
   of each other (confirmed: both call `AddAtmosphereViewLutPasses()`, both
   resolve an eye position + view-projection, both eventually reach an
   Opaque-shaped draw and a Sky-shaped draw). This is exactly the
   duplication `GENERIC_RENDERPASS_SYSTEM_DESIGN_V2.md`'s
   `ProviderScope::PerActiveView` mechanism exists to collapse into ONE
   generic loop body.
4. **GPU Skinning's output buffer is a real, already-shipping,
   hand-threaded cross-feature dependency.** `AddGpuSkinningPasses()`
   (`RenderPasses.cpp`) returns `std::vector<rg::BufferHandle>
   gpuSkinningOutputBuffers`, which `Application.cpp` then manually passes,
   by hand, into THREE separate call sites —
   `AddRenderOpaquePass(..., gpuSkinningOutputBuffers, ...)`,
   `AddSceneViewPass(..., gpuSkinningOutputBuffers, ...)`,
   `AddPresentPass(..., gpuSkinningOutputBuffers)` — each of which declares
   a phantom `ResourceAccess::VertexBufferRead` against every handle in the
   vector (`DeclareGpuSkinningReads()`, `RenderPasses.cpp`). This is
   confirmed, by direct user Q&A (see Locked Design Decision below), as
   this campaign's REQUIRED real proof case for the design doc's
   "blackboard" mechanism (Section 4 of the design doc).
5. **The Frame Debugger's tree builder is real, tested, and DELIBERATELY
   OUT OF SCOPE for this campaign, with one narrow, named exception.**
   `BuildRealFrameDebuggerSnapshot()` (`src/Editor/FrameDebuggerData.cpp`)
   reads `pass.kind`/`pass.viewScope`/`pass.category`/`pass.drawKind`
   directly off `RenderGraphPassSnapshot` in well over a dozen places
   (confirmed: `viewScope == SceneView` checks at lines ~217, 738, 796,
   849; `category == AtmosphereLut`/`category == Debug` checks at ~744,
   797; `drawKind` consumed by `GraphicsChildEventLabelFor()`), and finds
   "where the Game View region starts" by literally searching
   `graphSnapshot.passesInExecutionOrder` for a pass named `"RenderOpaque"`
   (`FindPassByName(..., "RenderOpaque")`, line ~698-699). This same
   pattern is also confirmed in a direct sibling file:
   `src/Editor/ComputeBlurValidation.cpp`
   explicitly tags its own pass `RenderPassCategory::Debug` +
   `ViewScope::SceneView` *specifically so the Frame Debugger's own filters
   exclude it* — confirmed by that file's own inline comment. (An earlier
   draft of this document also named `src/Editor/GpuSkinningValidation.h`
   here — CORRECTED: that file has no `RenderGraphBuilder::AddRenderPass()`/
   `ViewScope`/`RenderPassCategory` involvement at all; it is a fully
   self-contained CPU-vs-GPU numeric parity tool driven by a one-shot
   `Renderer::ImmediateSubmit()` call, never routed through the render
   graph, so it is simply not a Frame Debugger consumer of any kind — not
   in scope for this campaign for a different, simpler reason than
   `ComputeBlurValidation.cpp`.)
6. **`RenderPassEvent` (the design doc's new sort-hint enum,
   `BeforeEverything`/`PreOpaques`/`Opaques`/`AfterOpaques`/`Transparents`/
   `AfterTransparents`/`AfterEverything`) maps cleanly onto the engine's
   real, already-existing execution order**: GPU Skinning dispatch and the
   Atmosphere shared LUTs run first (`PreOpaques`/`BeforeEverything`), each
   view's own Sky-View LUT + Aerial Perspective volume run per-view before
   that view's Opaque draw (`PreOpaques`), `"RenderOpaque"` is `Opaques`,
   `"DrawSkyBackground"` is `AfterOpaques`, `"RenderTransparent"` is
   `Transparents`, and the Aerial Perspective Composite pass is
   `AfterTransparents`. This mapping is the concrete raw material PHASE3
   below builds the real provider registration table from.

## Locked Design Decisions (from direct user Q&A — do not re-litigate these)

These RESOLVE every place where `GENERIC_RENDERPASS_SYSTEM_DESIGN_V2.md`'s
own "Open questions" section, or a genuine ambiguity the design doc does
not cover once matched against the real codebase, needed a real answer.
Every one of these was asked and answered directly by the user in this
campaign's own kickoff session — do not ask them again.

1. **Scene View IS brought into the new per-view system.** The old
   `render-pass-1`/`render-pass-2` rule ("`\"SceneView\"` stays on plain
   `builder.AddPass()` forever, out of scope for the whole campaign") is
   EXPLICITLY REVERSED by this campaign, by the user's own direct
   instruction: `AddRenderOpaquePass()`/`AddSceneViewPass()` are confirmed
   near-identical twins, and `Application.cpp` already hand-duplicates the
   entire per-view sequence — this is precisely the duplication
   `ProviderScope::PerActiveView` exists to remove. See PHASE3.
2. **The Frame Debugger stays untouched, with exactly ONE named
   exception.** `FrameDebuggerData.cpp`/`ComputeBlurValidation.cpp`/
   `GpuSkinningValidation.h`'s existing, heavy reliance on
   `ViewScope`/`RenderPassCategory`/`RenderPassDrawKind` is NOT reworked
   onto the new tag/view system in this campaign — that old, purely
   descriptive metadata keeps being stamped, unchanged, on every migrated
   pass's underlying `PassRecord`, forever, alongside the new fields (see
   Decision 4 below). The ONE exception, already committed to: the literal
   `FindPassByName(..., "RenderOpaque")` pivot search is replaced with a
   structural, name-free lookup using the new `RenderPassEvent` field (the
   design doc's own Section 6 suggestion: "the first pass with `order >=
   Opaques`"). See PHASE4. Nothing else in the Frame Debugger changes.
3. **Phase B's required, real proof-of-concept hand-off is GPU Skinning
   publishing its output buffers, and RenderOpaque fetching them from the
   blackboard** — replacing today's manually-threaded
   `gpuSkinningOutputBuffers` vector parameter for the Opaque pass
   specifically (Scene View's and Present's own copies of that same
   parameter are migrated later, in PHASE3, once those passes themselves
   move onto the new system). See PHASE2.
4. **Migration scope is FULL for production passes, and explicitly
   EXCLUDES Editor-only/debug passes.** By the campaign's end, every
   PRODUCTION pass (Atmosphere LUTs ×4 + composite + debug-slice, Opaque,
   Sky Background, Transparent, GPU Skinning, Scene View's own
   opaque+sky, Present) is declared through the new `RenderPipeline`
   provider system. `AddFrameDebuggerReplayPasses()`'s N debug replay
   passes and `ComputeBlurValidation.cpp`'s own pass are DELIBERATELY LEFT
   on the old, direct `RenderGraphBuilder::AddRenderPass()` call style
   forever — they exist purely to feed the Frame Debugger tooling that
   Decision 2 already keeps unchanged, so migrating them would only add
   dual-maintenance cost for zero benefit.
5. **DIRECT, LOUD DEVIATION from `GENERIC_RENDERPASS_SYSTEM_DESIGN_V2.md`'s
   own Decision 1**: that design doc says the render-graph BUILDER's own
   pass-adding entry point signature changes to accept the new opaque
   types, with the old overload and old enums deleted once migration
   completes. **This campaign does NOT do that.** Because of Decisions 2
   and 4 above (Frame Debugger stays on the old fields forever; Debug-only
   passes stay on the old call style forever), `RenderGraphBuilder`'s
   existing `AddRenderPass()` overloads, and `ViewScope`/`RenderPassCategory`/
   `RenderPassDrawKind` themselves, are **never deleted, and
   `RenderGraphBuilder.h`'s public surface for THOSE specific
   parameters never changes at all**. Instead: `RenderPipeline`
   (the new declaration layer, living strictly above the builder) is
   the ONLY thing that ever sees the new opaque `RenderPassId`/
   `RenderPassTagMask`/`RenderViewId`/`RenderPassEvent` types, and it
   internally TRANSLATES them into the old `ViewScope`/`RenderPassCategory`
   values before making its own, ordinary call into the EXISTING, byte-
   for-byte-unchanged `AddRenderPass()` chokepoint. The only change
   `RenderGraphBuilder.h` genuinely needs is ONE new, trailing, DEFAULTED
   parameter (`RenderPassEvent renderPassEvent = RenderPassEvent::Opaques`)
   on both `AddRenderPass()` overloads — mirroring the exact
   trailing-defaulted-parameter technique `render-pass-2`'s own `drawKind`
   parameter already used, so every pre-existing call site (including
   every Debug-only one) compiles completely unmodified. See PHASE1 for
   the precise mechanics, and PHASE3 for the Game/Scene `ViewScope`
   translation table.
6. **Two separate `RenderPipeline` instances, not one.** `Application`
   today issues TWO separate `RenderGraph::Execute()` calls per frame —
   one SYNCHRONOUS offscreen call (Game View + Scene View together), one
   PIPELINED swapchain call (`"Present"` alone). A single shared
   `RenderPipeline` instance cannot serve both: a provider registered
   `ProviderScope::Once` (e.g. GPU Skinning, Atmosphere shared LUTs) would
   otherwise fire during BOTH `DeclareInto()` calls, silently duplicating
   those passes into the swapchain-regime graph too. This campaign
   therefore introduces `m_offscreenRenderPipeline` (GPU Skinning,
   Atmosphere, Opaque, Sky, Transparent — every pass in the SYNCHRONOUS
   offscreen `Execute()` call) and `m_presentRenderPipeline` (`"Present"`
   alone, in the PIPELINED swapchain `Execute()` call), as two distinct
   `Application` member objects. See PHASE3.

## Step 3: The Plan — Phase Index

Each phase below is its own `PHASEn_*.md` file in this same folder. Work
through them in order — later phases assume earlier ones already landed.
Every child phase file follows the same "Goal / Situation / Plan /
Definition of Done / What We Will NOT Do" shape `render-pass-1`'s own
phases already used — read
`task_manager/render-pass-1/PHASE1_RENDER_PASS_CORE_ABSTRACTION.md` for
the level of code-snippet detail expected in an implementer-facing phase
doc, and one of that campaign's own `PHASEn_COMPLETION_REPORT.md` files for
the level of detail expected in the report each phase below must produce
once actually implemented (not included in this strategy-writing pass —
see "What This Strategy-Writing Pass Does NOT Do" below).

| Phase | File | One-line summary |
|---|---|---|
| 1 | `PHASE1_CORE_VOCABULARY_AND_BLACKBOARD.md` | Add `RenderPassId`/`RenderPassTag`/`RenderPassTagMask`/`RenderViewId`/`RenderPassEvent`/`RenderPassDesc`/`RenderPassProvider`/`RenderPipeline`/`RenderPassBlackboard`/`RenderPassFrameContext`, purely additively, zero real consumers yet — mirrors `render_graphs`' own "Phase 1 ships pure vocabulary" precedent. |
| 2 | `PHASE2_GPU_SKINNING_OPAQUE_BLACKBOARD_PROOF.md` | The FIRST real consumer: register `"GpuSkinning"` and `"RenderOpaque"` as real providers on a new `m_offscreenRenderPipeline`, with GPU Skinning publishing its output buffers onto the blackboard and Opaque fetching them — replacing the hand-threaded parameter for THIS one call site only. |
| 3 | `PHASE3_FULL_PRODUCTION_PASS_MIGRATION_AND_VIEW_UNIFICATION.md` | The heavy phase: migrate every remaining production pass (Atmosphere ×6, Sky, Transparent, Scene View, Present) onto the provider system; collapse `Application.cpp`'s hand-duplicated Game/Scene `if` blocks into one generic per-view loop; stand up the second `m_presentRenderPipeline`. |
| 4 | `PHASE4_FRAME_DEBUGGER_EVENT_PIVOT_FIX.md` | The one, narrow, already-committed Frame Debugger change: replace the literal `"RenderOpaque"`-name pivot search with a structural `RenderPassEvent`-based lookup. Nothing else in the Frame Debugger changes. |
| 5 | `PHASE5_FINAL_INTEGRATION_FULL_BUILD_AND_LIVE_VERIFICATION.md` | The ONLY phase that runs a full build + `ctest` regression suite + a live, HTTP-driven verification. Campaign completion report, `AGENTS.md`/`README.md` updates, and an explicit, loud write-up of every deviation from the original design doc (Locked Design Decisions 5/6 above). |

## What This Strategy-Writing Pass Does NOT Do

This document and its four siblings are the OUTPUT of a strategy-planning
pass, not an implementation pass. Nothing under `src/`, `tests/`, or any
other engine file has been touched by writing these five `.md` files. Each
`PHASEn_*.md` file is a to-do list for whoever implements that phase next —
that implementer is the one who writes real code, runs a real incremental
compile, and produces that phase's own `PHASEn_COMPLETION_REPORT.md`
inside this same folder (not written yet, by design).

## Cross-Cutting Rules For Every Phase (do not repeat verbatim in each
child, but every implementer must follow these)

- **No full build/regression test until PHASE5.** Every earlier phase does
  an INCREMENTAL compile check only (build just the `gte_core`/
  `GreatTamanaEngineTests` targets that were touched — do not run the full
  test suite or a full clean rebuild). Debugging via `run_app_background`
  + `gte_send_request` (visual/HTTP verification of the specific thing that
  phase changed) is encouraged and does NOT count as "full build/full
  regression test" — see Note 5 of this campaign's own kickoff instructions.
- **Every phase ends with**: (a) an incremental compile check (and,
  where useful, a quick live visual/HTTP smoke check of just the thing that
  phase touched), (b) a short Markdown completion report written into this
  SAME folder (`task_manager/render-pass-3/PHASEn_COMPLETION_REPORT.md`),
  (c) a git commit of the code changes + the report together.
- **Stay on branch `feature/render-pass-impl`.** Never switch branches.
  Always read `README.md` and `AGENTS.md` at the repo root before starting
  a phase, and always read the previous phase's own completion report
  first — it may carry a clue, a deviation, or an open question for the
  next phase to pick up.
- **`AGENTS.md`'s general house rules still apply in full**: Clean
  Architecture layering (Renderer never depends on ECS/Editor; only
  Application knows about both), RAII, everything lives in `namespace gte`
  (or `gte::rg` for Render Graph internals), and the Testability/
  Regression-Safety rules (extract Tier-1-testable pure logic wherever the
  problem allows it — `RenderPassBlackboard`, `RenderPipeline`'s own
  sort/collect loop, and the `RenderViewId`/`RenderPassId` hashing are all
  excellent Tier-1 candidates with zero live `VkDevice` needed; add/update
  a matching test file under `tests/` in the SAME change, never as an
  afterthought).
- **IMPORTANT — delegation discipline.** If, while executing ANY phase
  (this one included), you find yourself needing to hand off further work
  via `delegate_task`, you MUST explicitly instruct that delegated task, in
  its own prompt text, to use the `ask_questions` tool for any genuine
  design ambiguity it hits — and to, in turn, pass that SAME instruction on
  to anything IT further delegates. This rule propagates recursively,
  forever, down every level of delegation this campaign ever produces.
- **If you are genuinely unsure about a design choice this document (or
  your own phase file) does not already pin down, use `ask_questions` to
  ask the user directly rather than guessing.** Every phase file below has
  already resolved every design decision the kickoff Q&A session covered;
  if you hit a NEW ambiguity these docs don't already answer, that is
  exactly when to stop and ask.
- **Never re-litigate a Locked Design Decision above.** In particular:
  do NOT attempt to delete `ViewScope`/`RenderPassCategory`/
  `RenderPassDrawKind`, do NOT attempt to rework the Frame Debugger beyond
  PHASE4's one named fix, and do NOT migrate `AddFrameDebuggerReplayPasses()`/
  `ComputeBlurValidation.cpp` onto the new provider system. All three were
  explicitly, directly decided by the user — treat them as settled facts,
  not as open design questions to reconsider.
