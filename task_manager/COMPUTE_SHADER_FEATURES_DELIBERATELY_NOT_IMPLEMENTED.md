# COMPUTE_SHADER_FEATURES_DELIBERATELY_NOT_IMPLEMENTED.md

## Purpose of this document

Companion to `RENDERGRAPH_FUTURE_TODO_DELIBERATELY_NOT_IMPLEMENTED.md`, but
for the compute-shader campaign specifically (`COMPUTE_SHADER_MASTER_STRATEGY_v2.md`
through `COMPUTE_PHASE7_COMPLETION_REPORT.md`, plus the still-unimplemented
buffer-side half of `GPU_DRIVEN_RENDERING_COMPUTE_INDIRECT_STRATEGY_v1.md`).
Answers one question: **what was deliberately left out of the engine's new
compute-shader/`RenderGraph` integration, on purpose, and why?**

Nothing below is a bug or an oversight — every item was either explicitly
named as out-of-scope in the master strategy document's own "What We Will
NOT Do" section, in one of the seven child phase strategy documents' own
"What We Will NOT Do" sections, or in one of the seven
`COMPUTE_PHASEn_COMPLETION_REPORT.md` files' own "What was deliberately NOT
done" sections. This document exists purely to collect all of that into one
place, since it's currently scattered across eight separate files.

As of `COMPUTE_PHASE7_COMPLETION_REPORT.md`, the campaign's **texture-side**
half (Phases 1-7: resource vocabulary, `ComputePipeline`, descriptor
binding, dispatch, synchronization, `RenderGraph` integration, and a real,
shipped compute box-blur validation workload) is complete and shipped. The
**buffer-side** half (GPU frustum culling + an indirect draw buffer, owned
by `GPU_DRIVEN_RENDERING_COMPUTE_INDIRECT_STRATEGY_v1.md`'s own Phase D/G)
is NOT yet implemented — see Section C below.

---

## Section A — Campaign-wide refusals (Master Strategy's own "What We Will NOT Do")

| # | Item | Why deferred | What would unlock it |
|---|------|---------------|------------------------|
| 1 | **Async compute / a second (compute) queue** | Nothing existed yet to parallelize against; the whole campaign deliberately stays on the single existing graphics queue | A real, measured need once compute passes are shipped and running correctly on the graphics queue (now true — this is the natural next step) |
| 2 | **Bindless/descriptor-indexing infrastructure** (`VK_EXT_descriptor_indexing`) | Every compute pass uses the existing "one descriptor set per distinct resource combination" precedent `MaterialTexture` already established | A workload needing more than a handful of distinct resource combinations bound at once (e.g. a GPU-driven multi-material indirect batch) |
| 3 | **3D/volume textures, texture arrays, cubemap storage images** | `RWTexture`/`Texture` mean plain 2D only, matching `RenderTexture`/`Texture2D`'s own existing scope | A real feature that specifically needs volumetric/array/cubemap storage images (none exists in this engine) |
| 4 | **Shader reflection** (SPIRV-Cross or otherwise) to auto-derive descriptor layouts from GLSL source | Binding numbers are a documented, hand-maintained convention between C++ and GLSL, exactly like `GpuResourceFactory::MaterialDescriptorSetLayout()`'s existing fixed layout | A shader count large enough that manual binding-convention drift becomes a real, observed source of bugs |
| 5 | **Any concrete compute workload beyond the campaign's own two validation features** (GPU frustum culling, from the companion GPU-driven document; a compute box blur, from Phase 7) — explicitly, GPU vertex skinning, procedural mesh generation, or anything else | Named explicitly as the natural next candidate rather than folded in speculatively | Both validation workloads (buffer + texture) landing first, closing this campaign fully |
| 6 | **Transient (render-graph-pooled) `RWTexture` support** *(v2 addition)* | `rg::TextureDesc` (`RenderGraphTypes.h`) has exactly four fields — no usage-flags field at all — so `RenderGraphResourcePool::AcquireTexture()` has no way to opt a pooled texture into `VK_IMAGE_USAGE_STORAGE_BIT` | Extending `rg::TextureDesc` + threading a matching opt-in through `RenderGraphResourcePool::AcquireTexture()`, once a real workload needs a storage-capable texture that ISN'T a persistent, externally-owned resource |
| 7 | **`BufferHandle`-flavored `finalOutputs`/`ImportBuffer()`** *(v2 addition)* | `RenderGraphCompiler::Compile()`'s reachability root set (`finalOutputs`) is `TextureHandle`-only; there is no `BufferHandle` equivalent, and `RenderGraphBuilder` has no `ImportBuffer()` counterpart to `ImportTexture()` | A real workload needing a buffer-only pass kept alive with no in-graph texture-reaching consumer (today, a compute pass whose only write is a buffer survives culling only if some other, transitively-`finalOutputs`-reaching pass reads it — see `RenderGraphCompilerTests.cpp`'s `BufferOnlyWriteSurvivesCullingOnlyWhenAReaderReachesATextureFinalOutput`) |

