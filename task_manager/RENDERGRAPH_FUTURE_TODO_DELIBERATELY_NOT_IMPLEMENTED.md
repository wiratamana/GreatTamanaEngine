# RENDERGRAPH_FUTURE_TODO_DELIBERATELY_NOT_IMPLEMENTED.md

## Purpose of this document

This is an analysis pass over the whole Render Graph campaign (Phases 1-8,
`RENDERGRAPH_PHASE0_MASTER_STRATEGY_v2.md` through
`RENDERGRAPH_PHASE8_COMPLETION_REPORT.md` /
`RENDERGRAPH_CAMPAIGN_COMPLETION_REPORT.md`), plus `TODO.md` and `AGENTS.md`,
answering one question: **what was deliberately left out of a modern game
engine's rendering backbone, on purpose, and why?**

Nothing below is a bug or an oversight — every item was either explicitly
named as out-of-scope in the phase documents themselves (mostly
`RENDERGRAPH_PHASE9_ADVANCED_FUTURE_SCOPE_STRATEGY_v2.md`, the campaign's own
backlog file) or is a gap this analysis identified by comparing what exists
today against what a modern renderer generally needs. This document exists
so the next milestone (GPU-driven rendering — see the companion strategy
document, `GPU_DRIVEN_RENDERING_COMPUTE_INDIRECT_STRATEGY_v1.md`) can be
planned against a complete, accurate picture of the gap, not a guess.

Organized into four sections:
- **A** — items the campaign's own Phase 9 backlog already names explicitly.
- **B** — items the campaign's own completion report flags as still open
  even after Phase 8 shipped.
- **C** — gaps this analysis identified that are necessary for a "modern
  game engine" but were never even discussed in the Render Graph docs
  (mostly because nothing in Phases 1-8 needed them yet).
- **D** — a prioritized reading of all of the above, specifically through
  the lens of "what genuinely blocks GPU-driven rendering".

---

## Section A — Explicit Phase 9 backlog (already named, already reasoned about)

These are lifted from `RENDERGRAPH_PHASE9_ADVANCED_FUTURE_SCOPE_STRATEGY_v2.md`
itself — restated here with the "what first" precondition each one carries,
since that precondition is exactly what the GPU-driven-rendering milestone
is now supplying for several of them.

