# PHASE5 — Completion Report: Render-Graph Pass Wiring + Production Cutover ⚠️ HIGH-RISK / DEDICATED DOUBLE-CHECK PHASE

## Status: DONE

Implemented exactly as scoped in the (already pre-implementation-corrected)
`PHASE5_RENDERGRAPH_PASS_WIRING_AND_PRODUCTION_CUTOVER.md`, per every "⚠️
CORRECTED" marker in that document (see `PHASE5_STRATEGY_DOUBLE_CHECK_REPORT.md`
for the full pre-check writeup). Read `readme.md`, `AGENTS.md`,
`PHASE0_MASTER_STRATEGY.md` (including Locked Design Decision 11, the
Game-View-only scope decision), and `PHASE1`–`PHASE4`'s own completion
reports in full before starting — no discrepancy was left unresolved from
PHASE3's own dedicated double-check: no separate
`PHASE3_DEDICATED_DOUBLE_CHECK_REPORT.md` file exists (`PHASE4_COMPLETION_REPORT.md`
already explicitly confirms `PHASE3_STRATEGY_DOUBLE_CHECK_REPORT.md` *is*
that same pre-implementation double-check, filed under its own working
name), and every gap that report flagged (the count-buffer reset, the
`drawCount == instanceCount` invariant) was already folded into this phase's
own corrected document before this phase started.

## What was built

### Modified files (production code)