---

## Section B — Per-phase refusals

### Phase 1 — Resource Vocabulary
- No typed/templated buffer wrapper (`Buffer<T>`) — `elementStride`/
  `elementCount` on `CreateStructuredBuffer()` are plain bookkeeping only.
- No 3D/array/cubemap storage images (restated from Section A.3).
- No automatic GLSL-format-qualifier-vs-`VkFormat` validation — a
  documented human convention, not a compile-time/runtime check.
- No change to `MaterialTexture`/`GpuResourceFactory::CreateMaterialTexture2D()`
  — confirmed already fully sufficient for a compute shader's read-only
  `Texture` binding, zero changes made.
- No change to `Buffer`'s public API beyond the new factory convenience
  method — no new fields, no new constructor overload.
- No transient/render-graph-pooled `RWTexture` (restated from Section A.6)
  — every `RWTexture` this campaign creates is externally-owned and
  persistent, imported per-frame via `ImportTexture()`.

### Phase 2 — Pipeline Infrastructure
- No shader permutation/variant system (specialization constants,
  `#define`-driven variants) — one `.comp` file compiles to exactly one
  `ComputePipeline`.
- No shader hot-reload — a `ComputePipeline` is built once, like `Pipeline`.
- No shader reflection of any kind (restated from Section A.4).
- No `RenderGraph` awareness whatsoever — `ComputePipeline` never sees a
  `PassContext`/`RenderGraphBuilder` (that's Phase 6's job).
- No descriptor-set-layout builder built here (that's Phase 3's job) — the
  throwaway validation shader's hand-built layout was deleted, not kept as
  a shortcut around Phase 3.
- No dispatch math/`Renderer::Dispatch()` built here (that's Phase 4's
  job) — the throwaway validation used a raw, hand-written
  `vkCmdDispatch(1,1,1)` purely for this phase's own isolated verification.
- The throwaway `Passthrough.comp` validation shader + its temporary
  `CMakeLists.txt`/`main.cpp` CLI call site were deleted before this phase
  was considered complete — never shipped.

### Phase 3 — Descriptor Binding Model
- No bindless descriptor indexing (restated from Section A.2).
- No automatic re-validation that a shader's GLSL bindings match the C++
  `DescriptorSetLayoutBuilder` calls — documented human convention only.
- No descriptor-set caching/deduplication across different compute passes
  — each future compute pass builds and owns its own layout/set.
- No support for a descriptor set spanning multiple frames-in-flight with
  independent double/triple-buffered copies — deferred to however
  `RenderGraph`'s own resource-pooling model wires this in.

