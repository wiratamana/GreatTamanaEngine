# PHASE5 — Render-Graph Pass Wiring + Production Cutover ⚠️ HIGH-RISK / DEDICATED DOUBLE-CHECK PHASE

## Parent
`PHASE0_MASTER_STRATEGY.md` — read it first. Also re-read `PHASE1`–`PHASE4`'s
own completion reports (PHASE3's dedicated double-check findings in
particular — any discrepancy it found must be resolved before this phase
starts, not carried forward silently).

**This is the second of the two dedicated-double-check phases named in
PHASE0's Locked Design Decision 10.** It is also the single highest-risk
phase in the whole campaign: it is the ONLY phase that changes this engine's
actual, currently-100%-reliable, production `RenderSystem::Draw()` hot path.
Immediately after this phase's own completion report is committed, delegate a
SEPARATE, DEDICATED `delegate_task` review of ONLY this phase's diff before
moving on to PHASE6.

> **Pre-implementation strategy-document double-check (2026-09-22).** This
> document was cross-checked against the actual, current repository source —
> `src/Application/RenderPasses.h/.cpp`, `src/Application/Application.cpp`
> (in full, including `RegisterOffscreenRenderPipelineProviders()`/
> `RegisterPresentRenderPipelineProvider()`/`Run()`), `src/Application/
> RenderPassViewData.h`, `src/Renderer/RenderGraph/RenderPipeline.h`,
> `src/Renderer/RenderGraph/RenderGraphCompiler.cpp`,
> `RenderGraphBarrierPlanner.h`, `RenderGraphTypes.h`, `src/Game/RenderSystem.h/
> .cpp`, `src/Game/Game.cpp`, PHASE1–4's own documents, and
> `PHASE3_STRATEGY_DOUBLE_CHECK_REPORT.md` — **before any of this phase's own
> code was written**, per the project owner's request for a dedicated pre-check
> on this campaign's two highest-risk phases. This pass found the original
> draft was written against a STALE mental model of how production passes
> actually get declared in this engine today, plus several genuine correctness
> gaps. Every "⚠️ CORRECTED" marker below is a real, load-bearing fix, not a
> style change:
>
> 1. **The single biggest finding: `AddRenderOpaquePass()`/`AddGpuSkinningPasses()`
>    are NOT called the way this document's original draft assumed.** The
>    `render-pass-3`/`render-pass-4` campaigns (already shipped, both landed
>    well before this campaign started) replaced `Application::Run()`'s old
>    direct, hand-ordered `AddRenderOpaquePass(frame.builder, ...)`-style calls
>    with a generic `rg::RenderPipeline`/`rg::RenderPassProvider` declaration
>    layer (`src/Renderer/RenderGraph/RenderPipeline.h`,
>    `Application::RegisterOffscreenRenderPipelineProviders()`). Confirmed by
>    direct `search_in_dir`: `AddRenderOpaquePass(` has **zero real call
>    sites** anywhere in this repository — the "RenderOpaque" pass the engine
>    actually runs today is built by an inline `rg::RenderPassDesc` inside the
>    `"RenderOpaque"` provider registered in that same function, not by calling
>    the free function this document used to describe as the wiring target.
>    `AddGpuSkinningPasses()` (the free function) has exactly ONE real
>    production caller left — the rare "both Game and Scene panels hidden,
>    render straight to the swapchain" fallback inside
>    `RegisterPresentRenderPipelineProvider()` — the ordinary, everyday
>    per-frame GPU-skinning dispatch goes through a `"GpuSkinning"` PROVIDER
>    instead, which builds its own `rg::RenderPassDesc` directly. **This
>    document's entire Section 3.1 has been rewritten** to wire into the REAL
>    mechanism (a new `"GpuDrivenBatches"` provider), not a free function
>    "called adjacent to `AddGpuSkinningPasses()`" the old way.
> 2. **Missing piece, confirmed via `ask_questions` during this pre-check: this
>    campaign's GPU-driven cutover is GAME VIEW ONLY.** Neither PHASE0 nor
>    PHASE4 mention Scene View at all, yet `RenderSystem::Draw()` and the
>    `"RenderOpaque"` pass both already run once per ACTIVE VIEW (Game AND
>    Scene, each with its own camera/frustum/`colorTarget`) — PHASE4's own
>    per-batch buffer cache is keyed only by `(MeshHandle, PipelineHandle)`,
>    with no view dimension at all, so it cannot correctness-safely serve two
>    different cameras' culling results at once. Left unresolved, the natural
>    "simplest" implementation (thread one global `batchedEntities` exclusion
>    set into every `Draw()` call this frame, declare the new passes only
>    against the Game View target) would silently make every batched entity
>    disappear from Scene View the moment both Editor panels are open
>    together — the DEFAULT layout. **Confirmed, locked answer (this
>    pre-check's own `ask_questions` call): Scene View (and the direct-render-
>    to-swapchain fallback — see point 6 below) keep drawing EVERY entity,
>    including every batch-eligible one, through the fully unmodified
>    per-entity path FOREVER.** The `batchedEntities` exclusion parameter is
>    threaded into ONE call site only: the Game-View branch of the
>    `"RenderOpaque"` provider. See Section 3.3 below.
> 3. **Incorrectness: the original draft's own safety-net claim was wrong.**
>    It said to "confirm via... `DetectRenderPassEventContradictions()`... that
>    this produces no contradiction warning" as the check that the new
>    indirect-draw pass is safely ordered after `"RenderOpaque"`'s clear.
>    Direct re-read of `RenderGraphCompiler.cpp` confirms
>    `DetectRenderPassEventContradictions()` only inspects a pass's `reads`
>    against the nearest preceding writer — it has **no WAW (write-after-write,
>    no intervening read) check at all**. Two same-tier passes that both WRITE
>    the same color/depth attachment (exactly this campaign's own shape: the
>    new indirect-draw pass and `"RenderOpaque"` both `WriteColorAttachment`/
>    `WriteDepthStencilAttachment` against the same Game View target, one
>    clearing, one not) get a WAW dependency edge either way — but the
>    detector is silent regardless of which DIRECTION that edge ends up
>    pointing. A mis-ordering here (the new pass's un-cleared write landing
>    BEFORE `"RenderOpaque"`'s clear) would be real, silent pixel-erasure, and
>    this detector would never flag it. See Section 3.1/3.4 below for the
>    actual, concrete guarantee this document now relies on instead
>    (registration-order placement + a live visual check), and Section 3.4 for
>    why this must be treated as load-bearing, not decorative.
> 4. **Missing piece (already flagged by `PHASE3_STRATEGY_DOUBLE_CHECK_REPORT.md`
>    as absent from BOTH PHASE4 and this document): the atomic visible-count
>    buffer's mandatory per-frame reset.** PHASE3's own corrected document
>    states this shader can never safely reset its own count buffer and names
>    this phase as the one that must declare the reset step — this document's
>    prior draft never did. Section 3.1 now declares a real per-batch reset
>    pass.
> 5. **Missing piece (same source report): the degenerate-padding branch's
>    `SubmitIndirect()` draw count must be EXACTLY this frame's `instanceCount`,
>    never PHASE4's buffer-capacity-only-grows cache size** (a concrete,
>    reachable ghost-geometry bug otherwise). Never stated anywhere in this
>    document's prior draft — now made explicit in Section 3.1.
> 6. **Missing piece, newly identified during this pre-check:
>    `AddPresentPass()`'s own direct-render-to-swapchain fallback branch**
>    (`RegisterPresentRenderPipelineProvider()`, reachable only when both Game
>    and Scene panels are hidden) is effectively a SECOND "Game View" render
>    path that also calls `RenderSystem::Draw()` — via `Game::Render()` with no
>    `frameDebuggerCapture`/`maxDrawCount` — completely independently of the
>    offscreen `m_offscreenRenderPipeline`/`"RenderOpaque"` provider path this
>    document's own new provider hooks into. If a future edit ever threaded the
>    SAME global `batchedEntities` exclusion set into that fallback's own
>    `Draw()` call without ALSO declaring the new indirect passes there, it
>    would reproduce the exact same "drawn zero times" bug class point 2 above
>    describes, just gated behind a rarer configuration. This document now
>    explicitly says: **do not thread `batchedEntities` into that fallback
>    path either** — it stays on the fully unmodified per-entity draw forever,
>    exactly like Scene View. This is a deliberate, accepted, narrow scope
>    limitation (that one configuration simply never gets the performance
>    win), not a correctness gap, PROVIDED the exclusion set is never passed
>    there — see Section 3.3.
>
> Everything below reflects these corrections directly; search for "⚠️
> CORRECTED" for the specific spots.