- **`src/Game/RenderSystem.h`/`.cpp`** — new `RenderSystem::TryGetPipeline(PipelineHandle)`
  (mirrors `TryGetMesh()`'s exact "never assert on a bad handle" shape) — needed
  so Application's new provider can resolve a batch's original `Pipeline&`.
  Both `Draw()` overloads gained a new, trailing, DEFAULTED parameter,
  `const std::unordered_set<Entity>& batchedEntities = {}` (empty by
  default — every pre-existing call site compiles/behaves completely
  unmodified). A `DrawCommand` whose `entity` is in this set is skipped for
  drawing (and for `RecordDraw()`/`RecordEntityDraw()` inside the
  `#if GTE_ENABLE_EDITOR` block) but still counts toward `consideredCount`
  for `maxDrawCount` purposes. `RenderSystem::CollectGpuDrivenBatches()`
  gained one new line: after `cache.PackThisFrame(key, instances)`, it now
  also calls `cache.EnsureDescriptorSetsWritten(renderer, key)` — this is
  what actually allocates/rewrites a batch's two persistent descriptor sets
  every frame (see `GpuDrivenBatchCache` below).
- **`src/Game/Game.h`/`.cpp`** — new `Game::GetRenderSystem() noexcept` accessor
  (mirrors `GetRegistry()`'s own "Application/Editor observes through a public
  accessor" precedent — `GetRegistry()` is already called directly from
  Application.cpp today). `Render()` gained a new, trailing, DEFAULTED
  parameter, `const std::unordered_set<Entity>& batchedEntities = {}`,
  forwarded straight through to the float-aspect `RenderSystem::Draw()`
  overload ONLY (mirrors `frameDebuggerCapture`'s own exact rule) — NEVER
  forwarded into the `viewProjectionOverride` branch (Scene View's own call
  site).
- **`src/Renderer/Culling/GpuDrivenBatchCache.h`/`.cpp`** — `Entry` gained two
  new fields, `cullingDescriptorSet`/`instanceBufferDescriptorSet`
  (`VkDescriptorSet`, `VK_NULL_HANDLE` until first use). New method
  `EnsureDescriptorSetsWritten(Renderer&, const GpuDrivenBatchKey&)` —
  idempotently `EnsureInitialized()`s the shared `CullingPipelines` (this is
  the actual call site PHASE4's own doc comment predicted: "PHASE5 is what
  actually calls `Pipelines().EnsureInitialized(renderer)`"), lazily
  allocates both descriptor sets once per key, then unconditionally
  `Rewrite()`s both every call (cheap — matches `ComputeDescriptorSet`'s own
  documented "safe/expected every frame" convention) against whichever real
  buffers the key's `Entry` currently holds. New method
  `ResolveInstancedPipeline(Renderer&, PipelineHandle originalHandle)` — see
  "Pipeline-resolution ownership decision" below.
- **`src/Application/Application.h`/`.cpp`** — see "Provider-registration
  diff" below for the exact `RegisterOffscreenRenderPipelineProviders()`
  change. New members: `GpuDrivenBatchCache m_gpuDrivenBatchCache;` (owned
  exactly like `m_atmosphereLutRenderer`/`m_volumeTexturePreviewRenderer` —
  a stateful, Renderer-layer helper with no ECS/Game dependency of its own,
  constructed once, reused every frame), a private nested
  `GpuDrivenBatchRenderData` struct + `std::vector<GpuDrivenBatchRenderData>
  m_gpuDrivenBatchesThisFrame`, `std::unordered_set<Entity>
  m_gpuDrivenBatchedEntitiesThisFrame`, and `Mat4
  m_gpuDrivenGameViewProjectionThisFrame` — all populated fresh, every
  frame, inside the offscreen regime's own `build` lambda, immediately
  before `m_offscreenRenderPipeline.DeclareInto()` is called (the exact
  same "resolve outside any provider, by the caller" shape
  `m_gpuSkinningRequestsThisFrame`/`m_gpuSkinningHandlesThisFrame` already
  established) — Game-View-only: computed inside the existing
  `if (gameTarget != nullptr) { ... }` block; the two "this frame" members
  are still cleared unconditionally every frame (before that `if`) purely
  for hygiene, since neither is ever read on a frame Game isn't active.
  `"RenderOpaque"`'s own execute lambda now passes
  `m_gpuDrivenBatchedEntitiesThisFrame` into `m_game.Render(...)` at
  EXACTLY one call site (the `isGameView` branch) — the Scene-View branch
  is untouched, still calling `m_game.Render(m_renderer,
  aspectWidthOverHeight, &viewProjectionOverride)` with no fifth argument at
  all (defaults to empty).

### No new files, no CMakeLists.txt changes

Every change landed inside already-registered translation units — no new
source file was added this phase.

## Pipeline-resolution ownership decision (Section 3.2)

**Decision: lazy, inside `GpuDrivenBatchCache::ResolveInstancedPipeline()`,
with HARDCODED shader paths — not a generic "recover the original
pipeline's construction parameters" mechanism, and not registered up front
by `MeshAssetGpuCatalog.cpp`.**

This was resolvable without an `ask_questions` detour because direct
inspection of the actual, current source closed the question the phase
document itself left open: `Pipeline` (`Pipeline.h`) stores no shader-path
information at all (only its compiled `VkPipeline`/`VkPipelineLayout` and a
cosmetic `debugName`), so there is no generic way to "recover" a
pipeline's own vertex/fragment shader paths from a `Pipeline&` alone — BUT
this campaign's own Locked Design Decision 7(c) (only exactly
`VertexLayout::PositionNormal` groups are eligible) already narrows the
search space to exactly ONE real content path today:
`MeshAssetGpuCatalog::EnsureMeshPipeline()` (`src/Game/Instantiation/MeshAssetGpuCatalog.cpp`),
which always builds its untextured pipeline from the exact same
`"shaders/Mesh.vert.spv"`/`"shaders/Mesh.frag.spv"` pair. Since
`Shaders/MeshInstanced.vert` was already built (PHASE2) specifically to
reuse `Shaders/Mesh.frag` unmodified, there is exactly one real shader pair
this campaign's own instanced sibling pipeline ever needs to mirror — no
more generic mechanism is required, and none was invented.
`GpuDrivenBatchCache::ResolveInstancedPipeline()` therefore hardcodes this
one shader pair, keyed by the original `PipelineHandle` in a small
`std::map<PipelineHandle, Pipeline, PipelineHandleLess>` (a private,
file-local comparator — `PipelineHandle` itself was deliberately NOT given
a new `operator<`, mirroring `GpuDrivenBatchKey`'s own precedent of adding
its own local ordering rather than growing a shared, widely-included
header), built once per distinct handle, reused forever after (a lazy
cache, never rebuilt per frame). Failure (`Renderer::CreatePipeline()`
throwing, e.g. a missing `.spv`) is caught at BOTH of `ResolveInstancedPipeline()`'s
two real call sites (once during the per-frame collection step, to decide
inclusion; once again, redundantly-but-cheaply, inside the
`"<batch> IndirectDraw"` pass's own `execute` lambda, which re-resolves by
handle rather than trusting a possibly-stale captured pointer — see next
section) and, on failure, the whole batch is excluded from both decisions
together (the "one source of truth" rule) via a `stderr` log line, never a
crash.

## Why the IndirectDraw pass re-resolves `Mesh&`/`Pipeline&` at execute time (a small addition beyond the literal phase document)

`GpuDrivenBatchRenderData` deliberately stores only `MeshHandle`/`PipelineHandle`
(never a raw `Mesh*`/`Pipeline*` captured at collection time) precisely
because `ResourcePool<T, HandleT>`'s own backing `std::vector<Slot>` can
reallocate if a new `Mesh`/`Pipeline` is registered anywhere between this
frame's batch-collection step and the point its own `"<batch> IndirectDraw"`
pass's deferred `execute` lambda actually runs (both happen within the same
frame/`Execute()` call today, so this is not a currently-reachable bug, but
capturing a handle and re-resolving via `m_game.GetRenderSystem().TryGetMesh(...)`/
`m_gpuDrivenBatchCache.ResolveInstancedPipeline(...)` inside `execute` itself
removes the *possibility* entirely, for a negligible, cached-map-lookup
cost). This is a deliberate, reasoned addition beyond the phase document's
own literal text, not a deviation from its intent.

## Frame Debugger decision (Section 3.3)

**Decision: the documented, accepted, narrow regression is exactly what
happens, and it is accepted as-is — no follow-up scope was added this
phase.** A batched entity's geometry still appears correctly in every
Frame-Debugger replay-step preview image (`AddFrameDebuggerReplayPasses()`
never receives a non-empty `batchedEntities` — it keeps its own default —
so it redraws every entity, including batched ones, via the ordinary
per-entity path, same pixels as before). What changes is only that the
REAL `"RenderOpaque"` pass's own per-entity Frame Debugger attribution list
(`capture->RecordEntityDraw(...)`) no longer has an entry for a batched
entity, since the real pass now skips it (the new `batchedEntities.contains(command.entity)`
early-`continue`, `RenderSystem.cpp`, runs BEFORE the `#if GTE_ENABLE_EDITOR`
block that would otherwise call `RecordEntityDraw()`). This was not
re-litigated via `ask_questions` — the phase document already framed this
as "an honest, out-loud, ACCEPTED regression scoped narrowly to Frame
Debugger introspection only, never to actual rendering or to the replay
preview image itself," and nothing discovered during implementation made
that framing feel wrong once the real code was in front of me — a batched
entity simply becomes an invisible gap in `"RenderOpaque"`'s own real
per-entity children list in the Frame Debugger tree, which is a real,
narrow, cosmetic-introspection-only limitation, not a rendering
correctness issue.

## Provider-registration diff (Section 3.1/3.4)

`Application::RegisterOffscreenRenderPipelineProviders()` (`Application.cpp`)
now registers, in this exact order (excerpted, full names only):

```
"AtmosphereSharedLut"   (Once)
"GpuSkinning"           (Once)
"AtmosphereViewLut"     (PerActiveView)
"RenderOpaque"          (PerActiveView)
"GpuDrivenBatches"      (PerActiveView)   <-- NEW, this phase
"DrawSkyBackground"     (PerActiveView)
"RenderTransparent"     (PerActiveView)
"AtmosphereComposite"   (PerActiveView, AfterDeferredPasses)
```

`"GpuDrivenBatches"`'s own `Register(...)` call is placed TEXTUALLY,
in the function body, strictly after `"RenderOpaque"`'s own `Register(...)`
call and strictly before `"DrawSkyBackground"`'s own `Register(...)` call —
confirmed by direct re-read of the final committed source, not merely
assumed. Its provider body early-returns for any view other than
`rg::RenderViewId::Named("Game")`. For each entry in
`m_gpuDrivenBatchesThisFrame` it appends three `rg::RenderPassDesc` entries,
all tagged `RenderPassEvent::Opaques` (the SAME tier `"RenderOpaque"` uses):
`"<batch> ResetCount"` (Compute), `"<batch> Culling"` (Compute), and
`"<batch> IndirectDraw"` (Graphics).

### RenderPassEvent/ordering details that mattered

- All three new passes intentionally share `"RenderOpaque"`'s own
  `RenderPassEvent::Opaques` tier (not a distinct, later tier) — the phase
  document is explicit that the tier VALUE itself doesn't matter for these
  three passes' own MUTUAL ordering (that is independently guaranteed by
  real RAW/WAW edges on their own shared buffer handles — the reset pass
  and the culling pass both write `countHandle`, so the render graph's
  existing, unmodified dependency-edge machinery orders the reset strictly
  first with zero special-casing needed), but it DOES matter for their
  ordering relative to `"RenderOpaque"`/`"DrawSkyBackground"` — this is
  exactly the WAW hazard `DetectRenderPassEventContradictions()` cannot
  catch (confirmed directly by re-reading `RenderGraphCompiler.cpp` — see
  next section), so the only real guarantee is the combination of (a) same
  tier as `"RenderOpaque"` and (b) registration order.