### Phase 4 — Dispatch Execution
- No `vkCmdDispatchIndirect` (indirect dispatch driven by GPU-computed
  counts) — every dispatch in this campaign has a CPU-known group count at
  record time. Noted as future work alongside indirect DRAW (the
  companion GPU-driven document's own job).
- No automatic/adaptive work-group-size tuning based on queried hardware
  characteristics (subgroup size, etc.) — every compute shader's local
  size is a fixed, hand-chosen constant.
- No generalized "compute uniform buffer" system beyond the existing
  128-byte push-constant convention already used for graphics.
- No `gte_add_shader()`/`CompileShaders.cmake` `DEFINES` mechanism to share
  a local-group-size constant verbatim between GLSL and C++ — explicitly
  named as a deferrable "stretch, more robust option," never mandatory;
  the plain "restate the number as a comment-linked constant" approach was
  used instead.
- **`PassContext::recordDispatch` callback — superseded, never built at
  all** (v2 finding): `Renderer::Dispatch()` fuses into a pass's stats via
  the same `BeginGraphPassRecording()`-stored-callback mechanism
  `Renderer::Submit()` already uses; no new `PassContext` field was needed.
- `Renderer::Dispatch()` deliberately never touches `PassGpuStats::drawStats`
  — no meaningful compute equivalent of a "triangle count" exists today;
  GPU TIME is already fully automatic per-pass regardless of content.

### Phase 5 — Synchronization
- No automatic detection or warning for a missed intra-pass barrier — a
  pass whose `execute` callback issues more than one dispatch with a real
  read-after-write dependency between them is entirely the pass author's
  own responsibility to barrier correctly; undetected by any tooling.
- No cross-resource barrier batching — unaffected by this phase, still
  the original Render Graph campaign's own Phase 9 backlog item.
- No new "read-write in one" `ResourceAccess` value (e.g. a combined
  `ComputeShaderReadWrite`) — a true read-modify-write `RWTexture`/
  `RWStructuredBuffer` declares BOTH a `ComputeShaderRead` usage and a
  `ComputeShaderWrite` usage on the same handle instead.
- No `RenderGraphBuilder`/`PassBuilder` API changes — entirely Phase 6's
  job; this phase only made the barrier-planning machinery correctly
  able to handle a compute usage once Phase 6 gave pass authors a way to
  declare one.
- No `BufferHandle`-flavored `finalOutputs`/`ImportBuffer()` (restated
  from Section A.7) — explicitly NOT this phase's job to fix, only to
  document and add a regression test proving the constraint is real.

### Phase 6 — RenderGraph Integration
- No generic, scripted/JSON-driven pass declaration — every compute pass
  is still hand-authored C++ via `AddPass()`/`AddComputePass()`.
- No descriptor-set caching/deduplication across passes (restated from
  Phase 3) — demonstrated directly by the throwaway validation pass,
  which rewrites its own `ComputeDescriptorSet` every time it runs.
- No compute-to-compute pipeline "fusion"/optimization.
- No change to `RenderGraphResourcePool`'s memory-aliasing/pooling policy
  — and, per Phase 1's own scope note, a transient (render-graph-pooled)
  `RWTexture` is still not possible (restated from Section A.6).
- No `PassContext::recordDispatch` callback (restated from Phase 4) —
  confirmed unnecessary once Phase 4's own finding was re-checked against
  this phase's real integration work.

### Phase 7 — Validation, Testing, Tooling
- No production-quality blur (no adjustable kernel radius UI, no
  separable two-pass Gaussian, no bilinear-optimized sampling) — a
  validation vehicle for the compute-shader/`RenderGraph` plumbing, not a
  real visual feature request. A real blur/bloom/post-process feature, if
  ever wanted, should be its own, separate strategy document reusing this
  infrastructure.
- No automated visual-diff/screenshot-comparison testing — remains
  Tier 2/manual, per this engine's existing accepted testing bucket.
- No benchmark-mode/perf-regression CI for compute — out of scope for
  this campaign entirely.
- No folding of GPU vertex skinning into this validation phase (restated
  from Section A.5) — named explicitly as the next candidate, never
  attempted here.
- No changes to `RenderGraphSnapshot`/`BuildRenderGraphSnapshot()`/the
  Editor's "Render Graph" panel — confirmed (by inspection) that the
  panel's existing, pass/resource-shape-agnostic design already displays
  a compute pass and its resources correctly with zero code changes.
- **The buffer-side validation workload (GPU frustum culling via the
  companion GPU-driven document) was explicitly NOT implemented in this
  phase's session** — a separate document's own scope; see Section C.

---

## Section C — What remains genuinely open after Phase 7

Per `COMPUTE_PHASE7_COMPLETION_REPORT.md`'s own "What remains open"
section — honestly tracked, not silently dropped:

1. **Buffer-side validation (GPU frustum culling + indirect draw)** — the
   companion `GPU_DRIVEN_RENDERING_COMPUTE_INDIRECT_STRATEGY_v1.md`
   document's own Phase D/G. This is NOT yet started. The compute-shader
   campaign's own `ResourceAccess::ComputeShaderRead/ComputeShaderWrite/
   IndirectCommandRead` (Phase 5), `CreateStructuredBuffer()` (Phase 1,
   with its `extraUsage` parameter specifically added for an indirect-draw
   buffer), and `AddComputePass()` (Phase 6) are all already shipped and
   ready for that document's own workload to consume directly — see
   Section D below for the full "what's already reusable" breakdown. Per
   the master strategy document's own "Their Role" instruction: **do not
   consider the whole compute-shader campaign complete until this half
   also lands** and is manually verified with validation layers clean and
   visually confirmed correct.
2. **Transient (render-graph-pooled) `RWTexture` support** (restated from
   Section A.6) — `rg::TextureDesc` still has no storage-usage opt-in;
   every `RWTexture` in this campaign, including Phase 7's own
   `blurredSceneOutput`, remains an externally-owned, persistent resource,
   imported per-frame.
3. **`BufferHandle`-flavored `finalOutputs`/`ImportBuffer()`** (restated
   from Section A.7) — a buffer-only compute pass's write still needs an
   in-graph reader whose own chain reaches a real texture final output,
   or it is silently culled (Phase 5's own documented, tested constraint)
   — not lifted by this campaign.
4. **Async compute, bindless descriptors, 3D/array/cubemap storage
   images** (restated from Section A.1/A.2/A.3) — all explicitly out of
   scope for the whole campaign from Phase 1 onward, unchanged.
5. **GPU vertex skinning** (restated from Section A.5) — the natural next
   compute-shader workload candidate once BOTH halves of this campaign
   close, per `RENDERGRAPH_FUTURE_TODO_DELIBERATELY_NOT_IMPLEMENTED.md`,
   Section C.5 — intentionally not started here.
6. **Real GPU timestamp timing for a compute pass's own row in the
   Editor's "Render Graph"/"Profiler" panels** — confirmed NOT actually a
   gap by design inspection: `RenderGraph::ExecuteCompiledGraph()`'s
   timestamp bracket already covers every pass unconditionally (see
   `B1_REAL_GPU_TIMING_COMPLETION_REPORT.md`), so a compute pass already
   reports real GPU milliseconds today with zero further work needed.
   Listed here only for completeness/clarity, not as an open item.

---

## Section D — What the compute-shader campaign already supplies for the buffer-side (GPU-driven) work

Since `GPU_DRIVEN_RENDERING_COMPUTE_INDIRECT_STRATEGY_v1.md` was written
before this campaign shipped, its own Phase A/B/C plans describe building
infrastructure that is now **already shipped** and should be reused
directly rather than rebuilt. Whoever picks up that document's Phase D
onward should treat the following as already-satisfied prerequisites:

| GPU-driven doc's own plan | Already shipped by | Where |
|---|---|---|
| Phase A — `ComputePipeline`, shared SPIR-V loader, `.comp` build support | ✅ Done | `COMPUTE_PHASE2_COMPLETION_REPORT.md` — `src/Renderer/ComputePipeline.h/.cpp`, `Vulkan/ShaderModule.h/.cpp` |
| Phase A — descriptor-set-layout builder + compute descriptor pool | ✅ Done | `COMPUTE_PHASE3_COMPLETION_REPORT.md` — `Vulkan/DescriptorSetLayoutBuilder.h/.cpp`, `ComputeDescriptorSet.h/.cpp`, `GpuResourceFactory::AllocateComputeDescriptorSet()` |
| Phase A — dispatch math + `Renderer::Dispatch()` | ✅ Done | `COMPUTE_PHASE4_COMPLETION_REPORT.md` — `src/Renderer/ComputeDispatch.h`, `Renderer::Dispatch()` |
| Phase B — `ResourceAccess::ComputeShaderRead`/`ComputeShaderWrite`/`IndirectCommandRead` + `RequiredStateFor()` handling | ✅ Done | `COMPUTE_PHASE5_COMPLETION_REPORT.md` — all three enumerators added, including `IndirectCommandRead`'s barrier state, ready and waiting for a real consumer |
| Phase A/B/generalized `AddComputePass()`/`WriteTexture()`/`ReadBuffer()`/`WriteBuffer()`/`resolveBuffer()`/`resolveTexture()` | ✅ Done | `COMPUTE_PHASE6_COMPLETION_REPORT.md` |
| `CreateStructuredBuffer()` with an `extraUsage` parameter (for `VK_BUFFER_USAGE_INDIRECT_COMMAND_BIT`) | ✅ Done | `COMPUTE_PHASE1_COMPLETION_REPORT.md` — added from day one specifically so the GPU-driven document's own indirect-draw buffer never needs a second, near-duplicate factory method |

**Still squarely the GPU-driven document's own job, not touched by this
campaign at all:**

- The real `Shaders/FrustumCull.comp` culling shader itself.
- The indirect-draw-buffer binary layout (`VkDrawIndexedIndirectCommand`
  mirror + per-instance culling input struct).
- `Renderer::SubmitIndirect()` (issuing `vkCmdDrawIndexedIndirect(Count)`),
  and the device-capability probe for `drawIndirectCount` support.
- A CPU-side culling-input SSBO populated from `RenderSystem`/ECS
  transform+bounds data — no per-instance bounding-sphere data exists in
  the ECS today at all (`Transform`/`MeshRenderer` carry no bounds field).
- An "unknown/indirect" `DrawStats` representation for a draw whose real
  count the CPU never reads back.
- The actual buffer-only-pass-survives-culling reachability design for the
  real culling→indirect-draw pass graph (Section C.3's constraint is
  proven correct by a regression test, but the real pass graph still has
  to be built with it consciously in mind).
- Editor tooling for an "instances culled this frame" readout.

See `GPU_DRIVEN_RENDERING_COMPUTE_INDIRECT_STRATEGY_v1.md`'s own body for
the full phase-by-phase plan — only its Phase A/B infrastructure
descriptions are now superseded by the table above; its Phase C onward
(indirect draw call support, the real culling shader, validation, Editor
tooling) is unchanged and still fully open work.
