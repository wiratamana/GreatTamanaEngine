# PHASE4 — RenderSystem Batching + Per-Batch GPU Resource Management

## Parent
`PHASE0_MASTER_STRATEGY.md` — read it first. Also re-read
`PHASE1`–`PHASE3`'s own completion reports (and PHASE3's dedicated
double-check findings) before starting.

## Step 1: The Goal (Where are we going?)

Give `RenderSystem` the ability to:
1. Group this frame's `DrawCommand`s (`RenderSystem::CollectRenderables()`'s
   own existing output) by `(MeshHandle, PipelineHandle)`, and decide — via a
   PURE, Tier-1-testable rule — which groups are "GPU-driven-eligible
   batches" per PHASE0's Locked Design Decision 7.
2. For each eligible batch, own a small set of PERSISTENT (created once,
   reused/resized across frames, never recreated every frame) GPU buffers:
   the per-instance culling-input SSBO, the indirect-command output buffer,
   and the count buffer — mirroring GPU Vertex Skinning's own Phase 4
   per-model resource cache precedent exactly
   (`task_manager/gpu_skinning/GPU_SKINNING_PHASE4_PER_MODEL_RESOURCE_MANAGEMENT_STRATEGY_v1.md`).
3. Every frame, for every eligible batch, repack that batch's live
   `Transform`/`Mesh`-bounds data (PHASE1's `PackCullingInstanceInput()`/
   `TransformAABB()`) into its persistent input SSBO — this is the ONE
   genuinely-every-frame-varying step; the buffers themselves are not
   reallocated unless the batch's own instance count changed since last
   frame.

This phase deliberately builds NO render-graph pass declarations and issues NO
`vkCmdDispatch`/indirect draw of any kind yet — it only builds the DATA this
frame's batches need, ready for PHASE5 to actually wire into passes. This
mirrors GPU Vertex Skinning's own Phase 4/5 split (per-model resource
management, THEN render-graph pass declaration as a separate, later phase).

## Step 2: The Situation (Where are we now?)

- `RenderSystem::CollectRenderables()` (`src/Game/RenderSystem.cpp`, line
  ~23) returns a flat `std::vector<DrawCommand>` — no grouping of any kind.
  This function is ALREADY pure/Tier-1-testable (`Registry&` only, no live
  Renderer) — confirmed by direct read; the new grouping logic should be a
  SEPARATE, similarly pure function operating on its OUTPUT, not a change to
  `CollectRenderables()` itself (keeps the existing function, and its
  existing `tests/Game/RenderSystemTests.cpp` coverage, completely
  unaffected).
- `Mesh` (PHASE1 addition) now optionally carries a local-space `AABB` — but
  `RenderSystem`'s own `m_meshes`/`m_pipelines`/`m_textures` `ResourcePool`s
  (`RenderSystem.h`, private section) are the only place a `MeshHandle`
  resolves to a real `Mesh&` — grouping-by-handle (PURE, no live Mesh needed)
  and per-batch-buffer-management (impure, needs real `Mesh`/`Pipeline`
  objects and a live `Renderer&`) are therefore naturally two separate
  concerns, exactly mirroring `CollectRenderables()` (pure) vs. `Draw()`
  (impure) already being split today.
- `Game::CollectGpuSkinningDispatchRequests()`/
  `AnimationSystem::CollectModelsNeedingGpuSkinningThisFrame()` (referenced
  by `src/Application/RenderPasses.cpp`'s `AddGpuSkinningPasses()`) is the
  existing mechanism that identifies which meshes are GPU-skinned THIS frame
  — this phase's own eligibility rule (Locked Design Decision 7(d), PHASE0)
  needs to exclude any `MeshHandle` that appears in that same set. **Read
  `AnimationSystem.h`/`.cpp` and `Game.h`/`.cpp` directly before writing this
  exclusion check** — the exact shape of "which `MeshHandle`s are GPU-skinned
  this frame" was not independently re-verified while writing this document;
  if the real mechanism doesn't expose a `MeshHandle` (e.g. it only exposes a
  buffer/entity list), use `ask_questions` to confirm the right way to
  cross-reference the two before inventing a new, parallel bookkeeping
  mechanism.
- `GPU_SKINNING_PHASE4_PER_MODEL_RESOURCE_MANAGEMENT_STRATEGY_v1.md`
  (`task_manager/gpu_skinning/`) is the exact precedent for "a persistent,
  keyed cache of per-thing GPU buffers, resized only when the thing's own
  size changes, rebuilt/re-imported into the render graph every frame via
  `ImportBuffer()`" — read it in full before designing this phase's own
  cache; do not re-derive this pattern from scratch.

## Step 3: The Plan

### 3.1 — Pure batching logic (Tier-1-testable, new file)

`src/Game/RenderBatching.h`/`.cpp` (new files, mirroring `RenderSystem.h`'s
own "pure logic lives in its own testable function" precedent):