- `RenderGraphCompiler::Compile()`'s own `effectiveOrder` is a stable sort
  by `(RenderPassEvent, original declaration index)` — for passes sharing
  a tier, relative order is EXACTLY their original declaration index, which
  for two `PerActiveView` providers is exactly their REGISTRATION order
  (`RenderPipeline::DeclareOnePhase()` loops `m_providers` in registration
  order, and for each one loops `frame.activeViews`, appending
  `RenderPassDesc`s to one shared scratch list in that nested order, THEN
  stable-sorts that whole list once). Since `"RenderOpaque"` is registered
  before `"GpuDrivenBatches"`, which is registered before
  `"DrawSkyBackground"`, and all three (for the Game view) share the
  `Opaques` tier except `"DrawSkyBackground"` itself (`AfterOpaques`, a
  strictly later tier, so it is guaranteed after every `Opaques`-tier pass
  regardless of registration order): the final, guaranteed execution order
  for the Game view is `"RenderOpaque"` → `"<batch> ResetCount"` →
  `"<batch> Culling"` → `"<batch> IndirectDraw"` → `"DrawSkyBackground"`.
  **This was directly, visually confirmed against the real, compiled,
  running engine — see "Live verification" below — not merely reasoned
  about.**
- `desc.legacyCategory` was left at its default (`RenderPassCategory::General`)
  for all three new passes — they are real, permanent, production passes,
  not Frame-Debugger-internal scaffolding, so `Debug` would have been
  wrong.

