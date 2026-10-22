# GPU-Driven Rendering (Frustum Culling + Indirect Draw)

_Part of [GreatTamanaEngine](../../AGENTS.md)'s contributor conventions. See
[docs/README.md](../README.md) for the full documentation index._

The engine has a production GPU-driven culling + indirect-draw path for the case where
several entities share the exact same `(MeshHandle, PipelineHandle)` pair (a "batch").
It replaces "one `vkCmdDrawIndexed` per entity, no culling" for that case. Follow these
rules whenever touching it:

- **Batching eligibility is a fixed rule (`RenderBatching.h`'s `IsGpuDrivenEligible()`).**
  Do not loosen it without a new design pass. A `(MeshHandle, PipelineHandle)` group is
  eligible if and only if: (a) its instance count is `>= kMinInstancesForGpuDrivenBatch`
  (4); (b) `Mesh::HasIndexBuffer()` is true; (c) its `Pipeline::VertexLayoutKind()` is
  exactly `VertexLayout::PositionNormal` (untextured, non-instanced); (d) the mesh is not
  part of this frame's GPU-skinning output-buffer set; (e) the mesh has no cluster table
  (`Mesh::HasClusters()`), because the cluster path draws it. Every other `MeshRenderer`
  keeps drawing through the per-entity `Renderer::Submit()` path. Do not add a second,
  parallel batching mechanism for the excluded cases.
- **The batch path is Game view only.** Scene View keeps rendering every entity through
  the per-entity path. The per-batch cache (`GpuDrivenBatchCache`) is keyed by
  `(MeshHandle, PipelineHandle)` with no view dimension, so it cannot serve two cameras
  at once. Do not pass `batchedEntities` to a Scene View batch call without adding a
  view dimension to the cache. The cluster path does not have this limit: every buffer,
  parameter block and claimed-entity set there is per view scope.
- **The batch path needs `drawIndirectCount`, `multiDrawIndirect` and
  `drawIndirectFirstInstance`.** Core declares no batch when one is missing
  (`OldBatchPathSupported()`), so the CPU path draws. The commands write
  `firstInstance = i`, which is invalid without the last two features.
- **The atomic visible-count buffer must be reset to `0` by a real
  `vkCmdFillBuffer` (`ResourceAccess::TransferDst`) EVERY FRAME, ordered
  before the culling compute pass — the culling shader itself can never
  safely do this.** GLSL compute has no cross-workgroup ordering guarantee
  within one dispatch, so a "have one invocation reset it first" scheme is a
  genuine data race against every other workgroup's own `atomicAdd`. This is
  why `"<batch> ResetCount"` exists as its own separate render-graph pass,
  ordered before `"<batch> Culling"` purely by a natural write-after-write
  edge on the shared count-buffer handle.
- **The CPU-side draw count passed to `Renderer::SubmitIndirect()`'s
  `maxDrawCount` (the degenerate-padding fallback branch) must always be
  THIS FRAME'S real, current instance count — never `GpuDrivenBatchCache`'s
  own buffer capacity, which only ever GROWS across frames and never
  shrinks.** If a batch shrinks (e.g. 10 instances last frame, 6 this
  frame), this frame's culling dispatch only ever touches slots `0..5` —
  slots `6..9` still contain the PREVIOUS frame's real, non-degenerate
  commands. Using the cache's capacity instead of the real per-frame
  instance count here would silently redraw stale "ghost" instances that no
  longer exist in the batch.
- **The indirect-command output buffer (binding 1 of `Shaders/FrustumCull.comp`)
  is a flat, manually-indexed `uint values[]` array (`values[i*5 + 0..4]`),
  NEVER a GLSL `struct` array.** `IndirectDrawCommand` (`IndirectDrawTypes.h`)
  is 20 bytes of plain scalar members — not a multiple of 16 — and GLSL's
  `std430` layout rule rounds a STRUCT array up to 16-byte (`vec4`)
  alignment even under `std430` (that relaxation only ever applies to arrays
  of bare scalars/vectors, never arrays of structs). Declaring this buffer
  as an ordinary `struct` array would silently produce a 32-byte stride
  instead of the required tightly-packed 20 bytes, corrupting every element
  after the first — mirrors `GpuSkinningTypes.h`/
  `SkinVerticesPositionNormal.comp`'s own identical flat-array treatment for
  its own tightly-packed output buffer.
- **The "instances culled this frame" readback is a Transfer pass, not a raw command.**
  Core declares `"<batch> CopyCountForReadback"` as a `rg::TransferRecord::Readback` pass
  and calls `KeepBufferOutput(readbackHandle)` first. Without that call the graph culls
  the pass: a pass whose only write is a `Buffer` is never a graph root. The copy reuses
  the offscreen fence wait that already happens, so it adds no new wait.
- **Both the real (`vkCmdDrawIndexedIndirectCount`) and fallback
  (`vkCmdDrawIndexedIndirect` against a fixed, degenerate-padded array) code
  paths are genuinely implemented and reviewed, selected EXCLUSIVELY by a
  runtime capability probe** (`Renderer::SupportsDrawIndirectCount()` /
  `VulkanDevice::QueryDrawIndirectCountSupport()`, queried once at startup).
  Never assume, hardcode, or document which branch any particular
  development/CI machine happens to exercise — both must keep compiling and
  behaving correctly regardless.
- **A batched entity becomes an invisible gap in `"RenderOpaque"`'s own
  per-entity Frame Debugger attribution list — an accepted, narrow,
  cosmetic-introspection-only regression, not a rendering correctness
  issue.** The entity's geometry still appears correctly in every Frame
  Debugger replay-step preview image (replay always redraws every entity via
  the ordinary per-entity path); only the real `"RenderOpaque"` pass's own
  live per-entity `RecordEntityDraw()` call is skipped for a batched entity.
  Do not "fix" this by threading `batchedEntities` into
  `AddFrameDebuggerReplayPasses()` — that would change what the replay
  preview itself renders, which is explicitly out of scope.

Open items: hierarchical/two-phase occlusion culling, LOD selection, textured/bindless
batching, primitive-shape batching, GPU-skinned-mesh batching and async compute. The
cluster-based path with per-view buffers is documented in
[gpu-driven-clusters.md](gpu-driven-clusters.md).
