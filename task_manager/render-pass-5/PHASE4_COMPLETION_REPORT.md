# PHASE4 — Completion Report: RenderSystem Batching + Per-Batch GPU Resource Management

## Status: DONE

Implemented exactly as scoped in
`PHASE4_PER_BATCH_RESOURCE_MANAGEMENT_AND_BATCHING.md`, with two genuine
ambiguities resolved via `ask_questions` before writing any code (see
"`ask_questions` detours" below) and two small, additive infrastructure gaps
(neither flagged explicitly by the phase doc's own literal text, but both
structurally necessary to implement it) closed along the way, documented
below. Read `readme.md`, `AGENTS.md`, `PHASE0_MASTER_STRATEGY.md`,
`PHASE1_COMPLETION_REPORT.md`, `PHASE2_COMPLETION_REPORT.md`,
`PHASE3_COMPLETION_REPORT.md`, and `PHASE3_STRATEGY_DOUBLE_CHECK_REPORT.md` in
full before starting, per this campaign's own working agreement — no
`PHASE3_DEDICATED_DOUBLE_CHECK_REPORT.md` file exists separately;
`PHASE3_STRATEGY_DOUBLE_CHECK_REPORT.md` is that same pre-implementation
double-check, filed under its own working name. This phase is NOT one of the
two dedicated-double-check phases (PHASE3/PHASE5 only), so no extra
delegation was spawned here, per PHASE0's own instructions.

## `ask_questions` detours (both flagged in advance by the phase doc itself)

### 1. Real mesh-bounds computation wiring (Locked Design Decision 3)

The phase doc only said to "add a small `Mesh::LocalBounds()`-style accessor
to `Mesh.h` now if PHASE1 didn't already" — confirmed PHASE1/PHASE2
deliberately left `Mesh.h` untouched. But no phase document anywhere in this
campaign (PHASE0–PHASE5) actually assigns anyone the job of calling
`ComputeLocalAABB()` on real, live imported-mesh CPU position data and
storing the result on the real `Mesh` object at load time — without that
wiring, `Mesh::LocalBounds()` would return an empty/default value for every
real mesh in the engine forever, making the whole per-batch packing pipeline
produce meaningless, always-degenerate AABBs for every real batch.

**Asked directly; the project owner's answer: wire real bounds computation
into `MeshAssetGpuCatalog.cpp`'s existing untextured/indexed (and, for
consistency/future use, textured) submesh creation call sites now, in this
phase**, so real batches get real, meaningful bounds end-to-end rather than
leaving this as a named follow-up gap.

### 2. The GPU-skinning exclusion cross-reference mechanism (Locked Design Decision 7d)

The phase doc explicitly flagged this as a point requiring direct
verification and, if the real mechanism doesn't expose a `MeshHandle`, an
`ask_questions` detour before inventing a new, parallel bookkeeping
mechanism. Direct inspection of `AnimationSystem.h`/`Game.h` confirmed:
`AnimationSystem::GpuSkinningDispatchRequest`
(`Game::CollectGpuSkinningDispatchRequests()`'s own real, only, shipped
result type) carries **no `MeshHandle` at all** — only a raw `VkBuffer
outputBuffer`, a `VkDescriptorSet`, `vertexCount`, and a `textured` bool.

The real, reachable cross-reference is instead a **`VkBuffer` identity
match**: `Mesh::VertexBuffer()` already returns the exact same `VkBuffer`
type, and a GPU-skinned model's own `Mesh` is literally built directly from
that same output buffer (see `GpuSkinningRigCache.h`'s `OutputGroup`). But
computing this match needs BOTH the `GpuSkinningDispatchRequest` list (a
Game/`AnimationSystem`-owned concept) and resolved `Mesh&` objects (a
`RenderSystem`-owned concept) in the same place — `RenderSystem` itself
cannot compute it alone without a new, layering-violating dependency on
`AnimationSystem`'s own type (AGENTS.md's Clean Architecture rule: only
`RenderSystem`/`MeshInstantiationSystem`/`AnimationSystem` may depend on both
ECS and Renderer; `RenderSystem` must never depend on
`Game`/`AnimationSystem`).

