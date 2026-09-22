# PHASE5 Strategy-Document Double-Check Report (`render-pass-5`)

## Scope

This was a **strategy-document-only** pre-implementation review of
`PHASE5_RENDERGRAPH_PASS_WIRING_AND_PRODUCTION_CUTOVER.md`, requested by the
project owner as the second of two dedicated pre-checks on this campaign's
highest-risk phases (PHASE0's Locked Design Decision 10), performed BEFORE the
whole 7-phase strategy set gets its general second-iteration review. No source
code was written or modified; no build/compile was run.

Read in full: `readme.md`, `AGENTS.md`, `PHASE0_MASTER_STRATEGY.md`,
`PHASE3_FRUSTUM_CULL_COMPUTE_SHADER_AND_PIPELINE.md`,
`PHASE3_STRATEGY_DOUBLE_CHECK_REPORT.md` (already present in this folder from
the earlier PHASE3 pre-check — it explicitly flags two items relevant to this
phase, both addressed below),
`PHASE4_PER_BATCH_RESOURCE_MANAGEMENT_AND_BATCHING.md`, and
`PHASE5_RENDERGRAPH_PASS_WIRING_AND_PRODUCTION_CUTOVER.md` itself (the focus of
this review). PHASE5's plan was then cross-checked directly against the
actual, current repository source rather than trusted at face value:

- `src/Application/RenderPasses.h/.cpp` (`AddRenderOpaquePass()`,
  `AddDrawSkyBackgroundPass()`, `AddGpuSkinningPasses()`,
  `DeclareGpuSkinningReads()`, `AddFrameDebuggerReplayPasses()`).
- `src/Application/Application.cpp`, **in full for the relevant sections** —
  `RegisterOffscreenRenderPipelineProviders()`,
  `RegisterPresentRenderPipelineProvider()`, and `Run()`'s own frame-build
  code.
- `src/Application/RenderPassViewData.h`.
- `src/Renderer/RenderGraph/RenderPipeline.h` (`RenderPassDesc`,
  `RenderPassProvider`, `ProviderScope`, `ProviderTiming`, `RenderPipeline::
  DeclareInto()`/`DeclareOnePhase()`).
- `src/Renderer/RenderGraph/RenderGraphCompiler.cpp` (`Compile()`,
  `DetectRenderPassEventContradictions()`).
- `src/Renderer/RenderGraph/RenderGraphBarrierPlanner.h` and
  `RenderGraphTypes.h` (`ResourceAccess`, `RenderPassEvent`, `PassRecord`).
- `src/Game/RenderSystem.h/.cpp` (`Draw()`'s two overloads, their existing
  trailing-defaulted-parameter convention, `CollectRenderables()`).
- `src/Game/Game.cpp` (`Game::Render()`).
- PHASE1/PHASE2's own documents, for `IndirectDrawCommand`/
  `GpuCullingInstanceInput`/`Renderer::SubmitIndirect()`'s real signature.

## What was found

PHASE5's original draft was internally coherent and well-reasoned about the
GPU-side mechanics (culling shader wiring, indirect draw call, the
`firstInstance`/`gl_InstanceIndex` trick), but it was written against a **STALE
model of how this engine actually declares its production render passes
today**, plus it carried forward two real gaps `PHASE3_STRATEGY_DOUBLE_CHECK_REPORT.md`
had already flagged as PHASE5's own responsibility, and it never considered
Scene View at all. Five real, load-bearing corrections came out of this pass
and are now folded directly into
`PHASE5_RENDERGRAPH_PASS_WIRING_AND_PRODUCTION_CUTOVER.md` (now overwritten in
place — search that file for "⚠️ CORRECTED" for each one):

1. **Incorrectness (would have wired the new feature into the WRONG, rarely-
   executed code path):** the original plan said to declare the new passes by
   "wiring into `AddRenderOpaquePass()`'s existing pass-building code" and
   calling a new free function "from `Application::Run()`/wherever
   `AddGpuSkinningPasses()` is currently called, likely immediately adjacent to
   it." Direct `search_in_dir` across the whole repository confirms
   `AddRenderOpaquePass(` has **zero real call sites** anywhere — the
   `render-pass-3`/`render-pass-4` campaigns (already shipped) replaced
   `Application::Run()`'s old direct `AddRenderOpaquePass()`/
   `AddGpuSkinningPasses()` calls with a generic `rg::RenderPipeline`/
   `rg::RenderPassProvider` declaration layer
   (`Application::RegisterOffscreenRenderPipelineProviders()`); the real
   `"RenderOpaque"` pass is built by an inline `rg::RenderPassDesc` inside a
   registered PROVIDER, not by calling that free function. `AddGpuSkinningPasses()`
   (the free function) has exactly ONE real caller left — the rare
   direct-render-to-swapchain fallback used only when both Game and Scene
   Editor panels are hidden — the everyday GPU-skinning dispatch instead goes
   through a `"GpuSkinning"` PROVIDER. Had PHASE5 been implemented literally as
   originally written, the new indirect-draw passes would have been declared
   into that same rare fallback path (or nowhere at all, if simply added as an
   unreferenced free function) while `RenderSystem::Draw()`'s new exclusion
   check would still have skipped the batched entities in the REAL,
   everyday Game View path — a genuine, guaranteed "drawn zero times" bug for
   the whole feature in ordinary Editor use. PHASE5's Section 3.1 is
   completely rewritten to register a new `"GpuDrivenBatches"` provider,
   mirroring `"GpuSkinning"`'s own real, shipped shape (buffers imported
   directly against the real `RenderGraphBuilder&` by `Application::Run()`
   itself, immediately before `DeclareInto()`, then read back by the
   provider) instead.