```cpp
// One (MeshHandle, PipelineHandle) group's worth of DrawCommands, in the
// same relative order CollectRenderables() produced them.
struct RenderBatchGroup {
    MeshHandle mesh;
    PipelineHandle pipeline;
    std::vector<DrawCommand> commands; // ALWAYS >= 1; ownership/lifetime mirrors DrawCommand's own plain-data nature.
};

// Groups `commands` (CollectRenderables()'s own output) by (mesh, pipeline) -
// stable, preserving first-seen group order and each group's own internal
// command order (so output is deterministic/diffable across frames for the
// SAME input, a real testability requirement). Pure, no Renderer/live-GPU
// dependency - the same "operates on plain DrawCommand data only" property
// CollectRenderables() itself already has.
std::vector<RenderBatchGroup> GroupDrawCommandsByMeshAndPipeline(const std::vector<DrawCommand>& commands);

// Locked Design Decision 7 (PHASE0) - decides whether ONE group is
// GPU-driven-eligible. `hasIndexBuffer`/`vertexLayout` describe the group's
// shared Mesh/Pipeline (resolved by the CALLER, which has live access - see
// RenderSystem::Draw() in 3.3 below); `isGpuSkinned` is true if this group's
// MeshHandle is part of this frame's GPU-skinning output-buffer set (see
// Step 2's own caveat above about verifying the real cross-reference
// mechanism). Pure decision, no live GPU/Renderer state read directly by
// this function itself.
bool IsGpuDrivenEligible(const RenderBatchGroup& group, bool hasIndexBuffer, VertexLayout vertexLayout,
    bool isGpuSkinned, std::size_t minInstancesForGpuDrivenBatch);

inline constexpr std::size_t kMinInstancesForGpuDrivenBatch = 4; // Locked Design Decision 7, PHASE0 - tunable.
```

- New test file `tests/Game/RenderBatchingTests.cpp` — covers
  `GroupDrawCommandsByMeshAndPipeline()` (empty input; all-same-group input;
  several interleaved groups; order preservation) and `IsGpuDrivenEligible()`
  (every true/false combination of its 4 boolean-ish inputs against the
  threshold, including the exact boundary at
  `kMinInstancesForGpuDrivenBatch`).

### 3.2 — Per-batch persistent GPU resource cache

New `src/Renderer/Culling/GpuDrivenBatchCache.h`/`.cpp` (mirrors GPU
Skinning's own per-model resource cache shape from its own Phase 4 document —
read that document's exact class/method shape before designing this one, do
not re-derive independently):

- Keyed by `(MeshHandle, PipelineHandle)` (a small, hashable key struct) —
  **deliberately no view/camera dimension.** This is safe and correct because
  this whole campaign's GPU-driven cutover is GAME VIEW ONLY (PHASE0's Locked
  Design Decision 11): Scene View and the rare direct-render-to-swapchain
  fallback never consult this cache at all, they keep rendering every entity
  through the unmodified per-entity path forever, so this cache is never
  asked to serve two different cameras' worth of culling results for the
  same batch at once. If a future campaign ever extends batching to Scene
  View too, this key must grow a view dimension first — do not silently
  assume this cache generalizes to multiple concurrently-active views.