## Step 1: The Goal (Where are we going?)

Wire PHASE3's real culling shader and PHASE4's real per-batch data into the
render graph, for real, in production, for the Game View only:

1. For every batch PHASE4 identified as eligible this frame, declare, per
   batch: a small reset pass (zeroes the atomic visible-count buffer), a real
   compute culling pass (`ReadBuffer(inputHandle, ComputeShaderRead)`,
   `WriteBuffer(indirectCommandHandle, ComputeShaderWrite)`,
   `WriteBuffer(countHandle, ComputeShaderWrite)`), and a real graphics pass
   reading that same buffer (`ReadBuffer(indirectCommandHandle,
   IndirectCommandRead)`, `ReadBuffer(countHandle, IndirectCommandRead)`, and
   a THIRD, independent read against the ORIGINAL per-instance input buffer,
   `ReadBuffer(inputHandle, ResourceAccess::VertexShaderStorageRead)` — see
   3.1 below for why this is a second, independent declaration against the
   SAME `inputHandle` the culling pass already read, not a duplicate) and
   calling `Renderer::SubmitIndirect()` instead of a per-entity
   `Renderer::Submit()` loop.
2. `RenderSystem::Draw()` skips any `DrawCommand` that belongs to a batch
   PHASE4 marked eligible THIS frame — but **only for the Game View's own
   draw call** (⚠️ CORRECTED — see the preface note above and Section 3.3).
   Scene View, and the rare direct-render-to-swapchain fallback
   (`AddPresentPass()`'s own fallback branch), keep drawing every entity,
   batch-eligible or not, through the byte-for-byte-unchanged existing
   per-entity loop, forever.
3. Zero change to `RenderGraphCompiler.cpp`/`RenderGraphBarrierPlanner.cpp`
   production logic — this phase is purely a new consumer of already-existing,
   already-correct machinery (confirm this claim empirically via this phase's
   own build/test run; if it turns out false, STOP and `ask_questions` before
   changing either file, per PHASE0's own Non-Goals). Note: this does NOT mean
   every ordering question in this feature is automatically safe merely
   because no compiler file changed — see 3.1/3.4 for the one real ordering
   hazard that remains the IMPLEMENTER'S responsibility to get right (correct
   provider REGISTRATION PLACEMENT), since the compiler's own
   `DetectRenderPassEventContradictions()` safety net does not cover it (see
   the preface note's point 3).

## Step 2: The Situation (Where are we now?) — ⚠️ CORRECTED throughout

- **The real, current pass-declaration mechanism for the Game/Scene View
  regime is `rg::RenderPipeline`/`rg::RenderPassProvider`
  (`src/Renderer/RenderGraph/RenderPipeline.h`), not the raw
  `RenderGraphBuilder::AddRenderPass()` chokepoint called directly.**
  `Application::RegisterOffscreenRenderPipelineProviders()`
  (`Application.cpp`, ~line 298) registers, in order: `"AtmosphereSharedLut"`
  (Once), `"GpuSkinning"` (Once), `"AtmosphereViewLut"` (PerActiveView),
  `"RenderOpaque"` (PerActiveView), `"DrawSkyBackground"` (PerActiveView),
  `"RenderTransparent"` (PerActiveView), `"AtmosphereComposite"`
  (PerActiveView, `ProviderTiming::AfterDeferredPasses`). Every one of these
  builds a `rg::RenderPassDesc` (a `debugName`/`kind`/`order`/`view`/
  `legacyCategory`/`drawKind`/`setup`/`execute` plain struct) and appends it to
  a per-phase scratch list; `rg::RenderPipeline::DeclareOnePhase()`
  stable-sorts that list by `RenderPassDesc::order` (the `RenderPassEvent`)
  and only THEN calls `builder.AddRenderPass(...)` for each one, in that
  sorted order — this is what actually assigns each pass its real,
  underlying declaration index (`PassRecord`'s position in
  `RenderGraphCompiler::Compile()`'s `input.passes`).
- **Consequence load-bearing for this phase: for two passes sharing the exact
  same `RenderPassEvent` tier (as `"RenderOpaque"` and this phase's own new
  indirect-draw pass will), their relative order after that stable sort is
  EXACTLY their relative order of COLLECTION into the scratch list** — which,
  for two different `PerActiveView` providers, is their REGISTRATION order
  (the order their `Register(...)` calls appear in
  `RegisterOffscreenRenderPipelineProviders()`'s own body), since
  `DeclareOnePhase()` loops `m_providers` in registration order and, for each
  one, loops `frame.activeViews`. **This is the actual mechanism this phase's
  own new pass must be registered relative to `"RenderOpaque"`/
  `"DrawSkyBackground"` correctly through — see 3.1/3.4.**
- **`AddRenderOpaquePass()`/`AddDrawSkyBackgroundPass()`/
  `AddRenderTransparentPass()` (the free functions in
  `src/Application/RenderPasses.h/.cpp`) are NOT called from
  `RegisterOffscreenRenderPipelineProviders()` at all** — confirmed by direct
  `search_in_dir` for each name: `AddRenderOpaquePass(` and
  `AddDrawSkyBackgroundPass(` have zero real call sites anywhere in this
  repository today; the `"RenderOpaque"`/`"DrawSkyBackground"` PROVIDERS
  re-implement the same logic inline (same clear color/depth constants,
  same `DeclareGpuSkinningReads()` helper call, same `MakeRecordSkyBackgroundCallback()`
  construction) rather than calling those free functions. The free functions
  still exist, still compile, and are still referenced (in comments, and by
  `AddFrameDebuggerReplayPasses()`, which is unrelated to this phase and stays
  untouched) — they are simply not this campaign's actual wiring target
  anymore. **Do not add this phase's own code to those free functions and
  assume it will run in production — it will not be called.**
- **`AddGpuSkinningPasses()` (the free function) has exactly ONE real
  production caller**: `RegisterPresentRenderPipelineProvider()`'s own
  `"Present"` provider, gated on `m_needsDirectGameRenderThisFrame` (true only
  when BOTH Game and Scene Editor panels are hidden, rendering directly to the
  swapchain). The ordinary, everyday per-frame GPU skinning dispatch instead
  goes through the `"GpuSkinning"` PROVIDER (`Application.cpp`, ~line 350),
  which builds its OWN `rg::RenderPassDesc` in a loop over
  `m_gpuSkinningRequestsThisFrame`/`m_gpuSkinningHandlesThisFrame` — TWO
  members `Application::Run()` populates itself, by calling
  `builder.ImportBuffer(...)` DIRECTLY (against the same real
  `RenderGraphBuilder&` this frame's graph is being built against),
  **immediately BEFORE calling `m_offscreenRenderPipeline.DeclareInto()`**.
  **This exact pattern — resolve/import outside any provider, store in a
  member, have the provider merely read that member back — is the one this
  phase's own new batches must copy**, because `rg::RenderPassDesc::setup` is
  a `std::function<void(RenderGraphBuilder::PassBuilder&)>`: it receives a
  `PassBuilder&` for declaring THIS pass's own reads/writes, never the outer
  `RenderGraphBuilder&` itself, so `ImportBuffer()` (a `RenderGraphBuilder`
  method) can never be called from inside a deferred `setup`/`execute`
  lambda.
- **Confirmed, locked scope decision (this pre-check's own `ask_questions`
  call): this campaign's GPU-driven cutover is GAME VIEW ONLY.** Scene View
  (`ViewScope`/`RenderViewId::Named("Scene")`) and the direct-render-to-
  swapchain fallback (`AddPresentPass()`'s fallback branch) both keep drawing
  every entity — batch-eligible or not — through the fully unmodified
  per-entity `Renderer::Submit()` path, forever. Nothing in PHASE4's own
  per-batch cache design needs to change as a result (it is correctly keyed
  by `(MeshHandle, PipelineHandle)` alone, with no view dimension, precisely
  BECAUSE only one view — Game — ever touches it) — **now recorded verbatim
  as `PHASE0_MASTER_STRATEGY.md`'s own Locked Design Decision 11** (added
  during the whole-campaign second-iteration review this document itself
  anticipated needing — see that decision for the permanent, campaign-wide
  record of this scope limitation; this document remains the place that
  explains WHY it's implemented the way it is).
- `RenderSystem::Draw()` (`RenderSystem.cpp`, line ~92/~98, two overloads)
  currently iterates EVERY `DrawCommand` from `CollectRenderables()`
  unconditionally (`consideredCount`/`maxDrawCount` early-exit aside — an
  unrelated, existing Frame Debugger replay mechanism, must not be disturbed).
  Both overloads already end in two trailing DEFAULTED parameters
  (`capture`/`maxDrawCount`) — this phase adds ONE MORE trailing defaulted
  parameter after those two (e.g. `const std::unordered_set<Entity>&
  batchedEntities = {}`), mirroring the exact same "every pre-existing call
  site compiles/behaves unchanged against its default" convention already
  used twice in this same function's own signature.
- **`DetectRenderPassEventContradictions()` (`RenderGraphCompiler.cpp`,
  render-pass-4 campaign) only examines a pass's `reads` against the nearest
  preceding writer in effective order — it has NO check for two writers of
  the same resource with no intervening read (a pure WAW hazard), which is
  EXACTLY this phase's own `"RenderOpaque"`-vs-new-indirect-draw-pass shape**
  (both `WriteColorAttachment`/`WriteDepthStencilAttachment` the same Game
  View target; the new pass never READS it). Confirmed by direct re-read of
  that function's source. **This detector must not be relied upon as the
  correctness guarantee for this specific ordering requirement** — see 3.1/3.4
  for the actual guarantee (registration placement) and the actual
  verification (a live visual check), which this document's prior draft
  conflated with the detector's own, narrower guarantee.
- A batch's indirect graphics pass must write into the SAME color/depth
  attachment the Game-View `"RenderOpaque"` provider writes into, and must run
  strictly AFTER it (so its own un-cleared write is never erased by
  `"RenderOpaque"`'s clear) and strictly BEFORE `"DrawSkyBackground"` (so Sky
  Background's own `EQUAL`-depth-test trick correctly treats a batched
  instance's pixels as "already covered by real geometry," not "empty sky") —
  see 3.1/3.4 for exactly how this ordering is guaranteed.

## Step 3: The Plan

### 3.1 — New `"GpuDrivenBatches"` render-pipeline provider ⚠️ CORRECTED (rewritten from a free-function design)

**Do not add a bare free function called directly from `Application::Run()`
"adjacent to `AddGpuSkinningPasses()`"** — that call site is the rare
direct-render-to-swapchain fallback (see Step 2), not the real per-frame Game
View path. Instead, mirroring `"GpuSkinning"`'s own real, shipped shape
exactly:

1. **Outside any provider**, in `Application::Run()`'s own offscreen
   `build`/frame-declaration code, immediately BEFORE calling
   `m_offscreenRenderPipeline.DeclareInto(...)` (the same place
   `m_gpuSkinningRequestsThisFrame`/`m_gpuSkinningHandlesThisFrame` are
   already populated):
   - Call PHASE4's `CollectGpuDrivenBatches(registry, renderer)` (or whatever
     its final name is) exactly ONCE this frame. Store its result in a new
     member, e.g. `m_gpuDrivenBatchesThisFrame` (one entry per eligible
     batch: its `(MeshHandle, PipelineHandle)` key, this frame's real
     `instanceCount`, the resolved `PositionNormalInstanced` `Pipeline&`/
     `Mesh&`, and PHASE4's `GpuDrivenBatchCache` raw `Buffer`s for the input/
     indirect-command/count buffers).
   - For each entry, call `builder.ImportBuffer(...)` (against the SAME
     `RenderGraphBuilder&` this frame's whole graph is being built against —
     exactly like GPU Skinning's own import call) for its input/indirect-
     command/count buffers, and store the resulting `rg::BufferHandle`s
     alongside that entry. **Pass names must be stable, permanent,
     static-storage-duration strings** — mirror `ReplayStepPassNamePool()`'s
     own `std::deque<std::string>` precedent (`RenderPasses.cpp`, anonymous
     namespace) for a batch-identity-keyed dynamic name; a raw
     per-frame-formatted `std::string`'s `c_str()` would dangle exactly like
     that comment already warns against.
   - **From this SAME result** (never a second, independently-computed pass),
     also build `m_gpuDrivenBatchedEntitiesThisFrame` (e.g. a
     `std::unordered_set<Entity>`) — the exact set of entities that
     successfully got a batch's worth of buffers imported this frame. **If a
     batch's `Mesh`/`Pipeline` fails to resolve (stale handle, unloaded
     asset), or its `PositionNormalInstanced` pipeline variant fails to build,
     that batch's entities must be excluded from BOTH decisions together — do
     not add entities to the exclusion set optimistically and then discover
     later, at pass-declaration time, that the batch can't actually be
     rendered this frame.** This one-source-of-truth rule is what prevents a
     batch that becomes unrenderable mid-session from silently disappearing
     (drawn zero times) instead of falling back to the safe per-entity path.
2. **Register a new provider**, `"GpuDrivenBatches"`
   (`m_offscreenRenderPipeline.Register("GpuDrivenBatches",
   rg::ProviderScope::PerActiveView, ...)`), inside
   `RegisterOffscreenRenderPipelineProviders()`. **⚠️ CORRECTNESS-CRITICAL
   PLACEMENT REQUIREMENT (see Step 2/3.4 for why): this `Register(...)` call
   must be inserted TEXTUALLY AFTER `"RenderOpaque"`'s own `Register(...)` call
   and TEXTUALLY BEFORE `"DrawSkyBackground"`'s own `Register(...)` call, in
   that exact function's body.** Comment this requirement AT the insertion
   point as loudly as `"AtmosphereComposite"`'s own existing
   `ProviderTiming::AfterDeferredPasses` comment already does for its own,
   analogous ordering hazard ("CORRECTNESS-CRITICAL, confirmed by live
   testing...").
   - The provider body early-returns (declares nothing) for any view other
     than Game (`if (frame.currentView != rg::RenderViewId::Named("Game")) {
     return; }`) — mirrors every other per-view provider's own internal
     `isGameView` branch precedent (`"RenderOpaque"`/`"AtmosphereViewLut"`),
     and is what actually implements the Game-View-only scope decision on the
     PASS-DECLARATION side (Section 3.3 covers the `Draw()`-exclusion side).
   - For each entry in `m_gpuDrivenBatchesThisFrame` (captured via `this`),
     append THREE `rg::RenderPassDesc` entries to `out`, in this exact order
     (their mutual ordering is ALSO independently guaranteed by real RAW/WAW
     edges on their own shared buffer handles, so — unlike the
     `"RenderOpaque"` cross-provider ordering above — this part does not rely
     on the registration-order tie-break at all):
     - **`"<batch> ResetCount"`** (`PassKind::Compute`, same
       `RenderPassEvent::Opaques` tier as the other two below — the tier
       value itself does not matter for THIS pass's own internal ordering,
       only consistency/simplicity): `setup` declares
       `WriteBuffer(countHandle, ResourceAccess::TransferDst)`; `execute`
       calls `vkCmdFillBuffer(ctx.cmd, countBuffer, 0, sizeof(std::uint32_t),
       0)` — **this is PHASE3's own flagged, previously-undeclared-anywhere
       requirement**: the atomic visible-count buffer must contain exactly
       `0` before every dispatch of `Shaders/FrustumCull.comp`, and the shader
       cannot safely do this itself (see
       `PHASE3_FRUSTUM_CULL_COMPUTE_SHADER_AND_PIPELINE.md`, Section 3.2).
       `CreateStructuredBuffer()`'s `GpuOnly` path already ORs in
       `VK_BUFFER_USAGE_TRANSFER_DST_BIT` automatically (confirmed by PHASE3's
       own pre-check), so this needs zero new buffer-creation change.
     - **`"<batch> Culling"`** (`PassKind::Compute`): `setup` declares
       `ReadBuffer(inputHandle, ComputeShaderRead)`,
       `WriteBuffer(indirectHandle, ComputeShaderWrite)`,
       `WriteBuffer(countHandle, ComputeShaderWrite)` — the last of these
       automatically creates a WAW edge from `"<batch> ResetCount"` into this
       pass via the existing, unmodified compiler machinery (both write the
       SAME `countHandle`), so the reset is correctly ordered first with no
       special-casing needed. `execute` calls `renderer.Dispatch()` against
       `CullingPipelines` (PHASE3), with the push-constant `useCompaction`
       flag set from `renderer.SupportsDrawIndirectCount()` (PHASE2) — decided
       ONCE per batch, never re-queried per-frame.
     - **`"<batch> IndirectDraw"`** (`PassKind::Graphics`): `setup` declares
       `WriteColorAttachment(gameViewTarget)`/
       `WriteDepthStencilAttachment(gameViewTarget)` (NO clear — mirrors
       `"DrawSkyBackground"`'s own "must never erase a prior pass's just-
       written pixels" convention), `ReadBuffer(indirectHandle,
       IndirectCommandRead)`, `ReadBuffer(countHandle, IndirectCommandRead)`,
       `ReadBuffer(inputHandle, VertexShaderStorageRead)` (a SECOND,
       independent read declaration against the SAME `inputHandle` the
       culling pass already read via `ComputeShaderRead` — needed because the
       barrier planner tracks access-kind-specific state per resource; this
       second declaration is what correctly orders THIS pass's own
       vertex-shader storage-buffer read after the culling pass's compute
       read/writes, not a duplicate/no-op). `execute` calls
       `renderer.SubmitIndirect()` with the resolved `PositionNormalInstanced`
       `Pipeline&`/`Mesh&` for this batch (see 3.2), the imported
       `indirectHandle`/`countHandle` buffers, and — **⚠️ CORRECTED,
       PHASE3's own flagged invariant, never previously stated in this
       document**: `maxDrawCount` must be set to EXACTLY this dispatch's own,
       current-frame `instanceCount` — **never** PHASE4's buffer-capacity
       (which only ever grows, never shrinks). Passing the buffer's capacity
       here on a frame where the batch has shrunk would silently redraw
       "ghost" instances from a larger previous frame (PHASE3's own worked
       example demonstrates exactly this). This value must be threaded
       through for BOTH the compacted (`vkCmdDrawIndexedIndirectCount`'s
       `maxDrawCount`) and degenerate-padding (`vkCmdDrawIndexedIndirect`'s
       `drawCount`) branches — `SubmitIndirect()` itself picks which Vulkan
       call to issue based on `Renderer::SupportsDrawIndirectCount()`
       (PHASE2), but the COUNT passed in is the same rule either way.

### 3.2 — Resolving a batch's `PositionNormalInstanced` pipeline

A `MeshRenderer`'s own authored `PipelineHandle` still points at an ordinary
`VertexLayout::PositionNormal` pipeline (unmodified — nothing about how
entities are authored/spawned changes in this campaign). The NEW instanced
pipeline variant is a SEPARATE object this phase's own code creates/caches
(lazily, keyed by the original `PipelineHandle` — a batch's "instanced
pipeline" is deterministic given its original pipeline, so a simple
`std::unordered_map<PipelineHandle, Pipeline>` cache, owned alongside
`GpuDrivenBatchCache`, is sufficient) — built once, on first use, via
`Renderer::CreatePipeline(..., VertexLayout::PositionNormalInstanced, ...)`
using the ORIGINAL pipeline's own vertex/fragment shader intent (same
`Mesh.frag`, new `MeshInstanced.vert`). Confirm this resolution happens in a
context with access to whatever the original pipeline's construction
parameters were (shader paths etc.) — if `Pipeline`/`PipelineHandle` doesn't
currently expose enough information to rebuild an equivalent instanced
variant generically, this may require `Game`/`RenderSystem`'s own pipeline-
registration call sites to ALSO register a matching instanced sibling
up-front (at the SAME place `Renderer::CreatePipeline(..., PositionNormal,
...)` is originally called, e.g. `MeshAssetGpuCatalog.cpp`) rather than
lazily inside this phase's pass-declaration code — inspect
`MeshAssetGpuCatalog.cpp`/`Game::EnsureMeshAsset()` directly and use
`ask_questions` if the cleanest ownership point isn't obvious once the real
code is in front of you. Whichever ownership point is chosen, it must be
reached at most ONCE per distinct original `PipelineHandle` for the whole
process lifetime (a lazy cache, never rebuilt every frame) — and its failure
mode (resolution fails) must feed back into 3.1's "one source of truth"
exclusion rule, never leave a batch half-excluded.

### 3.3 — `RenderSystem::Draw()` exclusion — ⚠️ CORRECTED (Game View only)

- Add a way for `Draw()`'s own per-`DrawCommand` loop to skip a command that
  belongs to an eligible batch this frame — a new, trailing, DEFAULTED
  parameter on BOTH `Draw()` overloads (e.g. `const std::unordered_set<Entity>&
  batchedEntities = {}`, empty by default — every pre-existing call site
  compiles and behaves completely unmodified, mirroring `capture`/
  `maxDrawCount`'s own precedent in this exact function's signature). A
  command whose entity is in `batchedEntities` is skipped for drawing (and,
  inside the `#if GTE_ENABLE_EDITOR` block, for `RecordDraw()`/
  `RecordEntityDraw()` — see below) but still counts toward
  `consideredCount` for `maxDrawCount` purposes, matching that parameter's own
  documented "iteration count, not resolved-draw count" contract.
- **This parameter must be passed a real, non-empty value at EXACTLY ONE call
  site: the Game-View branch of the `"RenderOpaque"` provider**
  (`frame.currentView == rg::RenderViewId::Named("Game")`), sourced from
  `m_gpuDrivenBatchedEntitiesThisFrame` (3.1). **Every other call site must
  keep passing the default (empty)** — Scene View's own `"RenderOpaque"`
  branch, `AddFrameDebuggerReplayPasses()`'s per-object replay steps (already
  the case — that function is untouched by this phase), and
  `AddPresentPass()`'s own direct-render-to-swapchain fallback branch (⚠️
  CORRECTED, newly identified during this pre-check — see the preface note's
  point 6 and Step 2). This is what actually implements the confirmed
  Game-View-only scope decision on the `Draw()` side (3.1 implements the
  matching pass-declaration side).
- **Confirm the Frame Debugger's own per-entity replay mechanism
  (`FrameDebuggerCaptureContext::RecordEntityDraw()`, `RenderSystem.cpp` line
  ~164) still behaves sensibly for a batched entity.** Since replay passes
  never receive a non-empty `batchedEntities` (they keep their own default),
  a batched entity's geometry still appears correctly in every reconstructed
  replay-step preview IMAGE (redrawn via the ordinary per-entity path, same
  pixels) — what changes is only that the REAL `"RenderOpaque"` pass's own
  per-entity Frame Debugger attribution list (`capture->RecordEntityDraw(...)`)
  no longer has an entry for that entity THIS frame, since the real pass now
  skips it. Decide (and record in this phase's completion report, flagging it
  clearly rather than silently) whether that is acceptable for this campaign
  (a batched entity becoming a Frame-Debugger-invisible gap under
  `"RenderOpaque"`'s own per-entity children list is an honest, out-loud,
  ACCEPTED regression scoped narrowly to Frame Debugger introspection only,
  never to actual rendering or to the replay preview image itself) or whether
  a follow-up Frame Debugger integration is worth a small amount of extra
  scope here — use `ask_questions` if this decision doesn't feel obviously
  acceptable once you see how visible the gap actually is in practice.

### 3.4 — Ordering/clear-value safety: what actually guarantees correctness here ⚠️ CORRECTED

Restated explicitly because the original draft's own safety-net claim was
wrong (see the preface note's point 3): the guarantee that
`"<batch> IndirectDraw"` never has its own un-cleared pixels erased by
`"RenderOpaque"`'s clear, and never draws AFTER `"DrawSkyBackground"`'s
EQUAL-depth-test sky pass (which would make Sky Background wrongly treat a
batched instance's pixels as untouched sky), rests on TWO things, BOTH of
which must be verified directly, not merely assumed from an automated check:

1. **The registration-order placement from 3.1** ("GpuDrivenBatches"
   registered strictly after `"RenderOpaque"`, strictly before
   `"DrawSkyBackground"`, in `RegisterOffscreenRenderPipelineProviders()`'s
   own body) — this is what makes the stable-sort tie-break (Step 2) resolve
   the three same-tier passes in the correct relative order.
2. **A live visual/Render-Graph-panel check** (see the Compile Check section
   below) — confirm the actual, compiled pass execution order for a frame
   with an eligible batch shows `"RenderOpaque"` → `"<batch's own three
   passes>"` → `"DrawSkyBackground"`, and that a batched instance's geometry
   is neither erased nor incorrectly fogged/sky-overwritten.

`DetectRenderPassEventContradictions()` remains a real, useful safety net for
its OWN documented purpose (a read resolving to the wrong/no writer) — it is
simply not the mechanism that protects this specific WAW ordering hazard, and
this document must not claim otherwise.

### What We Will NOT Do (this phase)

- No change to `RenderGraphCompiler.cpp`/`RenderGraphSnapshot.cpp`/
  `RenderGraphBarrierPlanner.cpp` production logic (PHASE0 Non-Goals) — if
  this phase's own testing finds a real gap, STOP and `ask_questions` first.
- No removal of the per-entity `Renderer::Submit()` path for anything not
  batched, in any view.
- **No GPU-driven batching for Scene View, and none for
  `AddPresentPass()`'s own direct-render-to-swapchain fallback** — both are a
  confirmed, deliberate, permanent scope limitation for this campaign (see
  Step 2/3.3), not a future TODO this phase needs to leave a seam for.
- No Editor tooling yet (PHASE6's job) beyond whatever the Editor's existing,
  already-generic "Render Graph" panel shows for free.
- No modification of `AddRenderOpaquePass()`/`AddDrawSkyBackgroundPass()`/
  `AddGpuSkinningPasses()` (the free functions) themselves — they are not this
  phase's wiring target (see Step 2); leave them exactly as they are.

### Compile check + dedicated double-check

Fast, targeted incremental compile check only. Manually verify: a live scene
with a real eligible batch, viewed through the Game panel, renders visually
IDENTICALLY to before this phase's own change (same positions, same lighting,
same depth-testing against non-batched geometry, sky correctly not overdrawing
batched geometry); the SAME scene, viewed through the Scene panel (with Game
panel ALSO open, the default layout), still renders every batched entity
correctly via the unchanged per-entity path; validation layers report zero new
warnings/errors; rotating the Game View's own camera so part of the batch
leaves the frustum visibly reduces its own indirect draw output (confirm via
the Editor's "Render Graph" panel), while the SAME entities remain visible in
Scene View regardless of the Game camera's orientation (proving the two view's
own draw paths are genuinely independent). Explicitly inspect the compiled
pass execution order (Render Graph panel or an equivalent dump) for one frame
with an eligible batch to confirm `"RenderOpaque"` → the batch's three passes
→ `"DrawSkyBackground"`, per 3.4 — do not rely solely on "no assert fired" as
proof of correct ordering. Write `PHASE5_COMPLETION_REPORT.md` (record the
exact pipeline-resolution ownership decision from 3.2, the Frame Debugger
decision from 3.3, the exact provider-registration diff, and every
`RenderPassEvent`/ordering detail that mattered, including confirmation of the
registration-order placement requirement from 3.1/3.4). Commit.

**Immediately after, delegate a dedicated, standalone `delegate_task` whose
ONLY job is to review this phase's diff for: (a) any place a batched entity
could be drawn TWICE (once by the old loop, once by the new indirect pass) or
ZERO times (skipped by the old loop, but its batch's own passes failed to
declare/run for some reason, INCLUDING via Scene View or the direct-render
fallback — confirm neither of those two ever receives a non-empty
`batchedEntities`) under some edge case (empty batch, a batch that shrinks to
below the eligibility threshold between frames, a batch whose `Mesh`/
`Pipeline` handle becomes invalid mid-session, both Game and Scene panels open
simultaneously); (b) whether the depth-attachment/clear-value/registration-
order interaction between the `"RenderOpaque"` provider and the new
`"GpuDrivenBatches"` provider is genuinely correct for every ordering case,
confirmed by directly re-reading `RegisterOffscreenRenderPipelineProviders()`'s
actual committed source (not just this document's own claim about where the
new `Register(...)` call landed); and (c) whether `RenderGraphCompiler.cpp`'s
existing dependency-edge logic really did need zero changes, as claimed, by
re-reading it directly rather than trusting this document's own claim. That
delegated task must also use `ask_questions` for any genuine ambiguity it
hits, and must repeat this same instruction to anything it further
delegates.**
