# PHASE0_MASTER_STRATEGY — Render Pass System Campaign

_Part of the `feature/render-pass-impl` branch. Lives under
`task_manager/render-pass-1/`. This is the ORCHESTRATOR document — every
other `PHASEn_*.md` file in this same folder is a child of this one. Read
this file FIRST, always, before touching any child phase._

## Step 1: The Goal (Where are we going?)

Today this engine already has a genuinely mature, fully-shipped **Render
Graph orchestrator** (`src/Renderer/RenderGraph/` — `RenderGraphBuilder`,
`RenderGraphCompiler`, `RenderGraphBarrierPlanner`, `RenderGraph` itself —
the product of the whole `render_graphs` 9-phase campaign,
`task_manager/render_graphs/RENDERGRAPH_PHASE0_MASTER_STRATEGY_v2.md`
through `RENDERGRAPH_PHASE9_ADVANCED_FUTURE_SCOPE_STRATEGY_v2.md`). That
orchestrator is NOT what's broken and is explicitly OUT OF SCOPE for this
campaign to rewrite — do not touch `RenderGraphCompiler`'s dependency/
culling logic, `RenderGraphBarrierPlanner`'s barrier synthesis, or
`RenderGraphResourcePool`'s physical-resource pooling. They work, they are
well tested, and they are the "manager" half of the kitchen analogy the
user already gave us.

What IS naive/messy today is the **Render Pass** layer sitting on top of
that orchestrator — every real pass in this engine (Atmosphere LUT
compute dispatches, the Sky Background draw, the "GameView" mesh-drawing
pass, the Aerial Perspective composite, GPU Skinning dispatches, the
Present pass, the Frame Debugger's own replay passes, the Compute Blur
Validation debug pass) is declared as an ad-hoc free function that builds
two raw lambdas and throws them directly at
`RenderGraphBuilder::AddPass()`/`AddComputePass()`, scattered across
`src/Application/RenderPasses.cpp`, `src/Application/AtmospherePassSequence.cpp`,
and `src/Renderer/Atmosphere/AtmosphereLutRenderer.cpp`, with zero shared
structure beyond "eventually calls `builder.AddPass(name, ...)`". Worse,
the Sky Background draw is not even its own pass — it is HAND-FUSED
inside the `"GameView"` pass's own `execute` lambda
(`AddGameViewPass()`, `src/Application/RenderPasses.cpp`), made visible to
the Frame Debugger only via a hardcoded `isSkyBackgroundDraw`/
`RecordSkyBackgroundDraw()` special case
(`src/Editor/FrameDebuggerCapture.h`). This is exactly the "naive render
pass" the user is unhappy with.

**The goal of this campaign is to give every render/compute/blit
operation in this engine a single, uniform, first-class "Render Pass"
declaration mechanism** (a thin, reusable chokepoint built directly on
top of the existing `RenderGraphBuilder::AddPass()`/`AddComputePass()`
API — NOT a rewrite of the orchestrator, NOT a heavyweight polymorphic
class hierarchy), split the current monolithic `"GameView"` pass into
real, separate, individually-inspectable passes (`RenderOpaque` then
`DrawSkyBackground`), scaffold a `RenderTransparent` extension point for
the future, migrate every existing pass in the engine onto this new
mechanism, and rework the Frame Debugger's tree-building logic so every
one of these passes is GENERICALLY discoverable — never another
hand-maintained special case — producing a clean, readable event tree for
the test scene along these lines:

```
"GameView"
  |-- "Compute LUT"                         (Transmittance/MultiScattering/SkyView/AerialVolume LUTs)
  |-- "Compute Dispatches (Pre-GameView)"    (anything else pre-view, e.g. GPU Skinning - only if present)
  |-- "RenderOpaque"                         (the built-in default mesh-drawing pass - per-entity children, unchanged mechanism)
  |-- "DrawSkyBackground"                    (a REAL, separate pass now - no more hack)
  |-- "RenderTransparent"                    (scaffolded, real no-op today - see PHASE2)
  |-- "Compute Dispatches (Post-GameView)"   (AerialPerspectiveComposite, ComputeBlurValidation, ...)
```