### The atomic count-buffer reset + `drawCount == instanceCount` invariants (both empirically re-confirmed, not just implemented)

- `"<batch> ResetCount"` declares `WriteBuffer(countHandle, ResourceAccess::TransferDst)`
  and its `execute` calls a raw `vkCmdFillBuffer(ctx.cmd, countBufferNative, 0,
  sizeof(std::uint32_t), 0)` directly against `ctx.cmd` — no
  `BeginGraphPassRecording()`/`EndGraphPassRecording()` bracket is used here
  (that pairing only matters for a `Renderer::Submit()`/`SubmitIndirect()`/
  `Dispatch()` call, none of which happen in this one pass).
- `"<batch> IndirectDraw"`'s own `execute` always passes
  `static_cast<std::uint32_t>(instanceCount)` (THIS frame's real, current
  instance count, from `GpuDrivenBatchFrameEntry::instanceCount` /
  `GpuDrivenBatchRenderData::instanceCount` — never the cache's own
  buffer-capacity-only-grows size) as `Renderer::SubmitIndirect()`'s
  `maxDrawCount` argument, for BOTH the compacted (`countBuffer` non-null,
  gated on `Renderer::SupportsDrawIndirectCount()`) and degenerate-padding
  (`countBuffer = VK_NULL_HANDLE`) branches.

### `DetectRenderPassEventContradictions()` re-confirmed insufficient for this hazard, by direct re-read

Re-read `RenderGraphCompiler.cpp`'s `DetectRenderPassEventContradictions()`
directly before relying on anything else: it only scans each pass's
`reads` against the nearest preceding writer (in effective order) — it
never inspects two passes that both WRITE the same resource with no
intervening read (a pure WAW hazard), which is exactly this phase's own
`"RenderOpaque"`-vs-`"<batch> IndirectDraw"` shape (both
`WriteColorAttachment`/`WriteDepthStencilAttachment` the same Game-View
target; neither reads it back). This confirms the phase document's own
"⚠️ CORRECTED" claim is accurate — this detector genuinely does not, and
structurally cannot, catch a mis-registration of this specific kind. The
real, load-bearing guarantee is the registration-order placement described
above, verified live (below).