2. **Missing piece, confirmed via `ask_questions` during this pre-check: this
   campaign's whole GPU-driven cutover must be GAME VIEW ONLY.** Neither
   PHASE0 nor PHASE4 mention Scene View anywhere, yet `RenderSystem::Draw()`
   and the `"RenderOpaque"` pass both already run once per ACTIVE VIEW (Game
   AND Scene, different cameras/frustums/color targets), and PHASE4's own
   per-batch buffer cache is keyed only by `(MeshHandle, PipelineHandle)` —
   no view dimension — so it cannot safely serve two different cameras' worth
   of culling results at once. Left unresolved, the natural "simplest"
   implementation (one global exclusion set applied to every `Draw()` call
   this frame, passes declared only against the Game View target) would
   silently make every batched entity disappear from Scene View the moment
   both Editor panels are open together — the DEFAULT layout, and therefore
   near-certain to be hit immediately during manual verification. Asked the
   project owner directly (`ask_questions`); the locked answer is: **Scene
   View, and the direct-render-to-swapchain fallback (see finding 3 below),
   keep drawing every entity — including every batch-eligible one — through
   the fully unmodified per-entity path, forever.** PHASE5 now threads its
   new `Draw()` exclusion parameter into exactly ONE call site (the Game-View
   branch of the `"RenderOpaque"` provider), and its new pass-declaration
   provider early-returns for any non-Game view.
3. **Missing piece, newly identified during this pre-check (not previously
   flagged anywhere in this campaign's docs):** `AddPresentPass()`'s own
   direct-render-to-swapchain fallback branch (reachable only when both Game
   and Scene panels are hidden) is effectively a SECOND "Game View" render
   path, calling `RenderSystem::Draw()` completely independently of the
   `"RenderOpaque"` provider this phase's new provider hooks into. PHASE5 now
   explicitly states this fallback must NEVER receive the new
   `batchedEntities` exclusion parameter either — it stays on the fully
   unmodified per-entity path, exactly like Scene View — an accepted,
   deliberate, narrow scope limitation (that one rare configuration doesn't
   get the performance win) rather than a second, easy-to-miss instance of
   finding 2's own bug shape.
4. **Incorrectness in PHASE5's own stated safety net:** the original draft
   said to "confirm via... `DetectRenderPassEventContradictions()`... that
   this produces no contradiction warning" as the guarantee that the new
   indirect-draw pass is safely ordered after `"RenderOpaque"`'s clear and
   before `"DrawSkyBackground"`'s sky pass. Direct re-read of
   `RenderGraphCompiler.cpp` confirms this detector only inspects a pass's
   `reads` against the nearest preceding writer — it has **no check at all**
   for two writers of the same resource with no intervening read (a pure WAW
   hazard), which is exactly this phase's own shape (`"RenderOpaque"` and the
   new indirect-draw pass both write the same Game View color/depth target;
   neither reads it back). A mis-ordering here — the new pass's un-cleared
   write landing before `"RenderOpaque"`'s clear — would be real, silent pixel
   erasure that this detector would never flag. PHASE5's new Section 3.4
   replaces this false guarantee with the actual one: same-tier passes from
   different `PerActiveView` providers are ordered, after the
   `RenderPipeline`'s own stable sort, exactly by PROVIDER REGISTRATION ORDER
   — so the new `"GpuDrivenBatches"` provider's `Register(...)` call must be
   placed, textually, strictly after `"RenderOpaque"`'s own and strictly
   before `"DrawSkyBackground"`'s own in
   `RegisterOffscreenRenderPipelineProviders()`'s body — verified by an
   explicit live visual/Render-Graph-panel pass-order check, not merely "no
   assert fired."
5. **Missing pieces already flagged by `PHASE3_STRATEGY_DOUBLE_CHECK_REPORT.md`
   as absent from both PHASE4 and this document, now folded in:** (a) the
   atomic visible-count buffer's mandatory per-frame reset (PHASE3's own
   corrected document states this shader can never safely reset it itself,
   and names this phase as the one that must declare the reset step) — PHASE5
   Section 3.1 now declares a real per-batch `"<batch> ResetCount"` pass
   (`vkCmdFillBuffer`, `ResourceAccess::TransferDst`) ordered before the
   culling pass via a natural WAW edge on the shared count-buffer handle; (b)
   the degenerate-padding branch's `Renderer::SubmitIndirect()` draw count
   must be EXACTLY this frame's real `instanceCount`, never PHASE4's
   buffer-capacity-only-grows cache size (a concrete, reachable
   ghost-geometry bug otherwise) — now stated explicitly in Section 3.1.