matching the spirit of the user's own example list (`Compute LUT` / `Draw
Sky LUT` / `Render Opaque` / `Draw Aerial Perspective LUT`) while keeping
the technically-correct real GPU ordering (Opaque must still be drawn
BEFORE the Sky Background, since the Sky Background pass relies on an
`EQUAL` depth-test against the depth buffer Opaque just wrote, to avoid
overdraw — see PHASE2's own "Locked Design Decision" about this).

## Step 2: The Situation (Where are we now?)

Confirmed by direct source inspection (this document is the single
source of truth for every fact quoted in every child phase — do not
re-derive these from scratch):

1. **The Render Graph orchestrator is real, tested, and complete.**
   `src/Renderer/RenderGraph/RenderGraphBuilder.h`'s `AddPass()`/
   `AddComputePass()` (two overloads each — a 3-argument form and a
   4-argument form that also stamps `ViewScope`) are the ONLY two entry
   points that exist today for declaring a pass. `PassRecord`
   (`RenderGraphTypes.h`) already carries `name`, `reads`, `writes`,
   `isCulled`, `isComputePass` (a plain `bool`), `viewScope`
   (`ViewScope::Shared`/`GameView`/`SceneView`), `execute`, and optional
   clear values.
2. **Every real pass today is a free function that builds two lambdas.**
   See `src/Application/RenderPasses.cpp` (`AddGameViewPass()`,
   `AddSceneViewPass()`, `AddPresentPass()`, `AddGpuSkinningPasses()`,
   `AddFrameDebuggerReplayPasses()`), `src/Application/AtmospherePassSequence.cpp`
   (`AddAtmosphereSharedLutPasses()`, `AddAtmosphereViewLutPasses()`,
   `AddAtmosphereCompositePass()`), and
   `src/Renderer/Atmosphere/AtmosphereLutRenderer.h/.cpp` (the actual
   `AddTransmittanceLutPass()`/`AddMultiScatteringLutPass()`/
   `AddSkyViewLutPass()`/`AddAerialPerspectiveVolumePass()`/
   `AddAerialPerspectiveVolumeDebugSlicePass()`/
   `AddAerialPerspectiveCompositePass()` methods this campaign leans on).
   There is no shared struct/class/chokepoint any of these funnel
   through beyond the two raw `RenderGraphBuilder` methods themselves.
3. **`"GameView"` is one pass doing two unrelated jobs.**
   `AddGameViewPass()` (`RenderPasses.cpp`) both (a) calls
   `game.Render(...)` (draws every entity with a `MeshRenderer` — see
   `RenderSystem::Draw()`/`CollectRenderables()`, which has NO opaque/
   transparent distinction anywhere — there is no `isTransparent` flag on
   `MeshRenderer` at all today) and (b), immediately afterward, still
   inside the SAME `vkCmdBeginRendering`/`vkCmdEndRendering` bracket,
   calls `recordSkyBackground(ctx.cmd)` (which ultimately reaches
   `AtmosphereSkyBackgroundRenderer::Draw()`), then, ONLY in an Editor
   build, directly dereferences `frameDebuggerCapture->RecordSkyBackgroundDraw(...)`
   to fake a Frame Debugger leaf for it. `FrameDebuggerCaptureContext`
   (`src/Editor/FrameDebuggerCapture.h`) carries a dedicated
   `isSkyBackgroundDraw` bool on `FrameDebuggerDrawRecord` purely to make
   this fusion look, after the fact, like it was its own event — a real,
   confirmed workaround, not a design.
4. **The Frame Debugger's tree builder hardcodes the literal pass name
   `"GameView"`.** `BuildRealFrameDebuggerSnapshot()`
   (`src/Editor/FrameDebuggerData.cpp`) searches
   `graphSnapshot.passesInExecutionOrder` for a pass literally named
   `"GameView"`, uses ITS execution-order index as the pivot for
   `"Compute Dispatches (Pre-GameView)"` vs. `"(Post-GameView)"`
   grouping, and reads `FrameDebuggerCaptureContext::DrawRecords()` for
   its per-entity (and per-sky-hack) children. Every real compute pass
   (any `isComputePass == true` survivor) is already GENERICALLY
   discovered this way (no hardcoded name list) — this is the PROVEN
   precedent this campaign's own Frame Debugger phase (PHASE4) extends
   to graphics passes too.
5. **`Application::Run()` (`src/Application/Application.cpp`)** is the
   one, giant, ~1300-line function that wires every one of the free
   functions above together by hand, in a fixed order, inside one nested
   `build` lambda passed to `m_renderGraph.Execute(...)`. This is the
   literal call-site order every child phase below must preserve
   (compute passes, then Game View atmosphere passes, then the view pass
   itself, then the composite pass) unless a phase explicitly says
   otherwise.
6. **There is no transparency concept anywhere in the ECS/render
   pipeline.** `ECS/Components/MeshRenderer.h` has no `isTransparent`/
   `renderQueue` field; `RenderSystem::CollectRenderables()` returns
   every entity with a `MeshRenderer` unconditionally. "Render Opaque"
   today, in practice, already IS "render everything" — there is nothing
   to filter yet. This campaign's `RenderTransparent` pass is therefore,
   correctly and deliberately, a structural no-op today (mirrors the
   already-proven `AddGpuSkinningPasses()` "return an empty vector, add
   nothing, whenever there's genuinely nothing to do" pattern) — a clean
   drop-in point for a REAL future transparency campaign, not a
   speculative implementation of transparency itself.

## Step 3: The Plan — Phase Index

Each phase below is its own `PHASEn_*.md` file in this same folder. Work
through them in order — later phases assume earlier ones already landed.
Every phase file follows the same "Goal / Situation / Plan / Definition
of Done / What We Will NOT Do" shape this codebase's own existing
`task_manager/*` campaigns already use — read one of those (e.g.
`task_manager/render_graphs/RENDERGRAPH_PHASE7_APPLICATION_MIGRATION_STRATEGY_v2.md`)
if you want a feel for the level of detail expected in a completion
report.

| Phase | File | One-line summary |
|---|---|---|
| 1 | `PHASE1_RENDER_PASS_CORE_ABSTRACTION.md` | New `PassKind`/`RenderPassCategory` vocabulary + one unified `RenderGraphBuilder::AddRenderPass()` chokepoint every future pass declaration goes through. |
| 2 | `PHASE2_RENDER_OPAQUE_SKY_SPLIT_AND_TRANSPARENT_STUB.md` | Split `"GameView"` into real `"RenderOpaque"` + `"DrawSkyBackground"` passes; add the `"RenderTransparent"` no-op scaffold pass. |
| 3 | `PHASE3_ATMOSPHERE_PASSES_MIGRATION.md` | Migrate every Atmosphere LUT/composite pass declaration onto the new chokepoint, tagged `RenderPassCategory::AtmosphereLut`. |
| 4 | `PHASE4_FRAME_DEBUGGER_GENERIC_TREE_REWORK.md` | Rewrite `BuildRealFrameDebuggerSnapshot()` to build the tree generically from `PassKind`/`RenderPassCategory`/execution order — remove the `"GameView"`-literal search and the `isSkyBackgroundDraw` hack entirely. |
| 5 | `PHASE5_REMAINING_PASSES_MIGRATION.md` | Migrate GPU Skinning, Present, Frame Debugger Replay passes, and Compute Blur Validation onto the same chokepoint (full-engine coverage). |
| 6 | `PHASE6_APPLICATION_ORCHESTRATION_CLEANUP_AND_DOCS.md` | Update `Application::Run()`'s call sites to the renamed functions, delete now-dead code, update `AGENTS.md`/`docs/conventions/frame-debugger.md`. |
| 7 | `PHASE7_FINAL_INTEGRATION_FULL_BUILD_AND_LIVE_VERIFICATION.md` | The ONLY phase that runs a full build + `ctest` regression suite + a live, HTTP-driven Frame Debugger screenshot check. Campaign completion report. |

## Locked Design Decisions (from user Q&A — do not re-litigate these)

1. **Shape of the abstraction**: lightweight, data/lambda-based, built
   directly on top of the existing `AddPass(setup, execute)` pattern —
   explicitly NOT a polymorphic `class IRenderPass` hierarchy with
   virtual methods. See PHASE1.
2. **Opaque vs. Sky**: SPLIT into two real, separate `RenderGraphBuilder`
   passes (two real `vkCmdBeginRendering`/`vkCmdEndRendering` brackets
   against the same target, back-to-back, second one using
   `VK_ATTACHMENT_LOAD_OP_LOAD` so it doesn't erase the first one's
   pixels) — not just a display-only relabel. See PHASE2.
3. **Migration scope**: FULL migration — every existing pass in the
   engine (Atmosphere LUTs ×4 + composite + debug-slice, Sky Background,
   Opaque, GPU Skinning dispatch, Present, Frame Debugger replay passes,
   Compute Blur Validation) ends this campaign declared through the new
   chokepoint. See PHASE3/PHASE5.
4. **Breaking changes**: EXPLICITLY ALLOWED and expected — this campaign
   WILL change the Frame Debugger's exact tree shape/pass names (e.g.
   `"GameView"` the PASS no longer exists; `"RenderOpaque"` takes its
   place with the per-entity children moving there), exactly matching
   this codebase's own well-established precedent (`frame-debugger-5`
   through `frame-debugger-9` each did this at least once). Document
   every one, loudly, in each phase's own completion report and in
   `docs/conventions/frame-debugger.md` (PHASE6).
5. **"Compute LUT" grouping**: the 3 (or 4, including Aerial Perspective
   Volume) Atmosphere LUT compute passes stay SEPARATE, individually
   selectable passes/leaves — they are only GROUPED under one clearer
   `"Compute LUT"` tree heading, replacing what today is lumped
   together, unlabeled, inside the generic `"Compute Dispatches
   (Pre-GameView)"` bucket. Non-atmosphere pre-view compute passes (e.g.
   GPU Skinning) keep the existing generic `"Compute Dispatches
   (Pre-GameView)"` heading, as a SIBLING group, not merged with `"Compute
   LUT"`. See PHASE4.
6. **Transparency scaffold**: YES, add a real (if currently always-empty)
   `AddRenderTransparentPass()` call site, mirroring
   `AddGpuSkinningPasses()`'s own established "no-op when there is
   nothing to do yet" pattern — never a speculative transparency
   pipeline. See PHASE2.
7. **Final verification**: the LAST phase (PHASE7) ONLY does a full
   build + `ctest` regression run AND a live, `gte_send_request`-driven
   Frame Debugger screenshot check of the real running engine, confirmed
   against this document's own target tree shape above.

## Cross-Cutting Rules For Every Phase (do not repeat verbatim in each
child, but every implementer must follow these)

- **No full build/regression test until PHASE7.** Every earlier phase
  does an INCREMENTAL compile check only (`cmake --build build --target
  GreatTamanaEngine` or similar — pick whatever narrow target proves the
  files you touched compile; do not run the full test suite or a full
  clean rebuild).
- **Every phase ends with**: (a) an incremental compile check, (b) a
  short Markdown completion report written into this SAME folder
  (`task_manager/render-pass-1/PHASEn_COMPLETION_REPORT.md`), (c) a git
  commit of the code changes + the report together.
- **Stay on branch `feature/render-pass-impl`.** Never switch branches.
  Always read `README.md` and `AGENTS.md` at the repo root before
  starting a phase, and always read the previous phase's own completion
  report first — it may carry a clue, a deviation, or an open question
  for the next phase to pick up.
- **`AGENTS.md`'s general house rules still apply in full**: Clean
  Architecture layering (Renderer never depends on ECS/Editor; only
  Application knows about both), RAII, everything lives in `namespace
  gte` (or `gte::rg` for Render Graph internals), and the
  Testability/Regression-Safety rules (extract Tier-1-testable pure
  logic wherever the problem allows it — several of this campaign's new
  pieces, like the `PassKind`→string helper or the Frame Debugger's new
  grouping-by-category logic, are excellent Tier-1 candidates; add/update
  a matching test file under `tests/` in the SAME change, never as an
  afterthought).
- **IMPORTANT — delegation discipline.** If, while executing ANY phase
  (this one included), you find yourself needing to hand off further work
  via `delegate_task`, you MUST explicitly instruct that delegated task,
  in its own prompt text, to use the `ask_questions` tool for any genuine
  design ambiguity it hits — and to, in turn, pass that SAME instruction
  on to anything IT further delegates. This rule propagates recursively,
  forever, down every level of delegation this campaign ever produces.
- **If you are genuinely unsure about a design choice this document (or
  your own phase file) does not already pin down, use `ask_questions` to
  ask the user directly rather than guessing.** Every phase file below
  has already resolved every design decision this document's Q&A session
  covered; if you hit a NEW ambiguity these docs don't already answer,
  that is exactly when to stop and ask.
