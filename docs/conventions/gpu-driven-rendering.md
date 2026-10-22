# GPU-Driven Rendering (Cluster Culling + Indirect Draw)

_Part of [GreatTamanaEngine](../../AGENTS.md)'s contributor conventions. See
[docs/README.md](../README.md) for the full documentation index._

The cluster path is the engine's GPU-driven path. A compute pass culls clusters per view and one
counted indirect draw submits the survivors. It is a render-feature kit verb (`ClusterDraw`) used by
the `GpuDrivenScene` feature and by depth-only lanes. Format, tools, claim rules and debug routes are
in [gpu-driven-clusters.md](gpu-driven-clusters.md). Follow these rules when touching it:

- **Eligibility is decided once per frame by Core.** `SelectClusterCandidates()` takes a draw command
  when the mesh has a cluster table (or is synthesizable and has at least
  `kMinInstancesForSynthesizedClusters` (4) instances), has an index buffer, uses the untextured
  `VertexLayout::PositionNormal` pipeline, is not GPU-skinned and has a finite world matrix.
  Everything else keeps drawing through the per-entity `Renderer::Submit()` path. Do not add a second,
  parallel batching mechanism.
- **Per-view buffers.** Every buffer, parameter block, descriptor set and claimed-entity set is keyed by
  view scope, lane and part slot. Two views never share one. Do not add a cache without a view dimension.
- **Capabilities.** The path needs `drawIndirectCount`, `multiDrawIndirect` and
  `drawIndirectFirstInstance` (`ClusterPathSupported()`). When one is missing the verb is inert and the
  CPU path draws. Commands write `firstInstance = slot`, which is invalid without the last two features.
- **Reset the visible-count buffer with a real fill, every frame, before the cull pass.** A compute
  shader cannot reset it safely: there is no cross-workgroup ordering inside one dispatch, so a reset
  by one invocation races with the other workgroups' `atomicAdd`. The chain declares
  `Reset` (transfer fill), `Cull` (compute) and `Draw` (indirect) in that order.
- **Claim only after proof.** Entities enter the claimed set only after reset, cull and draw were all
  declared. A refusal at any step leaves the entities to the CPU draw. A toggled-off pass of the chain
  means the whole chain is not declared.
- **The indirect-command buffer is a flat `uint values[]` array (`values[i*5 + 0..4]`), never a GLSL
  struct array.** `IndirectDrawCommand` is 20 bytes. `std430` rounds a struct array stride up to 16
  bytes, which would corrupt every element after the first.
- **The visible-count readback is a graph pass.** The stats chain clears, copies and host-copies the
  count together or not at all. A stats refusal never revokes a claim.
- **Frame Debugger captures never claim.** A frame with a capture armed draws every entity on the CPU
  path so the recorded draw list and replay cutoffs stay exact.
- **Resolve at declare time, look up again at execute time.** The verb resolves mesh, table, pipeline and
  descriptor set in `Plan()`. The execute lambda re-resolves the mesh with a null check: assert in debug,
  draw nothing in release.

Open items: two-phase occlusion culling (the cluster path has a one-frame-old Hi-Z), LOD selection,
textured/bindless batching, primitive-shape batching, GPU-skinned-mesh batching and async compute.