**Asked directly; the project owner's answer: `RenderSystem`'s new
`CollectGpuDrivenBatches()` method takes the GPU-skinned-`VkBuffer` exclusion
set as a caller-supplied `const std::unordered_set<VkBuffer>&` input
parameter (defaulted to empty) — the actual cross-referencing
(`GpuSkinningDispatchRequest` → `VkBuffer` set) happens OUTSIDE
`RenderSystem`, in Game/Application-level code, which is PHASE5's job once a
real caller exists (the everyday Game View production frame-build code).**
PHASE4 itself only defines the parameter and does the
`VkBuffer`-vs-`Mesh::VertexBuffer()` comparison inside `RenderSystem` using
whatever set it's handed — implemented exactly as decided (see
`RenderSystem::CollectGpuDrivenBatches()`, `RenderSystem.cpp`).

## What was built

### New files

- **`src/Game/DrawCommand.h`** — `DrawCommand` extracted verbatim (zero
  field/behavior change) out of `RenderSystem.h`, into its own standalone
  header. Needed to avoid a circular `#include` between `RenderSystem.h` (now
  also depends on `RenderBatching.h`/`GpuDrivenBatchCache.h`) and
  `RenderBatching.h` (which needs `DrawCommand`'s full definition for its own
  `RenderBatchGroup::commands` member) — both now include this one shared,
  leaf header instead of each other.
- **`src/Game/RenderBatching.h`/`.cpp`** — exactly as scoped in section 3.1:
  `RenderBatchGroup`, `GroupDrawCommandsByMeshAndPipeline()` (a stable,
  first-seen-order-preserving grouping via an internal hash-keyed lookup, not
  an O(n²) scan), `kMinInstancesForGpuDrivenBatch` (= 4), and
  `IsGpuDrivenEligible()` (Locked Design Decision 7's exact 4-condition rule).
  Pure, zero Renderer/live-GPU dependency.
- **`src/Renderer/Culling/GpuDrivenBatchCache.h`/`.cpp`** — exactly as scoped
  in section 3.2, mirroring `GpuSkinningRigCache`'s per-model resource-cache
  precedent: `GpuDrivenBatchKey` (a small, `operator<`-comparable
  `(MeshHandle, PipelineHandle)` key — a `std::map`, not
  `std::unordered_map`, avoiding a hand-written hash functor at this cache's
  expected scale), `GpuDrivenBatchCache::Entry` (`inputBuffer`
  `CpuToGpu`/`indirectCommandBuffer` `GpuOnly`/`countBuffer` `GpuOnly`, both
  with `extraUsage = VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT` — the CORRECT
  Vulkan enumerator name, per PHASE3_COMPLETION_REPORT.md's own flagged
  documentation-only typo correction, not the phase docs' own
  `VK_BUFFER_USAGE_INDIRECT_COMMAND_BIT`, which does not exist), plus
  `EnsureCapacity()`/`PackThisFrame()`/`TryGet()`. **Deliberately kept
  Renderer-layer-clean**: unlike the phase doc's own literal pseudocode (which
  showed `PackThisFrame(key, const RenderBatchGroup&, Registry&)`), this class
  never touches `RenderBatchGroup`/`DrawCommand`/`Registry`/`Entity` (all
  Game/ECS-layer concepts) at all — it only ever takes a plain
  `std::vector<GpuCullingInstanceInput>` the CALLER (`RenderSystem`, which is
  explicitly allowed to depend on both ECS and Renderer) already packed. This
  is a deliberate, reasoned deviation from the doc's literal signature, not an
  oversight — mirrors `GpuSkinningRigCache` living under `src/Game/Animation/`
  (not `src/Renderer/GpuSkinning/`) for the identical Clean-Architecture
  reason, except here the SPLIT happens inside the class boundary instead of
  moving the whole class to a different folder, since `GpuDrivenBatchCache`
  itself has no genuine Game-layer dependency once packing is factored out
  this way. Also owns the one shared `CullingPipelines` instance (`Pipelines()`
  accessor), per `CullingPipelines.h`'s own documented expectation — never
  `EnsureInitialized()`'d by this phase (no dispatch of any kind is issued
  here; PHASE5's job).