- For each eligible batch, owns:
  - A `Buffer` sized for `instanceCount` `GpuCullingInstanceInput` entries
    (`BufferMemoryUsage::CpuToGpu`, host-visible/persistently-mapped — this
    buffer is re-written from the CPU every frame via `Buffer::Upload()`/
    `Mesh::UpdateVertexData()`'s own established host-write pattern, NOT a
    device-local buffer needing a staging upload each frame — mirrors
    `CreateSkinnedMesh()`'s own host-visible vertex buffer precedent for the
    exact same "written by the CPU every frame" reason).
  - A `Buffer` sized for `instanceCount` `IndirectDrawCommand` entries
    (`BufferMemoryUsage::GpuOnly`, `extraUsage =
    VK_BUFFER_USAGE_INDIRECT_COMMAND_BIT`, via `CreateStructuredBuffer()`) —
    sized for the WORST CASE (every instance survives), regardless of
    `useCompaction` mode, since a compacted array can never need MORE slots
    than the input has instances.
  - A small `Buffer` for the atomic count (`BufferMemoryUsage::GpuOnly`, one
    `uint32_t`, `extraUsage = VK_BUFFER_USAGE_INDIRECT_COMMAND_BIT`) — ⚠️
    **RESOLVED (previously an open question in this document — confirmed by
    `PHASE3_STRATEGY_DOUBLE_CHECK_REPORT.md`, folded into
    `PHASE3_FRUSTUM_CULL_COMPUTE_SHADER_AND_PIPELINE.md`'s own Situation
    section): per the Vulkan spec, this buffer MUST be created with BOTH
    `VK_BUFFER_USAGE_INDIRECT_COMMAND_BIT` (required by
    `vkCmdDrawIndexedIndirectCount`'s own `countBuffer` argument) AND
    `VK_BUFFER_USAGE_STORAGE_BUFFER_BIT` (needed for `Shaders/FrustumCull.comp`'s
    own `atomicAdd`/write access to it as a plain SSBO) — both flags together,
    on the SAME buffer, simultaneously, is legal Vulkan; `CreateStructuredBuffer()`
    already ORs in `VK_BUFFER_USAGE_STORAGE_BUFFER_BIT` unconditionally, so
    only `extraUsage` needs to be passed explicitly, unconditionally (not
    gated behind a `SupportsDrawIndirectCount()` check — the fallback path
    also benefits from `CreateStructuredBuffer()`'s automatic
    `VK_BUFFER_USAGE_TRANSFER_DST_BIT`, which this same buffer also needs for
    its own mandatory per-frame `vkCmdFillBuffer` reset, see PHASE3/PHASE5).**
    Also remember: this buffer must contain exactly `0` before every dispatch
    of `Shaders/FrustumCull.comp` — this class does NOT reset it itself
    (PHASE5 declares the real `vkCmdFillBuffer` reset pass; see
    `PHASE3_FRUSTUM_CULL_COMPUTE_SHADER_AND_PIPELINE.md`'s own Section 3.2 for
    why the shader can never safely do this on its own).
- `EnsureCapacity(key, instanceCount)` — reallocates ONLY when
  `instanceCount` grew past what's currently allocated for that key (mirrors
  `RenderGraphResourcePool`'s own "reuse if it already matches, don't
  reallocate needlessly" philosophy, applied to a persistent, NOT
  render-graph-pooled, resource this time — deliberately NOT going through
  `RenderGraphResourcePool` at all, since these buffers must survive across
  frames by IDENTITY, imported fresh into a new graph each frame via
  `ImportBuffer()`, exactly like GPU Skinning's own per-model output buffer).
- `PackThisFrame(key, const RenderBatchGroup&, Registry&)` — for every
  `DrawCommand` in the group, resolves its `Mesh`'s local `AABB`
  (PHASE1/PHASE1's `Mesh::LocalBounds()` accessor — add this small accessor
  to `Mesh.h` now if PHASE1 didn't already; cross-check against PHASE1's
  completion report), transforms it by that command's own `model` matrix
  (`TransformAABB()`), and calls `PackCullingInstanceInput()` — writing the
  whole batch's worth of instances into the persistent input buffer via ONE
  `Buffer::Upload()` call (never per-instance).
- This class owns NO render-graph handles itself (`BufferHandle`) — it only
  owns raw `Buffer`s; PHASE5 is what imports them into a fresh
  `RenderGraphBuilder` every frame via `ImportBuffer()`, mirroring
  `AddGpuSkinningPasses()`'s own `builder.ImportBuffer(request.name,
  request.outputBuffer, request.outputBufferSize)` call exactly.

### 3.3 — `RenderSystem` integration point

- `RenderSystem` gains a new method (name TBD during implementation, e.g.
  `CollectGpuDrivenBatches(Registry&, Renderer&)`) that: calls
  `CollectRenderables()`, calls `GroupDrawCommandsByMeshAndPipeline()`,
  resolves each group's real `Mesh&`/`Pipeline&` (via its own existing
  `m_meshes`/`m_pipelines` pools — exactly like `Draw()` already does),
  applies `IsGpuDrivenEligible()`, and for every eligible group calls into
  `GpuDrivenBatchCache::EnsureCapacity()`/`PackThisFrame()`. Returns enough
  information (which groups were eligible + this frame's packed buffer
  identities) for PHASE5's pass-declaration code to consume.
- **This method does NOT replace `Draw()`, and `Draw()` itself is NOT
  modified in this phase** — PHASE5 is what actually threads eligible-batch
  exclusion into `Draw()`'s own per-`DrawCommand` loop (so a batched
  `DrawCommand` is skipped by the old per-entity path once its own indirect
  pass exists) — keeping this phase's own diff strictly additive and
  independently compilable/testable.

### What We Will NOT Do (this phase)

- No render-graph pass declaration, no `AddComputePass()`/`ReadBuffer()`/
  `WriteBuffer()` call of any kind (PHASE5's job).
- No change to `RenderSystem::Draw()`'s own existing per-entity loop
  behavior — it keeps drawing EVERY `DrawCommand`, including ones that will
  become batch-eligible once PHASE5 lands, unmodified, so this phase alone
  introduces zero rendering-behavior change of any kind (a green, fully
  reversible checkpoint).
- No change to `Mesh.h` beyond a small, additive `LocalBounds()`-style
  accessor if PHASE1 didn't already add one.

### Compile check

Fast, targeted incremental compile check only. Confirm the new
`RenderBatchingTests.cpp` Tier-1 tests pass. Manually sanity-check
`GpuDrivenBatchCache`'s allocate/repack path against a live scene with a
real, multi-instance batch (read back the packed input buffer's first few
entries via a throwaway debug print/breakpoint if needed — no automated Tier
2 coverage is required or expected here, per this project's own accepted
Tier 2 testing gap, `AGENTS.md`). Write `PHASE4_COMPLETION_REPORT.md`
(include the final, real cross-reference mechanism found for the GPU-skinning
exclusion check in Locked Design Decision 7(d) — flag clearly if this
required an `ask_questions` detour and what was decided). Commit.