## What was confirmed already correct (no change needed)

- PHASE5's central GPU-mechanics claims — one compute culling pass + one
  indirect graphics pass per eligible batch, the three-buffer read/write
  shape (`ComputeShaderRead`/`ComputeShaderWrite`/`IndirectCommandRead`/
  `VertexShaderStorageRead`), and the `firstInstance`/`gl_InstanceIndex`
  reasoning it inherits from PHASE3 — are all correct and required zero
  change.
- The claim that zero change to `RenderGraphCompiler.cpp`/
  `RenderGraphBarrierPlanner.cpp` is needed remains correct — this phase is a
  new consumer of already-generic, already-correct machinery; the fix
  required was to how/where this phase's OWN passes get declared and ordered,
  never to that machinery itself.
- PHASE5's own Section 3.2 (resolving a batch's `PositionNormalInstanced`
  pipeline) and its existing Frame Debugger discussion (Section 3.3, now
  renumbered but substantively unchanged) were already appropriately hedged
  with explicit `ask_questions` guidance for genuine remaining
  implementation-time unknowns (whether `Pipeline`/`PipelineHandle` exposes
  enough information to rebuild an instanced variant generically, and whether
  the Frame Debugger per-entity attribution gap is acceptable) — left
  substantively as-is, with only small clarifying additions (the "one source
  of truth" exclusion-set rule, and a note that pipeline-resolution failure
  must feed back into that same rule).

## Flagged for the upcoming whole-campaign second-iteration review (NOT fixed here, per this task's own scope)

- **`PHASE0_MASTER_STRATEGY.md` currently contains no Locked Design Decision
  recording that this campaign's GPU-driven cutover is Game-View-only.** This
  pre-check confirmed the answer directly with the project owner (see finding
  2 above) and folded the consequence into PHASE5's own plan, but the decision
  itself is exactly the kind of campaign-wide fact PHASE0 is supposed to be
  the one, permanent source of truth for — the general review should add it
  there (mirroring how every other Locked Design Decision is phrased) rather
  than leaving it discoverable only inside PHASE5's own prose.
- **PHASE4's own document (`PHASE4_PER_BATCH_RESOURCE_MANAGEMENT_AND_BATCHING.md`)
  never mentions Scene View, or the Game-View-only scope, at all.** Its
  per-batch cache design (keyed by `(MeshHandle, PipelineHandle)` only) is
  CORRECT as a result of the Game-View-only decision (finding 2), but PHASE4's
  own text doesn't say why — a future reader of PHASE4 in isolation could
  reasonably wonder whether the missing view dimension is an oversight. A
  one-line cross-reference to PHASE0's new Locked Design Decision (once added)
  would close this. Not edited here, per this task's explicit scope (PHASE4 is
  a different phase's file).
- Independently, this pre-check's own read of PHASE3's document confirms it
  already correctly anticipated exactly two of the five gaps this report just
  closed (the count-buffer reset and the drawCount/instanceCount invariant) —
  both are now addressed in PHASE5 itself, closing the loop PHASE3's own
  double-check report explicitly left open ("PHASE5's own document should be
  updated during the general review to actually include this step" — done now,
  during this dedicated PHASE5 pre-check, rather than deferred further).

## Outcome

`PHASE5_RENDERGRAPH_PASS_WIRING_AND_PRODUCTION_CUTOVER.md` was overwritten in
place with a corrected, substantially more accurate version: Section 3.1 is
rewritten from a stale free-function design to the real `rg::RenderPipeline`/
`rg::RenderPassProvider` mechanism this engine actually uses today, with an
explicit, load-bearing provider-registration-order requirement (Section 3.4)
replacing a previously-incorrect reliance on
`DetectRenderPassEventContradictions()`; Section 3.3 is corrected to scope the
new `Draw()` exclusion parameter to the Game View only, with the direct-
render-to-swapchain fallback explicitly called out as another path that must
never receive it; the atomic count-buffer reset pass and the
drawCount-must-equal-instanceCount invariant (both previously flagged only in
PHASE3's own double-check report, absent from PHASE4/PHASE5 themselves) are
now explicit, concrete steps in Section 3.1. No other `.md` file in this
campaign was modified, per this task's scope — `PHASE0_MASTER_STRATEGY.md` and
`PHASE4_PER_BATCH_RESOURCE_MANAGEMENT_AND_BATCHING.md` both have a real,
identified cross-reference gap (see the section above) left for the
whole-campaign review to reconcile. No other source code was written or
changed. No build/compile was run (this was a document-only review).
