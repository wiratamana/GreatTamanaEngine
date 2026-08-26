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
| 2 | **Async compute / multi-queue submission** | Zero compute shaders exist anywhere in the engine yet — nothing to parallelize | **Directly unblocked by the GPU-driven-rendering milestone** — once real compute passes exist and run correctly on the graphics queue, moving one to a dedicated compute queue becomes a real, measurable optimization instead of speculative machinery |
| 3 | **Compute passes as first-class graph citizens** | No compute shader exists anywhere in this engine yet; `ResourceAccess` (Phase 1) was deliberately scoped to graphics-only access kinds | **This is the GPU-driven-rendering milestone itself** — see the companion strategy document |
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
2. **The campaign's own single most novel capability — a genuine
   cross-pass texture READ, synthesized into a real barrier — has never
   been exercised by a real, shipped pass.** None of Game view/Scene
   view/Present ever declares a `ReadTexture()` of another pass's output;
   Dear ImGui samples the Game/Scene `RenderTexture`s entirely outside the
   graph's own resource model (`FinalizeRenderTextureForExternalSampling()`
   is a manual workaround for exactly this gap — see `RenderPasses.cpp`).
   The already-scoped validation candidate (a Scene-view outline-highlight
   post-process, `TODO.md`) remains unimplemented. **This matters directly
   for GPU-driven rendering**: a GPU culling compute pass writing an
   indirect-draw buffer that a LATER graphics pass reads is exactly this
   same "pass B consumes pass A's output through the graph" pattern,
   applied to a buffer instead of a texture — the read side of Phase
   2/5/6's machinery (`ReadBuffer()`, `RequiredStateFor()` for a storage
   buffer) has never been exercised against a real workload either.
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

### C.1 — No compute shader capability anywhere in the engine
Confirmed by direct inspection: `Pipeline.h/.cpp` only ever builds a
`VK_PIPELINE_BIND_POINT_GRAPHICS` pipeline (vertex + fragment stage,
`VkPipelineRenderingCreateInfo` for dynamic rendering); `GpuResourceFactory`
has no `CreateComputePipeline()`; `RenderGraphTypes.h`'s `ResourceAccess`
enum has no compute-specific value (`ShaderReadWrite`/`ShaderWrite` for a
storage image/buffer, as Phase 9 item 3 above already names); `Shaders/`
contains only `.vert`/`.frag` pairs, zero `.comp` files;
`cmake/CompileShaders.cmake`'s `gte_add_shader()` has never been called with
a compute shader. This is the load-bearing gap for the whole next
milestone — see the companion strategy document.

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
calls, and neither half exists today.

### C.3 — No GPU-side culling of any kind
`RenderSystem::CollectRenderables()` (`RenderSystem.cpp`) walks every
`MeshRenderer` in the `Registry` unconditionally, every frame, and submits
every one of them — no frustum culling, no occlusion culling, not even a
CPU-side bounding-volume check. For a scene with a meaningful entity count
this is the actual bottleneck GPU-driven rendering exists to remove — and
right now there is nothing to accelerate at all, CPU or GPU.

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
need to be culled/drawn through the same indirect buffer.

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
milestone, after culling.

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

Given the stated next milestone (add + integrate compute shaders and
indirect buffers into the render graph), here is the dependency order that
actually matters, cross-referencing the sections above:

1. **C.1 (compute shader capability)** and **C.2 (indirect draw support)**
   are the two hard, load-bearing prerequisites — nothing else in this
   list can be built before these two exist. See the companion strategy
   document for the full phased plan.
2. **A.3 (compute passes as first-class graph citizens)** is the render-graph-side
   integration of C.1/C.2 — this is where `ResourceAccess` grows new
   enumerators, `PassBuilder` gains `AddComputePass()`, and Phase 5's
   barrier planner learns `VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT` and
   `VK_ACCESS_2_INDIRECT_COMMAND_READ_BIT`.
3. **B.2 (a real cross-pass READ, never yet exercised)** gets its FIRST
   real, production exercise the moment a culling compute pass's output
   buffer is read by a later graphics pass's indirect draw call — this
   milestone is what finally proves out the render graph's own core
   promise, arguably more convincingly than the originally-planned
   outline-highlight texture-read validation ever would have.
4. **C.3 (no GPU culling exists)** is the actual FEATURE this milestone
   delivers — not a prerequisite, the payoff.
5. **B.1 (GPU timing not wired up)** should be picked up alongside this
   milestone, not after it — you cannot honestly evaluate whether
   GPU-driven culling is actually faster without real, per-pass GPU
   timing for the new compute pass AND the indirect draw pass it feeds.
6. **A.2 (async compute)** and **C.4 (bindless descriptors)** are the
   natural, real "what's next" once a first synchronous, single-queue,
   single-material GPU-driven culling pass is shipped and proven —
   exactly matching Phase 9's own "never speculatively, only once a real,
   concrete need appears" discipline.
7. Everything else in Sections A/B/C is either orthogonal (physics, scene
   serialization, audio) or a pure follow-on optimization (barrier
   batching, resource aliasing, stale pool eviction) that should stay
   exactly as deferred as it already is.