| # | Item | Why deferred | What unlocks it |
|---|------|---------------|-------------------|
| 1 | **Memory aliasing** (transient resources with non-overlapping lifetimes sharing one physical allocation) | `RenderGraphResourcePool`'s conservative "at most one virtual resource per pool entry per frame" rule already gives correctness; aliasing is a pure memory-savings optimization with real `GpuMemoryTracker` accounting complexity | Real, observed memory pressure from a multi-pass feature (a post-process chain, a G-buffer) |
| 2 | **Async compute / multi-queue submission** | Zero compute shaders exist anywhere in the engine yet — nothing to parallelize | **UPDATE: the "nothing to parallelize" precondition is now GONE** — the compute-shader campaign (`COMPUTE_SHADER_MASTER_STRATEGY_v2.md`, Phases 1-7) has shipped real compute passes running correctly on the graphics queue (a compute box-blur, `ComputeBlurValidation`). Async compute itself is STILL not implemented (still deliberately deferred, still single-queue-only), but moving a compute pass to a dedicated compute queue is now a real, measurable optimization to evaluate rather than speculative machinery — see `COMPUTE_SHADER_FEATURES_DELIBERATELY_NOT_IMPLEMENTED.md`, Section A.1 |
| 3 | **Compute passes as first-class graph citizens** | No compute shader exists anywhere in this engine yet; `ResourceAccess` (Phase 1) was deliberately scoped to graphics-only access kinds | **✅ DONE** — see `COMPUTE_SHADER_MASTER_STRATEGY_v2.md`'s full seven-phase campaign (`COMPUTE_PHASE1_COMPLETION_REPORT.md` through `COMPUTE_PHASE7_COMPLETION_REPORT.md`): `ComputePipeline`, `ResourceAccess::ComputeShaderRead/ComputeShaderWrite/IndirectCommandRead`, `AddComputePass()`/`PassBuilder::WriteTexture()`/`ReadBuffer()`/`WriteBuffer()`, and a real, shipped compute pass (the box-blur validation workload) are all live today. The companion GPU-driven-rendering document's own Phase A/B are consequently already satisfied — see its own "Step 0: Status Update" |
| 4 | **Subpass merging / tile-based-renderer (mobile) optimizations** | This engine targets desktop Vulkan only, no mobile target on the roadmap | A stated requirement to target mobile hardware |
| 5 | **Temporal/history resources** (a resource whose previous frame's content is a declared input to this frame, e.g. TAA) | No temporal-effect feature (TAA, motion blur, temporally accumulated GI) exists yet | A real temporal-effect feature actually being built |
| 6 | **Cross-resource barrier batching** (combining several resources' barriers into one `vkCmdPipelineBarrier2` call) | Pure performance micro-optimization, no correctness benefit; pass counts are tiny today | Real GPU-timing evidence that barrier submission overhead is significant at this engine's pass count |
| 7 | **Stale pooled-resource eviction/trim policy** | `RenderGraphResourcePool` never evicts a pool entry whose desc stopped matching anything (e.g. after a one-time Editor panel resize) — bounded, small, unobserved cost | A real, observed memory-growth symptom traced to accumulated stale pool entries |
| 8 | **Data-driven / scripted pass declaration** (JSON/DSL-authored render pipeline instead of hand-written `AddPass()` C++) | This engine's whole philosophy is hand-authored C++; there isn't even a scene serialization format yet | Scene serialization (`TODO.md`'s own top engine-roadmap item) landing first |
| 9 | **Widening `Profiling::GpuPass` beyond three fixed named passes** (`GameView`/`SceneView`/`Present`) | Real, separate blast radius across four already-shipped Profiler files; explicitly out of scope for the whole nine-phase campaign | Real, sustained demand for a NEW pass (e.g. a compute culling pass, a shadow pass) to get its own dedicated "Profiler" panel line, not just a "Render Graph" panel line |
| 10 | **Full multi-color-attachment (MRT) support** + the matching `Pipeline` multi-format-attachment change (`Pipeline.cpp`'s `VkPipelineRenderingCreateInfo::colorAttachmentCount` is hardcoded to `1`) | Zero real passes in Phases 1-8 use more than one color attachment; building this speculatively was explicitly identified as a v1 mistake and removed in v2 | A real G-buffer / deferred-shading pass being designed — build BOTH halves (graph-side `RenderTargetSet` + `Pipeline` change) together, never one without the other |

---

## Section B — Explicitly open per the campaign's own completion report

From `RENDERGRAPH_CAMPAIGN_COMPLETION_REPORT.md`'s own "What is genuinely
proven vs. what remains open" and the Phase 6/7/8 completion reports' own
"Handoff notes":

1. **~~Real GPU timing is still not wired up for render-graph passes.~~ -
   CLOSED, see `B1_REAL_GPU_TIMING_STRATEGY_v1.md` /
   `B1_REAL_GPU_TIMING_COMPLETION_REPORT.md`.** `RenderGraph::LastKnownStatsFor()`'s
   `timing` field is now real, driver-measured data for every surviving
   pass - a brand-new, dedicated, name-keyed `RenderGraphTimestampPool`
   (`src/Renderer/RenderGraph/RenderGraphTimestampPool.h`) now backs it,
   built directly on `RenderGraphNameSlotTable`'s already-proven name->slot
   assignment logic, rather than extending `GpuTimingService`'s fixed
   3-slot design in place - a full-repository grep (performed as part of
   this change, see the completion report's own evidence) confirmed
   `GpuTimingSlot::Offscreen0/Offscreen1/SwapchainPresent` have ZERO
   remaining real (non-`nullopt`) production callers as of Phase 7's
   migration, so `GpuTimingService`/`VulkanQueryPool`/`GpuTimingSlot`
   themselves were left completely untouched (still serving
   `AssetPreviewMesh`/`BoneViewerWindow`'s own independent previews). Both
   the "Render Graph" and "Profiler" Editor panels now show identical,
   real GPU-time numbers for `GameView`/`SceneView`/`Present` every frame.
2. **~~The campaign's own single most novel capability — a genuine
   cross-pass texture READ, synthesized into a real barrier — has never
   been exercised by a real, shipped pass.~~ - CLOSED (texture side), see
   `COMPUTE_PHASE7_COMPLETION_REPORT.md`.** The compute-shader campaign's
   real, shipped compute box-blur pass (`ComputeBlurValidation`) declares
   `ReadTexture(sceneViewHandle, ShaderRead)` against the Scene view's own
   `RenderTexture` — WRITTEN by an earlier graphics pass in the SAME
   offscreen `RenderGraph::Execute()` call — synchronized entirely by the
   existing, unmodified barrier planner
   (`ApplyUsageBarrierIfNeeded()`/`RequiredStateFor()`), with no hand-written
   barrier anywhere in the new pass's own code. This is exactly the
   "pass B consumes pass A's output through the graph" pattern this item
   originally called out as unproven — now proven, for textures.
   `FinalizeRenderTextureForExternalSampling()` remains in place for Dear
   ImGui's own OUTSIDE-the-graph sampling of Game/Scene views (an
   unrelated, still-real workaround for a different consumer). **What
   remains open**: the BUFFER-side equivalent (`ReadBuffer()`,
   `RequiredStateFor()` for a storage buffer/`IndirectCommandRead`) is
   still never exercised against a real workload — that remains
   `GPU_DRIVEN_RENDERING_COMPUTE_INDIRECT_STRATEGY_v1.md`'s own Phase D/G
   job specifically, and directly relevant for GPU-driven rendering: a GPU
   culling compute pass writing an indirect-draw buffer that a LATER
   graphics pass reads is the same pattern, applied to a buffer instead of
   a texture.
3. **`Renderer::Present()`/`FramePresenter::Present()` (the old
   `FrameRecorder`-based scaffold) is confirmed dead code but was never
   deleted.** Cosmetic cleanup debt, not a functional gap, but worth
   clearing before this codebase grows a second (compute) rendering path
   that could confuse a future reader about which of two present paths is
   real.
4. **Every item in Section A above**, restated by reference (Phase 9's
   backlog is the campaign's own forward-looking half of "what remains
   open").

---

## Section C — Gaps identified by this analysis, not previously written down

These were never discussed in any Render Graph phase document (because
nothing in Phases 1-8's three real passes ever needed them), but are
standard expectations of a "modern game engine" and are directly relevant
to (or blocking) GPU-driven rendering specifically.

### C.1 — ~~No compute shader capability anywhere in the engine~~ - CLOSED
**UPDATE: this gap is closed.** `COMPUTE_SHADER_MASTER_STRATEGY_v2.md`'s
seven-phase campaign shipped `ComputePipeline`
(`src/Renderer/ComputePipeline.h/.cpp`), `GpuResourceFactory::
CreateComputePipeline()`, `RenderGraphTypes.h`'s `ResourceAccess::
ComputeShaderRead`/`ComputeShaderWrite`/`IndirectCommandRead`, a real
`.comp` shader compiled through `gte_add_shader()`
(`src/Shaders/BoxBlur.comp`), and a real, shipped compute pass declared via
`AddComputePass()`. See `COMPUTE_PHASE1_COMPLETION_REPORT.md` through
`COMPUTE_PHASE7_COMPLETION_REPORT.md`, and
`COMPUTE_SHADER_FEATURES_DELIBERATELY_NOT_IMPLEMENTED.md` for what was
deliberately left out of that campaign instead. **What remains open**:
this was only ever the load-bearing gap for the whole next milestone's
INFRASTRUCTURE half — the actual GPU-driven CULLING feature itself (C.3
below) and indirect draw support (C.2 below) are still unbuilt.

### C.2 — No indirect draw support anywhere
`FrameRecorder.cpp`/`RenderGraph.cpp`'s `PassContext::recordDraw` and every
draw call in the engine goes through `vkCmdDraw`/`vkCmdDrawIndexed` with
CPU-supplied, per-item parameters (see `DrawStats.h`'s own
`AccumulateDrawStats()`, which assumes exactly this shape). There is no
`VkDrawIndexedIndirectCommand` struct anywhere in this codebase, no GPU
buffer laid out to hold an array of them, and no
`vkCmdDrawIndexedIndirect`/`vkCmdDrawIndexedIndirectCount` call site. This
is the second load-bearing gap for GPU-driven rendering — a compute shader
that CULLS objects is only half the win; the other half is a single
indirect draw call replacing thousands of individual `Renderer::Submit()`
calls, and neither half exists today. **Still fully open** — this is
`GPU_DRIVEN_RENDERING_COMPUTE_INDIRECT_STRATEGY_v1.md`'s own Phase C, and
none of it was touched by the compute-shader campaign (which is
buffer-storage/dispatch-shaped, not indirect-draw-shaped). One shipped
piece it CAN lean on directly: `GpuResourceFactory::CreateStructuredBuffer()`
already accepts an `extraUsage` parameter for exactly the
`VK_BUFFER_USAGE_INDIRECT_COMMAND_BIT` flag an indirect-command buffer
needs — see `COMPUTE_PHASE1_COMPLETION_REPORT.md`.

### C.3 — No GPU-side culling of any kind
`RenderSystem::CollectRenderables()` (`RenderSystem.cpp`) walks every
`MeshRenderer` in the `Registry` unconditionally, every frame, and submits
every one of them — no frustum culling, no occlusion culling, not even a
CPU-side bounding-volume check. For a scene with a meaningful entity count
this is the actual bottleneck GPU-driven rendering exists to remove — and
right now there is nothing to accelerate at all, CPU or GPU. **Still fully
open** — this is `GPU_DRIVEN_RENDERING_COMPUTE_INDIRECT_STRATEGY_v1.md`'s
own Phase D (`Shaders/FrustumCull.comp` + real pass declaration), unaffected
by the compute-shader campaign landing its own, unrelated box-blur
validation workload.

### C.4 — No bindless / descriptor-indexing infrastructure
`GpuResourceFactory::MaterialDescriptorSetLayout()` is one fixed,
single-combined-image-sampler descriptor set layout, and every
`MaterialTexture` gets its OWN `VkDescriptorSet` allocated from a shared
pool (`GpuResourceFactory.cpp`'s `kMaxMaterialTextures = 4096` descriptor
pool). A real GPU-driven pipeline (where a compute shader decides which
draws happen and a shader indexes into "whichever material this instance
needs" without a CPU-side `vkCmdBindDescriptorSets` per draw) generally
wants `VK_EXT_descriptor_indexing`/bindless arrays instead — binding one
enormous descriptor set once and letting shaders index into it by a
per-instance integer. Not strictly required for a first GPU-driven-culling
milestone (a single-material batch doesn't need it), but it is the natural
next wall this work will hit the moment two differently-textured meshes
need to be culled/drawn through the same indirect buffer. **Still fully
open, unaffected by the compute-shader campaign** — that campaign
deliberately reused `MaterialTexture`'s existing one-descriptor-set-per-
resource-combination precedent (see
`COMPUTE_SHADER_FEATURES_DELIBERATELY_NOT_IMPLEMENTED.md`, Section A.2)
rather than building bindless infrastructure.

### C.5 — No GPU (compute) skinning; CPU skinning re-uploads every frame
`Game::UpdateSkeletalAnimators()`/`Animation/VertexSkinning.h`'s
`SkinVertices()` does all bone-blending on the CPU, then re-uploads the
ENTIRE vertex buffer via `Mesh::UpdateVertexData()` every frame for every
animated model (`Renderer::CreateSkinnedMesh()`'s host-visible,
persistently-mapped vertex buffer). This is explicitly flagged in
`TODO.md`'s own Phase 9 discussion (`RENDERGRAPH_PHASE9_...`'s own "compute
passes" entry names "GPU vertex skinning... the obvious first candidate"
for the first real compute workload). Directly relevant: this is the
single most concrete, already-existing, already-needed compute workload
this engine has — a natural second validation target for the GPU-driven
milestone, after culling. **Still not implemented, but genuinely more
approachable now**: `ComputePipeline`/`DescriptorSetLayoutBuilder`/
`Renderer::Dispatch()`/`RWStructuredBuffer` support (all real, shipped
infrastructure — see `COMPUTE_SHADER_MASTER_STRATEGY_v2.md`) are exactly
the building blocks a GPU-skinning compute shader would need (per-vertex
position/normal `StructuredBuffer` in, skinned `RWStructuredBuffer` out).
Still explicitly named as the next candidate to pick up only once BOTH
halves of the compute-shader campaign (buffer + texture) are closed — see
`COMPUTE_SHADER_FEATURES_DELIBERATELY_NOT_IMPLEMENTED.md`, Section C.5.

### C.6 — No physics/collision engine at all
`Assets/PhysicsData.h` already extracts PMX rigid-body/joint DATA on
import, but there is no simulation backend (Bullet or otherwise) anywhere
in this codebase, and no AABB/sphere collider primitives either (`TODO.md`
calls this out explicitly as a prerequisite for click-to-select
raycasting). Not directly a GPU-driven-rendering blocker, but a real,
still-open "modern engine" gap worth tracking alongside it.

### C.7 — No scene serialization
`Game.cpp`'s demo entities remain hardcoded in C++; there is no
`Registry` ↔ text-format (JSON or otherwise) read/write path at all.
`TODO.md` calls this "likely the single highest-leverage next
engine-level feature" for the engine broadly (unrelated to rendering
specifically, but blocking Phase 9 backlog item 8 above).

### C.8 — No Tier-2 (GPU-backed) automated test coverage
`Buffer`/`RenderTexture`/`Pipeline`/`GpuResourceFactory`/every
`Renderer/Vulkan/` class and now every GPU-touching Render Graph class
(`RenderGraphResourcePool`, `RenderGraph` itself) have zero automated
tests — verified manually only (see `TESTING.md`'s own "Tier 2" note). A
`GTE_BUILD_GPU_TESTS`-gated headless (`VK_EXT_headless_surface`) fixture
is a named, accepted backlog item, never attempted. This matters more once
compute shaders/indirect buffers land, since a subtle compute-dispatch or
indirect-buffer-layout bug is exactly the kind of thing that currently has
no automated regression net at all.

### C.9 — No `VkAllocationCallbacks` host-memory hook
Named in `TODO.md`'s own "Memory Profiler" section — every Vulkan
`vkCreate*`/`vkDestroy*` call across the whole engine would need to
consistently pass the same custom allocator callback struct, a real,
multi-file refactor, currently not justified by any observed CPU-memory
blind spot. Tangential to GPU-driven rendering, but relevant the moment
a compute pipeline/pool of indirect-draw buffers adds a new class of
allocation this hook would also need to cover consistently.

### C.10 — `vmaBuildStatsString()` per-block detail parser
Also named in `TODO.md` — the "Memory" panel currently only shows
aggregate per-heap VMA statistics, not a per-block/per-allocation
breakdown. Not a blocker, but worth revisiting once GPU-driven rendering
introduces a handful of new persistent GPU buffers (indirect-draw command
buffers, draw-count buffers, culling input SSBOs) whose individual block
placement becomes something worth debugging.

---

## Section D — Prioritized reading, specifically for the GPU-driven-rendering milestone

**UPDATE: items 1, 2, 3 (partial), and 5 below are now DONE** — the
compute-shader campaign (`COMPUTE_SHADER_MASTER_STRATEGY_v2.md`, Phases
1-7) shipped the texture-side half of this whole plan, and a separate,
dedicated effort (`B1_REAL_GPU_TIMING_STRATEGY_v1.md`) closed item 5. See
`COMPUTE_SHADER_FEATURES_DELIBERATELY_NOT_IMPLEMENTED.md` for the full
breakdown of what shipped vs. what remains. The list below is kept in its
original form for historical context, with each item's current status
called out inline.

Given the stated next milestone (add + integrate compute shaders and
indirect buffers into the render graph), here is the dependency order that
actually matters, cross-referencing the sections above:

1. **C.1 (compute shader capability)** — ✅ **DONE**, see C.1 above — **and
   C.2 (indirect draw support)** — ⏸ **STILL OPEN** — were the two hard,
   load-bearing prerequisites. Only C.2 remains; see the companion
   strategy document's own "Step 0: Status Update" for the full phased
   plan starting from its Phase C.
2. **A.3 (compute passes as first-class graph citizens)** — ✅ **DONE**,
   see A.3 above — is the render-graph-side integration of C.1/C.2:
   `ResourceAccess` grew the new enumerators, `PassBuilder` gained
   `AddComputePass()`/`WriteTexture()`, and the barrier planner learned
   `VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT` and
   `VK_ACCESS_2_INDIRECT_COMMAND_READ_BIT` — all real, shipped, tested code
   today (`COMPUTE_PHASE5_COMPLETION_REPORT.md`/`COMPUTE_PHASE6_COMPLETION_REPORT.md`).
3. **B.2 (a real cross-pass READ, never yet exercised)** — ✅ **DONE for
   textures**, ⏸ **STILL OPEN for buffers** — see B.2 above. The
   texture-side cross-pass read is now proven by the compute box-blur
   pass reading the Scene view's own `RenderTexture`. The buffer-side
   equivalent (a culling compute pass's indirect-draw buffer, read by a
   later graphics pass's indirect draw call) is what remains — this
   milestone is STILL what will finally prove out that half of the render
   graph's own core promise.
4. **C.3 (no GPU culling exists)** — ⏸ **STILL OPEN** — is the actual
   FEATURE this milestone delivers — not a prerequisite, the payoff.
5. **B.1 (GPU timing not wired up)** — ✅ **DONE**, closed independently
   by `B1_REAL_GPU_TIMING_COMPLETION_REPORT.md` — every `RenderGraph` pass,
   including a future compute culling pass and its indirect-draw consumer,
   already gets real, driver-measured GPU milliseconds automatically, with
   no further plumbing needed.
6. **A.2 (async compute)** and **C.4 (bindless descriptors)** — both
   ⏸ **STILL OPEN, but now genuinely more actionable** — are the natural,
   real "what's next" once the buffer-side GPU-driven culling pass (C.2/
   C.3) is shipped and proven — exactly matching Phase 9's own "never
   speculatively, only once a real, concrete need appears" discipline.
7. Everything else in Sections A/B/C is either orthogonal (physics, scene
   serialization, audio) or a pure follow-on optimization (barrier
   batching, resource aliasing, stale pool eviction) that should stay
   exactly as deferred as it already is.