- **`tests/Game/RenderBatchingTests.cpp`** — 14 new Tier-1 tests:
  `GroupDrawCommandsByMeshAndPipeline()` (empty input; all-same-group input;
  several interleaved groups with first-seen order preservation, both across
  groups and within one group; a same-index-different-generation handle
  treated as a genuinely different group — a real `ResourcePool`-style
  regression case) and `IsGpuDrivenEligible()` (every true/false combination
  of its 4 boolean-ish inputs, including the exact boundary at
  `kMinInstancesForGpuDrivenBatch`, PLUS an explicit case proving a group
  whose `Pipeline` was ALREADY `PositionNormalInstanced` is never re-reported
  eligible).

### Modified files

- **`src/Renderer/Mesh.h`** — new `std::optional<AABB> m_localBounds` field +
  `SetLocalBounds()`/`LocalBounds()` accessors (Locked Design Decision 3 —
  lives on `Mesh`, never on `MeshRenderer`). Includes the new
  `Culling/CullingTypes.h` for `AABB` (no circular-include risk — that header
  has zero dependency on `Mesh.h`).
- **`src/Renderer/Pipeline.h`/`.cpp`** — new `m_vertexLayout` field +
  `VertexLayoutKind() const noexcept` accessor (both constructors now
  initialize it; move ctor/assignment now carry it too). **Not explicitly
  named by the phase doc's own text**, but structurally required: `Pipeline`
  previously exposed no way at all to query which `VertexLayout` it was built
  with, and Locked Design Decision 7(c) ("a group's shared Pipeline was built
  with EXACTLY `VertexLayout::PositionNormal`") cannot be checked without it.
  Mirrors `DebugName()`'s own existing "small, additive, purely descriptive
  accessor" precedent — no behavior change to anything that already used
  `Pipeline`.
- **`src/Game/Instantiation/MeshAssetGpuCatalog.cpp`** — new
  `ComputeSubsetLocalAABB()` anonymous-namespace helper (computes a TIGHT
  bound from only the subset of `mesh->positions` a given submesh's own
  `indices` actually reference — not the whole model's shared position
  array), called immediately after building each real `gpuMesh` (both the
  untextured merged submesh AND every textured submesh, via
  `gpuMesh.SetLocalBounds(...)`) — the `ask_questions`-confirmed real-content
  wiring (see detour #1 above).
- **`src/Game/RenderSystem.h`/`.cpp`** — `DrawCommand` extraction (see above,
  zero behavior change); new `GpuDrivenBatchFrameEntry` struct; new
  `RenderSystem::CollectGpuDrivenBatches(Registry&, Renderer&,
  GpuDrivenBatchCache&, const std::unordered_set<VkBuffer>&
  gpuSkinnedOutputBuffersThisFrame = {}, std::size_t
  minInstancesForGpuDrivenBatch = kMinInstancesForGpuDrivenBatch)` method —
  groups this frame's `DrawCommand`s, resolves each group's real
  `Mesh&`/`Pipeline&`, computes `isGpuSkinned` via the `ask_questions`-decided
  `VkBuffer` cross-reference (detour #2 above), applies
  `IsGpuDrivenEligible()`, and for every eligible group calls
  `cache.EnsureCapacity()` then packs this frame's live
  `Transform`/`Mesh`-bounds data (`TransformAABB()` + `PackCullingInstanceInput()`,
  both PHASE1's own already-tested functions) into `cache.PackThisFrame()`.
  **`Draw()` itself (both overloads) is completely untouched** — confirmed by
  a direct diff review; this method is purely additive, called by nobody yet
  (PHASE5's job).
- **`CMakeLists.txt`** (root) — registered
  `src/Game/DrawCommand.h`/`RenderBatching.h`/`.cpp` (immediately before
  `RenderSystem.cpp`/`.h`) and
  `src/Renderer/Culling/GpuDrivenBatchCache.h`/`.cpp` (immediately after
  `CullingPipelines.h`/`.cpp`).
- **`tests/CMakeLists.txt`** — added `Game/RenderBatchingTests.cpp` to
  `GTE_TEST_SOURCES` (immediately after `Game/RenderSystemTests.cpp`), plus a
  matching descriptive taxonomy entry in this file's own header comment.

## Manual sanity check (per this phase's own "Compile Check" section)

A throwaway verification harness (`src/Application/GpuDrivenBatchVerificationHarness.h`/`.cpp`,
called once from `Application`'s constructor, immediately after the existing
`GTE_LOG_INFO("Application", "GreatTamanaEngine started.")` call — mirroring
PHASE2/PHASE3's own identical "build, run against a real live Renderer,
manually verify the logged results, then fully delete" discipline) was built,
run once, and then fully deleted (confirmed via a second clean rebuild plus
`git status` showing zero leftover trace of it anywhere):

- Built a real, hand-authored, indexed `VertexLayout::PositionNormal` quad
  `Mesh` (via `Renderer::CreateMesh()`'s indexed overload) with its local
  bounds set via `ComputeLocalAABB()`, and a matching real `Pipeline`
  (`shaders/Mesh.vert.spv`/`Mesh.frag.spv`).
- Spawned a plain `Registry` with 5 entities, all sharing that exact
  `(MeshHandle, PipelineHandle)` pair, at 5 distinct world positions
  (`x = 0, 2, 4, 6, 8`, `z = 10`).
- Called the real `RenderSystem::CollectGpuDrivenBatches()` against a real
  `GpuDrivenBatchCache`, then read back the cache entry's own `inputBuffer`
  **directly via its already-mapped `MappedData()`** (it's
  `BufferMemoryUsage::CpuToGpu` — no GPU→CPU staging-buffer readback needed
  at all, unlike PHASE3's own `GpuOnly`-buffer harness).

**Actual logged results** (via `GET /get_logs?category=GpuDrivenBatchHarness`,
against a real running engine session, zero `Warning`/`Error` entries the
entire session — confirmed via `GET /get_logs?min_level=Warning` returning
`count: 0`):

```
batches.size()=1
batch[0].instanceCount=5 commands.size()=5
cache entry capacity=5
instance[0] translation=(0.000000, 0.000000, 10.000000) worldAabbMin=(-0.500000, -0.500000, 10.000000) worldAabbMax=(0.500000, 0.500000, 10.000000) indexCount=6
instance[1] translation=(2.000000, 0.000000, 10.000000) worldAabbMin=(1.500000, -0.500000, 10.000000) worldAabbMax=(2.500000, 0.500000, 10.000000) indexCount=6
instance[2] translation=(4.000000, 0.000000, 10.000000) worldAabbMin=(3.500000, -0.500000, 10.000000) worldAabbMax=(4.500000, 0.500000, 10.000000) indexCount=6
instance[3] translation=(6.000000, 0.000000, 10.000000) worldAabbMin=(5.500000, -0.500000, 10.000000) worldAabbMax=(6.500000, 0.500000, 10.000000) indexCount=6
instance[4] translation=(8.000000, 0.000000, 10.000000) worldAabbMin=(7.500000, -0.500000, 10.000000) worldAabbMax=(8.500000, 0.500000, 10.000000) indexCount=6
=== PHASE4 harness: ALL CHECKS PASSED ===
```

This confirms, against a real live `Renderer`/GPU device (not just a unit
test): a real 5-instance batch was correctly detected as eligible (5 >= the
threshold of 4); `EnsureCapacity()` allocated real buffers sized for exactly
5 instances; `PackThisFrame()` wrote real, DISTINCT, per-instance world
matrices (each instance's own translation matches its own `Transform`, not a
stale/shared/all-zero value) and real, correctly-`TransformAABB()`'d
world-space bounds (each instance's own local `[-0.5,-0.5,0]..[0.5,0.5,0]`
bound shifted by exactly its own world position) — i.e. the whole
allocate/repack path genuinely works end-to-end, not merely compiles.

## Deviations from the phase document

1. `GpuDrivenBatchCache::PackThisFrame()`'s real signature takes a plain
   `std::vector<GpuCullingInstanceInput>` rather than the doc's own literal
   `(key, const RenderBatchGroup&, Registry&)` pseudocode — a deliberate
   Clean-Architecture fix (see "What was built" above), not an oversight.
2. `Pipeline` gained `VertexLayoutKind()` (not explicitly named anywhere in
   PHASE0–PHASE4's own text) — a small, structurally necessary, additive
   accessor with zero behavior change to any existing caller.
3. Both `ask_questions` detours above (bounds-computation wiring;
   GPU-skinning cross-reference mechanism) resolved exactly as the project
   owner decided — see those two sections for the full record.

Everything else (file locations, struct/function shapes, the
`kMinInstancesForGpuDrivenBatch = 4` constant, the count-buffer's dual
`VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT | VK_BUFFER_USAGE_STORAGE_BUFFER_BIT`
usage-flag resolution already pinned down by PHASE3's own corrected
document) matches the phase document exactly.

## What We Did NOT Do (matches the phase doc's own scope)

- No render-graph pass declaration, no `AddComputePass()`/`ReadBuffer()`/
  `WriteBuffer()`/`ImportBuffer()` call of any kind (PHASE5's job).
- No `vkCmdDispatch`/indirect draw of any kind — `CullingPipelines` is owned
  by the new cache but never `EnsureInitialized()`'d here.
- **`RenderSystem::Draw()`'s own existing per-entity loop is completely
  unmodified** — every `DrawCommand`, including ones that will become
  batch-eligible once PHASE5 lands, is still drawn exactly as before this
  phase. Confirmed by direct diff review of `RenderSystem.cpp`.
- No count-buffer reset (`vkCmdFillBuffer`) — PHASE5's job, per PHASE3's own
  corrected document.
- No wiring of `CollectGpuDrivenBatches()` into any real production call
  site (`Application::Run()`) — PHASE5's job.

## Compile check (per Locked Design Decision 9 — targeted only, no full build)

- `cmake -S . -B build` (reconfigure, since new source files were added to
  the explicit `target_sources()` lists) + `cmake --build build --target
  GreatTamanaEngine --config Debug` — succeeds cleanly, zero warnings/errors
  (both with the temporary harness present, used for the manual verification
  above, and after it was fully removed — confirmed via a second clean
  rebuild).
- `cmake --build build --target GreatTamanaEngineTests --config Debug` —
  succeeds cleanly.
- Targeted test run:
  `RenderBatchingTest.*:RenderSystemTest.*` — **26/26 tests passed** (14 new
  `RenderBatchingTest` cases + all 12 pre-existing `RenderSystemTest` cases,
  confirming zero regression from the `DrawCommand` extraction).
  `CullingTypes.*:MeshVertexPackingTest.*:ResourcePoolTest.*:DrawStatsTest.*`
  — **38/38 tests passed** (confirming zero regression from the `Mesh.h`/
  `Pipeline.h` changes). No full `cmake --build build` + full `ctest` pass was
  run, per `PHASE0_MASTER_STRATEGY.md`'s Locked Design Decision 9 (reserved
  for PHASE7 only).

## Next phase

PHASE5 (`PHASE5_RENDERGRAPH_PASS_WIRING_AND_PRODUCTION_CUTOVER.md`, a
dedicated double-check phase) can now depend on:
`RenderSystem::CollectGpuDrivenBatches()` real, correct, and manually
GPU-verified end-to-end; `GpuDrivenBatchCache::EnsureCapacity()`/
`PackThisFrame()`/`TryGet()`/`Pipelines()` all real and ready to be imported
into a fresh `RenderGraphBuilder` every frame via `ImportBuffer()`; every real
`Mesh` built by `MeshAssetGpuCatalog.cpp` (the one currently-eligible content
path) now carrying real, load-time-computed local bounds; `Pipeline::VertexLayoutKind()`
available for resolving a batch's `PositionNormalInstanced` sibling pipeline
(PHASE5's own Section 3.2); and the exact, `ask_questions`-confirmed
GPU-skinning exclusion mechanism (a caller-supplied `std::unordered_set<VkBuffer>`,
cross-referenced inside `RenderSystem` against `Mesh::VertexBuffer()`) that
PHASE5 must now actually build from `Game::CollectGpuSkinningDispatchRequests()`
and thread through to the real, everyday Game View production call site.