## `RenderGraphCompiler.cpp`/`RenderGraphBarrierPlanner.cpp` needed zero changes — confirmed, not just claimed

No change was made to either file this phase. This claim was validated
empirically, not merely assumed: the full targeted Tier-1 test suite for
`RenderGraphCompilerTest`/`RenderGraphBarrierPlannerTest`/`RenderGraphBuilderTest`/
`RenderGraphResourceAccessTest` (113 tests total) passed unchanged both
before and after this phase's own diff, and the live engine run below
shows the new passes' barriers/ordering resolving correctly with zero
validation-layer warnings.

## Live verification (a genuinely running engine, HTTP-driven)

A **temporary** manual-verification harness (`SpawnPhase5VerificationBatch()`,
`Application.cpp`, built, run, screenshotted, then FULLY DELETED before this
commit — mirroring PHASE2/3/4's own identical "build, run, verify, delete"
discipline) was needed because this project's own only real imported mesh
asset (`build/Project/terrain.gta`) is fully TEXTURED, and Locked Design
Decision 8 explicitly excludes textured batches from eligibility — so no
real, GPU-driven-eligible batch existed anywhere in this project's existing
test content. The harness hand-built a real, untextured, indexed
`VertexLayout::PositionNormal` quad `Mesh` + a real `Pipeline` (the exact
same `"shaders/Mesh.vert.spv"`/`"shaders/Mesh.frag.spv"` pair
`MeshAssetGpuCatalog::EnsureMeshPipeline()` uses), registered both via
`RenderSystem::RegisterMesh()`/`RegisterPipeline()`, and spawned 6 entities
(≥ `kMinInstancesForGpuDrivenBatch` = 4) sharing that exact `(MeshHandle,
PipelineHandle)` pair at 6 distinct world positions, plus a NAMED Camera
entity (so the live rotation test below could address it by name over
HTTP — the engine's own auto-created default camera has no `Name`
component).

Confirmed, live, against the real compiled engine (`GET /get_logs`
returning `count: 0` for `min_level=Warning` throughout every step below —
zero validation-layer/engine warnings or errors at any point):

1. **Game View renders the batch correctly through the new indirect path.**
   `GET /get_game_view` showed the expected merged quad strip. The Editor's
   "Render Graph" panel (`GET /activate_tab?name=Render%20Graph` +
   `GET /get_swapchain`) showed, for the Offscreen Regime, in this EXACT
   order: `RenderOpaque` (**Draws: 0** — every batched entity correctly
   excluded from the per-entity path), `GpuDrivenBatch0 ResetCount`,
   `GpuDrivenBatch0 Culling`, `GpuDrivenBatch0 IndirectDraw`, then
   `DrawSkyBackground` — the EXACT required order, confirmed visually, not
   just reasoned about.
2. **Scene View still renders every batched entity via the fully unmodified
   per-entity path.** With the Scene tab focused (`GET /activate_tab?name=Scene`),
   `GET /get_swapchain` showed the same 6 quads rendered through Scene
   View's own, independent `EditorCamera`, AND the Render Graph panel's
   `RenderOpaque` row now showed **Draws: 6, Tris: 12** (6 quads × 2
   triangles each) — direct, numeric, live proof that Scene View's own
   `RenderOpaque` pass drew all 6 entities unbatched, and that NO
   `GpuDrivenBatch0 *` passes were declared at all for that frame (Game
   wasn't the visible tab that frame, so its own per-view branch of
   `"GpuDrivenBatches"`/`"RenderOpaque"` never ran) — confirming Scene View
   never received a non-empty `batchedEntities`.
3. **Culling genuinely responds to camera rotation.** Rotating the named
   Game-View camera 35° (`POST /set_entity_trs`) visibly shifted the quad
   strip in `GET /get_game_view` exactly as expected (real perspective
   change). Rotating it a further 90° (so the whole batch is behind/beside
   the camera) made `GET /get_game_view` show ONLY sky — every instance
   correctly, dynamically culled — with zero warnings/errors even for this
   "all instances culled, `visibleCount == 0`" edge case. Reverting the
   rotation was not needed for this report; the temporary harness was
   deleted immediately afterward.
