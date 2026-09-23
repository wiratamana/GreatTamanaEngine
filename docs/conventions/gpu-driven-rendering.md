# GPU-Driven Rendering (Frustum Culling + Indirect Draw)

_Part of [GreatTamanaEngine](../../AGENTS.md)'s contributor conventions. See
[docs/README.md](../README.md) for the full documentation index._

A seven-phase campaign (`task_manager/render-pass-5/`,
`PHASE0_MASTER_STRATEGY.md`) gave the engine a real, always-on, PRODUCTION
GPU-driven culling + indirect-draw path for the common case where several
entities share the exact same `(MeshHandle, PipelineHandle)` pair (a
"batch") — replacing the old "walk every `MeshRenderer`, issue one
`vkCmdDrawIndexed` per entity, unconditionally, no culling of any kind"
behavior for exactly that case. Follow these rules whenever touching this
feature:

- **Batching eligibility is a fixed, four-condition rule
  (`RenderBatching.h`'s `IsGpuDrivenEligible()`) — do not loosen it without a
  new, dedicated design pass.** A `(MeshHandle, PipelineHandle)` group is
  eligible if and only if: (a) its instance count is
  `>= kMinInstancesForGpuDrivenBatch` (4); (b) `Mesh::HasIndexBuffer()` is
  true; (c) its `Pipeline::VertexLayoutKind()` is EXACTLY
  `VertexLayout::PositionNormal` (untextured, non-instanced); (d) the mesh is
  NOT part of this frame's GPU-skinning output-buffer set. Every other
  `MeshRenderer` — every `VertexLayout::PositionColor` primitive, every
  textured (`PositionNormalUv`) submesh, every GPU-skinned model, every group
  below the threshold — keeps drawing through the byte-for-byte-unchanged
  per-entity `Renderer::Submit()` path, forever. Do not add a second,
  parallel batching mechanism for any of these excluded cases without first
  reading `PHASE0_MASTER_STRATEGY.md`'s own Locked Design Decisions 6-8.
- **This entire cutover is GAME VIEW ONLY, permanently.** Scene View
  (`RenderViewId::Named("Scene")`) and the rare direct-render-to-swapchain
  fallback (`AddPresentPass()`'s own fallback branch) both keep rendering
  EVERY entity — including every batch-eligible one — through the fully
  unmodified per-entity path. This was confirmed via `ask_questions` during
  this campaign's own dedicated PHASE5 pre-implementation review (see
  `PHASE5_STRATEGY_DOUBLE_CHECK_REPORT.md`) specifically because the
  per-batch GPU resource cache (`GpuDrivenBatchCache`) is keyed by
  `(MeshHandle, PipelineHandle)` alone, with NO view dimension — it cannot
  safely serve two different cameras' worth of culling results for the same
  batch at once. Do not thread the new `RenderSystem::Draw()`/`Game::Render()`
  `batchedEntities` exclusion parameter into any Scene View or fallback call
  site without first re-deriving (or adding) a view dimension to the cache.
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
- **The "instances culled this frame" readback is a raw command issued
  directly against `offscreenCmd`, NOT a fourth render-graph pass.** A pass
  whose only write is a `Buffer` is never treated as a graph-culling "root"
  (`RenderGraphCompiler::Compile()`'s reachability step: `isRoot = false`
  unconditionally for `ResourceKind::Buffer`), so a dedicated
  `"<batch> CopyCountForReadback"` render-graph pass with no downstream
  reader is silently culled every frame and its `execute` callback never
  runs — a real bug this campaign hit and fixed during PHASE6 (see
  `PHASE6_COMPLETION_REPORT.md`'s "A real bug found and fixed" section). The
  count-buffer copy back into `GpuDrivenBatchCache::countReadbackBuffer` is
  therefore issued as a plain `vkCmdCopyBuffer` (with its own explicit
  `ComputeShaderWrite -> TransferRead` barrier) directly against the
  offscreen command buffer, immediately after `m_renderGraph.Execute(...)`
  returns and before `Renderer::EndOffscreenRenderGraphRecording()` (which
  already fence-waits) — reusing that already-happening wait, never adding a
  new one, per `AGENTS.md`'s "Profiling" section.
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

Full campaign detail (phase-by-phase writeup, every Locked Design Decision,
every dedicated double-check finding, and an honest "what remains genuinely
open" section restating every Non-Goal — occlusion culling, hierarchical/
two-phase culling, LOD selection, textured/bindless batching, primitive-shape
batching, GPU-skinned-mesh batching, async compute):
`task_manager/render-pass-5/PHASE0_MASTER_STRATEGY.md` and
`task_manager/render-pass-5/CAMPAIGN_COMPLETION_REPORT.md`.