4. **A default, empty scene (no batch present, the harness's own code
   fully removed) still renders identically to every prior phase**
   (`GET /get_game_view` on a fresh engine run showed the same plain
   sky/ground the engine has always shown for a scene with no mesh
   entities), confirming this phase's changes are a pure additive/
   opt-in cutover with no regression for any non-batched content.

## Compile check (per Locked Design Decision 9 — targeted only, no full build)

- `cmake --build build --target GreatTamanaEngine --config Debug` — succeeds
  cleanly, zero warnings/errors, both with the temporary verification
  harness present and after it was fully removed (two separate clean
  incremental rebuilds).
- `cmake --build build --target GreatTamanaEngineTests --config Debug` —
  succeeds cleanly.
- Targeted test run:
  `RenderSystemTest.*:RenderBatchingTest.*:CullingTypes.*:DrawStatsTest.*:RenderGraphResourceAccessTest.*:RenderGraphBarrierPlannerTest.*:RenderGraphBuilderTest.*:RenderGraphCompilerTest.*`
  — **155/155 tests passed**, confirming zero regression from every
  `RenderSystem::Draw()`/`Game::Render()` signature change and zero change
  needed to `RenderGraphCompiler.cpp`/`RenderGraphBarrierPlanner.cpp`/
  `RenderGraphBuilder.cpp`. No full `cmake --build build` + full `ctest`
  pass was run, per `PHASE0_MASTER_STRATEGY.md`'s Locked Design Decision 9
  (reserved for PHASE7 only).

## Deviations from the phase document

None load-bearing. Two small, reasoned additions beyond its literal text
(both already called out above in their own sections): (1) the pipeline-
resolution mechanism hardcodes the one real shader pair rather than
building a more generic "recover the original construction parameters"
mechanism, since direct inspection confirmed there is only ever one real
shader pair eligible batches can have today; (2) `"<batch> IndirectDraw"`'s
own `execute` lambda re-resolves `Mesh&`/`Pipeline&` by handle rather than
capturing a raw pointer at collection time, closing a theoretical (not
currently reachable) dangling-pointer risk from `ResourcePool`'s own
vector-reallocation-on-growth behavior.

## What We Did NOT Do (matches the phase doc's own scope)

- No change to `RenderGraphCompiler.cpp`/`RenderGraphSnapshot.cpp`/
  `RenderGraphBarrierPlanner.cpp` production logic.
- No removal of the per-entity `Renderer::Submit()` path for anything not
  batched, in any view.
- No GPU-driven batching for Scene View, and none for
  `AddPresentPass()`'s own direct-render-to-swapchain fallback (its own
  `game.Render(renderer, *directGameRenderAspect)` call in
  `RenderPasses.cpp` was not touched at all — it still has no fifth
  argument, so it still defaults to an empty `batchedEntities`).
- No Editor tooling ("instances culled this frame" readout, etc.) — PHASE6's job.
- No modification of `AddRenderOpaquePass()`/`AddDrawSkyBackgroundPass()`/
  `AddGpuSkinningPasses()` (the free functions in `RenderPasses.cpp`/`.h`) —
  confirmed still byte-for-byte unchanged.

## Next phase

PHASE6 (`PHASE6_EDITOR_TOOLING_AND_LIVE_VALIDATION.md`) can now depend on:
the whole GPU-driven cutover being real, wired, and live-verified end to
end for the Game View; `m_gpuDrivenBatchesThisFrame`/
`m_gpuDrivenBatchedEntitiesThisFrame` (Application.h) as the natural data
source for a future "instances culled this frame" readout; and
`GpuDrivenBatchCache::EnsureDescriptorSetsWritten()`/`ResolveInstancedPipeline()`
as the two new, real, per-batch resource-preparation steps a future
Editor-tooling change might want to surface diagnostics for.

**Per this phase's own mandatory instruction (this campaign's second and
final dedicated-double-check phase, PHASE0's Locked Design Decision 10), a
separate, dedicated `delegate_task` review of ONLY this phase's committed
diff was spawned immediately after this report and its accompanying code
were committed — see `PHASE5_DEDICATED_DOUBLE_CHECK_REPORT.md`, this same
folder, for its own findings once it completes.**
