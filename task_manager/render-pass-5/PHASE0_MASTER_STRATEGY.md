# PHASE0 — Master Strategy: GPU-Driven Frustum Culling + Indirect Draw (`render-pass-5`)

This document is the **orchestrator**. It does not itself contain implementation
steps — it defines the goal, the current situation, the locked design
decisions, and the map of child phase documents that carry out the actual code
changes, in order. Every child phase document follows the same three-step
shape (Goal / Situation / Plan) and must be executed in numeric order, since
each phase's code depends on the previous one existing and compiling.

Source material for this whole campaign (read these before starting, they are
the accumulated planning record this campaign supersedes/completes):
- `task_manager/GPU_DRIVEN_RENDERING_COMPUTE_INDIRECT_STRATEGY_v1.md` — the
  original design doc. Its Phase A/B (compute pipeline infra, `ResourceAccess`
  extension) are **already shipped** by the separate compute-shader campaign —
  see its own "Step 0: Status Update". Its Phase C onward is what this
  campaign (`render-pass-5`) actually finishes, restructured into 7 phases
  below with several concrete design decisions this doc left open now locked.
- `task_manager/RENDERGRAPH_FUTURE_TODO_DELIBERATELY_NOT_IMPLEMENTED.md` and
  `task_manager/COMPUTE_SHADER_FEATURES_DELIBERATELY_NOT_IMPLEMENTED.md` — for
  the full "what's already reusable vs. what's still open" record.
- `task_manager/gpu_skinning/` (all 7 phases + completion reports) — **the
  single closest precedent in this codebase.** GPU Vertex Skinning already
  solved "a compute pass writes a buffer, a later graphics pass reads that
  exact buffer, cross-pass, barrier-synchronized by the render graph" for a
  vertex buffer. This campaign solves the same shape for an INDIRECT COMMAND
  buffer. Reuse its patterns (`GpuSkinningPipelines`, `AddGpuSkinningPasses()`,
  `DeclareGpuSkinningReads()`, `ResourceAccess::VertexBufferRead`) as the
  literal template wherever this campaign's own plan says "mirror GPU
  Skinning's own X".

Read this file first. Then execute, in order:

- `PHASE1_FOUNDATIONS_BOUNDS_INDIRECT_TYPES_AND_VOCABULARY.md`
- `PHASE2_INSTANCED_DRAW_PRIMITIVE_AND_INDIRECT_SUBMIT.md`
- `PHASE3_FRUSTUM_CULL_COMPUTE_SHADER_AND_PIPELINE.md`
- `PHASE4_PER_BATCH_RESOURCE_MANAGEMENT_AND_BATCHING.md`
- `PHASE5_RENDERGRAPH_PASS_WIRING_AND_PRODUCTION_CUTOVER.md`
- `PHASE6_EDITOR_TOOLING_AND_LIVE_VALIDATION.md`
- `PHASE7_DOCS_FULL_BUILD_AND_CAMPAIGN_COMPLETION.md`

Always re-read the previous phase's own completion report (each phase's
working agreement, mirrored from every other campaign in this repository, is
to write a short `PHASEn_COMPLETION_REPORT.md` next to this file once that
phase's code compiles) before starting the next one — it may record a
decision or a snag that changes a later phase's exact plan.

**Every implementation phase, and anything it further delegates, must use the
`ask_questions` tool whenever it hits a genuine ambiguity or a design choice
this document doesn't already pin down.** This rule propagates recursively: if
an implementation phase itself delegates a sub-task, that delegation prompt
must repeat this same instruction, verbatim, to whatever it delegates to.

**Two of these phases are flagged as this campaign's highest-risk work
(PHASE3 and PHASE5 — see the Phase Map below) and each get their OWN dedicated
`delegate_task` double-check pass immediately after they land, BEFORE the
single, whole-campaign second-iteration double-check that reviews every `.md`
file together.** Do not fold those two dedicated checks into the general
second-iteration review — they happen first, individually, exactly as scoped
in this document's own orchestration instructions (see the top-level task that
created this document).

---

## Step 1: The Goal (Where are we going?)

Replace this engine's current "walk every `MeshRenderer` in the `Registry` and
issue one `vkCmdDrawIndexed` per entity, every frame, unconditionally — no
frustum culling, no bounding-volume check of any kind" behavior
(`RenderSystem::CollectRenderables()`/`RenderSystem::Draw()`,
`src/Game/RenderSystem.cpp`) with a real, GPU-driven path for the common case
where **several entities share the exact same `MeshHandle` + `PipelineHandle`**
(a "batch"):

1. A real compute shader (`Shaders/FrustumCull.comp`) reads one
   `GpuCullingInstanceInput` per instance (world matrix + local AABB, packed
   once per frame from live ECS `Transform`/`Mesh` data), tests it against the
   active camera's 6 frustum planes, and writes exactly one
   `VkDrawIndexedIndirectCommand` for every surviving instance into a
   render-graph-owned indirect-command buffer, plus an atomic visible count
   into a companion count buffer.
2. A graphics pass reads that SAME buffer — a real
   `RenderGraphBuilder::PassBuilder::ReadBuffer(handle,
   ResourceAccess::IndirectCommandRead)`, synchronized automatically by the
   EXISTING, unmodified `RenderGraphBarrierPlanner` — and issues **exactly
   one** `vkCmdDrawIndexedIndirectCount` (or, on a device without
   `drawIndirectCount`, exactly one `vkCmdDrawIndexedIndirect` against a
   full-width, degenerate-padded command array) instead of N individual
   `Renderer::Submit()` calls.
3. Each surviving instance's own MODEL MATRIX is read by the VERTEX SHADER
   itself, from the SAME per-instance input buffer the culling shader read
   from, indexed by `gl_InstanceIndex` (which — because every emitted command
   has `instanceCount == 1` and `firstInstance == this instance's own index in
   the (uncompacted) input array` — always resolves to exactly the right row).
   This is what makes ONE indirect draw call able to render N differently-
   positioned objects: this engine's existing push-constant-model-matrix
   convention (`Pipeline.h`'s 128-byte `model`+`viewProj` push constant) is
   physically incapable of this (one push constant per `vkCmdDraw*` call, not
   per instance) — a genuinely new, additive vertex-shader/pipeline variant is
   required (see PHASE2).
4. Any `MeshRenderer` NOT part of a big-enough batch (below a tunable instance
   threshold), or whose vertex layout/animation state makes it ineligible (see
   Locked Design Decisions below), keeps drawing through the EXACT, unmodified,
   existing per-entity `Renderer::Submit()` path — a strangler-fig migration,
   zero regression to anything not opted in structurally.
5. This is simultaneously the campaign's payoff (a real performance
   optimization for any scene with a meaningful repeated-mesh entity count)
   AND the proof, for the first time, that this engine's render graph's own
   single most novel, most-advertised capability — a compute pass's buffer
   WRITE consumed by a later pass's buffer READ, synchronized automatically —
   holds for a buffer, not just a texture (the texture-side equivalent was
   already proven by `ComputeBlurValidation` in the separate compute-shader
   campaign).

This is deliberately scoped as ONE validated, PRODUCTION feature (batched
entities sharing a mesh+pipeline get real GPU-driven culling+indirect drawing,
today, unconditionally, whenever the batch is big enough) — not a generic
"instancing framework" built speculatively beyond what this one feature needs.

## Step 2: The Situation (Where are we now?)

Confirmed directly against the current source (2026-09-22):

- **Already shipped, reusable as-is, requires ZERO changes this campaign:**
  - `ComputePipeline` (`src/Renderer/ComputePipeline.h/.cpp`), the shared
    SPIR-V loader, `DescriptorSetLayoutBuilder`
    (`src/Renderer/Vulkan/DescriptorSetLayoutBuilder.h`), `ComputeDescriptorSet`
    (`src/Renderer/ComputeDescriptorSet.h/.cpp`),
    `GpuResourceFactory::AllocateComputeDescriptorSet()`/
    `CreateComputePipeline()`, `Renderer::Dispatch()` +
    `ComputeGroupCount()`/`ComputeGroupCount3D()`
    (`src/Renderer/ComputeDispatch.h`).
  - `RenderGraphTypes.h`'s `ResourceAccess` enum already has
    `ComputeShaderRead`/`ComputeShaderWrite`/`IndirectCommandRead`/
    `VertexBufferRead`, each with full `IsWriteAccess()`/`ToString()`/
    `RequiredStateFor()` handling (`RenderGraphTypes.cpp`/
    `RenderGraphBarrierPlanner.cpp`) — `IndirectCommandRead` in particular has
    a `RequiredStateForIndirectCommandRead` unit test but **no real consumer
    anywhere**, and no hand-simulated two-state barrier-transition regression
    test (unlike the texture-side
    `ComputeShaderWriteFollowedByShaderReadEmitsExactlyOneCorrectBarrier` test
    that already exists) — PHASE1 closes that gap.
  - `RenderGraphBuilder::PassBuilder::ReadBuffer()`/`WriteBuffer()`/
    `AddComputePass()`/`ImportBuffer()` (`RenderGraphBuilder.h`) — real,
    shipped, and already exercised end-to-end by GPU Vertex Skinning's own
    `AddGpuSkinningPasses()` (`src/Application/RenderPasses.cpp`, line ~392) —
    the exact template this campaign's PHASE5 copies.
  - `GpuResourceFactory::CreateStructuredBuffer()`/
    `Renderer::CreateStructuredBuffer()` already accept an `extraUsage`
    parameter specifically for `VK_BUFFER_USAGE_INDIRECT_COMMAND_BIT` — no new
    buffer-factory method needed.
- **Confirmed genuinely absent, and squarely this campaign's own job:**
  - No bounding-volume data anywhere in the ECS or `Mesh` (`Transform`/
    `MeshRenderer` carry no bounds field; `Mesh.h` carries no local AABB).
  - No `VkDrawIndexedIndirectCommand` mirror struct, no per-instance
    culling-input struct, anywhere in this codebase.
  - No `Renderer::SubmitIndirect()` / `vkCmdDrawIndexedIndirect(Count)` call
    site anywhere — every draw goes through `FrameRecorder::IssueDrawCommand()`
    (`vkCmdDraw`/`vkCmdDrawIndexed`, CPU-known instance count of exactly 1 —
    see `DrawStats.h`'s own documented "no instancing exists anywhere in this
    engine yet" assumption, which this campaign is the first to break).
  - No device-capability probe for `VkPhysicalDeviceVulkan12Features::
    drawIndirectCount` — `VulkanDevice::CreateLogicalDevice()`
    (`src/Renderer/Vulkan/VulkanDevice.cpp`, line ~187) only chains a
    `VkPhysicalDeviceVulkan13Features` struct (`dynamicRendering`/
    `synchronization2`) today; no `VkPhysicalDeviceVulkan12Features` chain
    link exists at all.
  - **No way for the vertex shader to source a PER-INSTANCE model matrix at
    all.** `Pipeline.h`'s ENTIRE push-constant convention (128 bytes,
    `model` then `viewProj`, shared by `VertexLayout::PositionColor`/
    `PositionNormal`/`PositionNormalUv`) is fundamentally one-draw-one-object.
    This is a REAL, necessary new addition this campaign's own planning doc
    (`GPU_DRIVEN_RENDERING_COMPUTE_INDIRECT_STRATEGY_v1.md`) never explicitly
    named — confirmed by this analysis, not merely assumed — see PHASE2.
  - `RenderSystem::CollectRenderables()`/`Draw()` (`src/Game/RenderSystem.h/
    .cpp`) has no grouping/batching concept at all — one `DrawCommand` per
    entity, one `Renderer::Submit()` call per `DrawCommand`, always.
  - `DrawStats.h`'s `AccumulateDrawStats()` has no "unknown/indirect" bucket —
    every counted draw assumes a CPU-known `vertexCount`/`indexCount`.

## Step 3: The Plan (How do we get there?)

### Locked Design Decisions

Confirmed via `ask_questions` with the project owner before writing any child
phase document. MUST NOT be silently changed by a later phase without
updating this file first:

1. **Strategy `.md` files live in `task_manager/render-pass-5/`** (the
   already-given, empty folder for this campaign).
2. **Full production integration, not a debug-only validation pass.** Unlike
   `mrt-1`'s `GBufferValidation`/the compute-shader campaign's
   `ComputeBlurValidation`, this campaign's mechanism is **always-on,
   unconditional production code**: any time `RenderSystem` finds a group of
   entities sharing an identical `(MeshHandle, PipelineHandle)` pair whose
   count is at least `kMinInstancesForGpuDrivenBatch` (PHASE4), that group is
   automatically rendered through the new compute-cull + indirect-draw path,
   every frame, with no toggle. This is the accepted, bigger-payoff,
   bigger-scope option (confirmed via `ask_questions`) over the smaller,
   debug-only-validation-pass alternative every prior render-graph-mechanism
   campaign in this repo used.
3. **Bounding volume: AABB (min/max), local-space, computed ONCE per `Mesh` at
   load time** (confirmed via `ask_questions` over a bounding sphere) — mirrors
   `GpuSkinningTypes.h`'s own "computed once, at model-load time" convention.
   Lives as a new, optional field on `Mesh` itself (`src/Renderer/Mesh.h`),
   NOT on the `MeshRenderer` ECS component — a bound describes the GEOMETRY,
   which is shared by every instance of that mesh, not a per-entity fact.
4. **Both the real (`vkCmdDrawIndexedIndirectCount`) and the fallback
   (`vkCmdDrawIndexedIndirect` with a fixed, degenerate-padded command count)
   code paths must be built and reviewed, even though only one of them is
   exercisable on any one given development machine.** Confirmed via
   `ask_questions`. **The strategy documents below, and all code they produce,
   must never mention or depend on which specific path any particular
   development or CI machine happens to support** — the choice is made
   EXCLUSIVELY by a runtime capability probe
   (`VulkanDevice`/`Renderer::SupportsDrawIndirectCount()`, PHASE2), queried
   once at startup and never assumed either way. Whichever branch this
   engineer's own machine cannot exercise must still compile, still be
   exercised by a Tier-1 test of its own pure decision logic, and be reviewed
   as carefully as the branch that IS exercisable locally.
5. **New `ResourceAccess::VertexShaderStorageRead` enumerator** (PHASE1) — a
   StructuredBuffer read by the VERTEX SHADER stage
   (`VK_PIPELINE_STAGE_2_VERTEX_SHADER_BIT` /
   `VK_ACCESS_2_SHADER_STORAGE_READ_BIT`), distinct from the existing
   `ShaderRead` (fragment-stage sampling only — confirmed by reading
   `RequiredStateFor()`), `ComputeShaderRead` (compute stage), and
   `VertexBufferRead` (the fixed-function vertex-INPUT-ASSEMBLER's read of a
   real bound vertex buffer, not a shader's own storage-buffer read). This is
   what the new instanced graphics pass declares against the per-instance
   input buffer it reads via `gl_InstanceIndex` (see decision 7 below) —
   without it, the ONLY existing option (`ShaderRead`, fragment-stage-only)
   would produce an incorrect barrier that does not actually synchronize
   against a vertex-stage read, a real, silent GPU hazard.
6. **Two new `VertexLayout` enumerators are explicitly REFUSED this
   campaign**: no `PositionColorInstanced`/`InstancedTriangle.vert` variant
   for built-in primitive shapes. `PrimitiveMeshGenerator`'s shapes are
   NON-INDEXED (`Mesh.h`'s own comment: "every `PrimitiveMeshGenerator`-built
   shape still uses this [non-indexed constructor]"), and this campaign's own
   indirect-draw mechanism is `VkDrawIndexedIndirectCommand`-only (indexed
   draws only — matches the original design doc's own explicit "no
   `vkCmdDrawIndirect` (non-indexed) support" refusal). Only ONE new
   enumerator is added: `VertexLayout::PositionNormalInstanced` (PHASE2),
   paired with a new `Shaders/MeshInstanced.vert` reusing the EXISTING
   `Mesh.frag` unmodified. Primitive-shape batching (which would need either a
   non-indexed indirect path or giving `PrimitiveMeshGenerator` real index
   buffers) is a named, explicit follow-up, NOT this campaign's job.
7. **Batching eligibility (`kMinInstancesForGpuDrivenBatch`, PHASE4) — a
   `(MeshHandle, PipelineHandle)` group is GPU-driven-eligible if and ONLY
   if:** (a) its instance count is `>= kMinInstancesForGpuDrivenBatch` (a
   named, tunable `constexpr`, default `4`); (b) its `Mesh::HasIndexBuffer()`
   is true; (c) its `Pipeline` was built with exactly
   `VertexLayout::PositionNormal` (untextured, non-instanced — PHASE4/5
   resolve such a group's `Pipeline`/`Mesh` pair onto the NEW
   `PositionNormalInstanced` pipeline/shader instead, never mutating the
   original); (d) the mesh is NOT part of this frame's GPU-skinning output-buffer
   set (`Game::CollectGpuSkinningDispatchRequests()`) — a GPU-skinned model's
   own per-frame-varying vertex buffer/pose is fundamentally per-entity, not
   shareable across "instances" in the sense this campaign means. **Every
   OTHER `MeshRenderer` — including every `VertexLayout::PositionColor`
   primitive and every `VertexLayout::PositionNormalUv` textured submesh, and
   every group below the threshold — keeps drawing through the existing,
   byte-for-byte-unchanged per-entity `Renderer::Submit()` path in
   `RenderSystem::Draw()`.**
8. **Textured (`PositionNormalUv`) batches are explicitly out of scope.** A
   textured indirect batch would need either bindless/descriptor-indexing (so
   different instances in ONE indirect call can sample different
   `MaterialTexture`s) or a same-texture-only sub-batch split — both are real,
   separate design problems, named as follow-up work, never attempted here.
9. **Only PHASE7 runs a full build + full `ctest` pass.** PHASE1–6 do a fast,
   targeted incremental compile check only — never a full regression run, to
   keep iteration fast on this machine (this project's established working
   agreement — see `task_manager/mrt-1/PHASE0_MASTER_STRATEGY.md`'s identical
   rule).
10. **PHASE3 (the real compute shader + pipeline plumbing) and PHASE5 (the
    real render-graph pass wiring + production `RenderSystem::Draw()` cutover)
    are this campaign's two highest-risk phases** — PHASE3 because it is new,
    correctness-critical GLSL (a wrong culling test either pops geometry that
    should be visible, or draws nothing at all — no compiler catches this) and
    PHASE5 because it changes the engine's actual, currently-100%-reliable,
    production hot rendering path. **Each gets its own dedicated
    `delegate_task` double-check pass, immediately after it lands, before the
    single whole-campaign second-iteration double-check.**

11. **This campaign's entire GPU-driven cutover is GAME VIEW ONLY** (confirmed
    via `ask_questions` during PHASE5's own dedicated pre-implementation
    double-check — see `PHASE5_STRATEGY_DOUBLE_CHECK_REPORT.md`, this same
    folder). Scene View (`RenderViewId::Named("Scene")`) and the rare
    direct-render-to-swapchain fallback (`AddPresentPass()`'s own fallback
    branch, reachable only when both Game and Scene Editor panels are hidden)
    both keep rendering EVERY entity — including every batch-eligible one —
    through the fully unmodified, existing per-entity `Renderer::Submit()`
    path, FOREVER. Only the Game View's own `"RenderOpaque"` provider branch
    ever receives PHASE5's new `Draw()` batched-entity exclusion parameter,
    and only the Game View ever gets the new compute-cull + indirect-draw
    passes declared against it (PHASE5's own new `"GpuDrivenBatches"` provider
    early-returns for any non-Game view). **This is exactly why PHASE4's own
    per-batch resource cache is safely keyed by `(MeshHandle, PipelineHandle)`
    alone, with no view dimension** — only one view (Game) ever consumes it,
    so it can never be asked to serve two different cameras' worth of culling
    results for the same batch at once.

### Non-Goals (explicitly out of scope for `render-pass-5`)

- **No occlusion culling** (frustum culling only).
- **No hierarchical/two-phase (occlusion-aware) culling.**
- **No LOD selection** inside the culling shader.
- **No bindless/descriptor-indexing infrastructure**, and therefore **no
  multi-material indirect batch** — one pipeline/one untextured mesh per
  batch, exactly matching how `RenderSystem::Draw()` already submits
  per-pipeline today.
- **No async compute / dedicated compute queue** — stays on the single
  existing graphics queue, exactly like GPU Vertex Skinning and the
  compute-shader campaign's own box-blur validation.
- **No primitive-shape (`VertexLayout::PositionColor`) batching** — see Locked
  Design Decision 6.
- **No GPU-skinned-mesh batching** — see Locked Design Decision 7(d).
- **No removal of the existing per-entity `Renderer::Submit()` path** — it
  remains correct and necessary for every entity/group that doesn't qualify
  for batching (Locked Design Decision 7).
- **No change to `RenderGraphCompiler.cpp`/`RenderGraphSnapshot.cpp`** — both
  already generalize correctly over an arbitrary buffer read/write, proven by
  GPU Vertex Skinning's own real, shipped `ReadBuffer(..., VertexBufferRead)`
  consumer; if a phase's own investigation ever finds this false for some real
  edge case, STOP and use `ask_questions` before changing either file.
- **No Editor-authored/inspector-editable bounds override UI** — bounds are
  always computed automatically from mesh geometry (Locked Design Decision 3).

### Phase Map

| Phase | Deliverable |
|---|---|
| **1** | `Math`/`Renderer/Culling` foundations: local-space `AABB` type, `IndirectDrawCommand` (mirrors `VkDrawIndexedIndirectCommand`), `GpuCullingInstanceInput` (std430-safe packed layout), pure frustum-plane-extraction + AABB-vs-frustum Tier-1-tested math, `ResourceAccess::VertexShaderStorageRead` (+ full exhaustive-switch/barrier-planner wiring + regression tests), and the still-missing `ComputeShaderWrite -> IndirectCommandRead` hand-simulated barrier regression test. Zero behavior change to anything existing. |
| **2** | The new instanced-draw graphics primitive: `VertexLayout::PositionNormalInstanced` + `Shaders/MeshInstanced.vert` (reuses `Mesh.frag`), `Renderer::SubmitIndirect()`, `VulkanDevice`'s `drawIndirectCount` capability probe (both branches, Locked Design Decision 4), `DrawStats`'s new "unknown/indirect" bucket. No render-graph/ECS wiring yet — a hand-driven throwaway smoke test only, mirroring the compute-shader campaign's own Phase 2 discipline. |
| **3** | ⚠️ **Dedicated double-check phase.** The real `Shaders/FrustumCull.comp` culling shader (both compaction-mode and degenerate-padding-mode branches, push-constant-selected) + `CullingPipelines` (mirrors `GpuSkinningPipelines` exactly) + descriptor-set-layout plumbing. |
| **4** | `RenderSystem`'s pure, Tier-1-testable batching/grouping logic (group `DrawCommand`s by `(MeshHandle, PipelineHandle)`, apply Locked Design Decision 7's eligibility rule) + the per-batch persistent GPU-resource cache (culling-input SSBO, indirect-command buffer, count buffer — mirrors GPU Skinning Phase 4's per-model resource cache precedent) + per-frame instance-data packing from live `Transform`/`Mesh` bounds. |
| **5** | ⚠️ **Dedicated double-check phase.** The real render-graph pass wiring: a small `"<batch> ResetCount"` pass + a compute culling pass + an indirect graphics pass declared per eligible batch, registered as a NEW `"GpuDrivenBatches"` `rg::RenderPassProvider` (`Application::RegisterOffscreenRenderPipelineProviders()`) — placed textually between `"RenderOpaque"` and `"DrawSkyBackground"`'s own `Register()` calls — never a free function called adjacent to `AddGpuSkinningPasses()` (that call site is only the rare direct-render-to-swapchain fallback, not this engine's real per-frame Game View path; see Locked Design Decision 11). This is declared alongside (never replacing) the untouched per-entity path for non-batched draws AND for Scene View/the fallback (Locked Design Decision 11) — the actual production cutover, Game View only. |
| **6** | Editor tooling ("instances culled this frame" readout) + a real, meaningful-instance-count live scene/spawn mechanism for manual/HTTP-driven validation + visual/regression comparison against the pre-cutover per-entity rendering. |
| **7** | `AGENTS.md`/`README.md`/`docs/conventions/` updates; final Tier-1 test sweep; full `cmake --build build`; full `ctest`; a live, running-engine, HTTP-driven, screenshot-verified smoke test; `CAMPAIGN_COMPLETION_REPORT.md`. |

### Definition of Done for the whole campaign

- `cmake --build build` succeeds (default configuration, `GTE_ENABLE_EDITOR`
  ON).
- `ctest` passes, including every new test file/case this campaign adds, with
  zero regressions against the baseline count recorded at the start of PHASE7.
- A live scene containing a `(MeshHandle, PipelineHandle)` group at or above
  `kMinInstancesForGpuDrivenBatch` renders through exactly one compute culling
  pass + one indirect draw call per such group, visually IDENTICAL to how it
  rendered before this campaign (pure performance/mechanism change, zero
  rendering-behavior change) — confirmed via `GET /get_game_view`/
  `GET /get_swapchain` before/after comparison.
- Rotating the camera so part of such a group leaves the frustum measurably
  reduces that batch's own indirect draw count (confirmed via the Editor's
  "Render Graph" panel / the new "instances culled this frame" readout from
  PHASE6) — the actual, visible proof culling is happening.
- Validation layers report zero new warnings/errors.
- Every entity/group NOT eligible for batching (primitives, textured
  submeshes, GPU-skinned models, small groups) renders exactly as it did
  before this campaign, through the byte-for-byte-unchanged per-entity
  `Renderer::Submit()` path.
- Scene View — even with a batch-eligible group also visible through it, and
  even with both Game and Scene Editor panels open simultaneously (the
  default layout) — and the rare direct-render-to-swapchain fallback both
  keep rendering every entity through the unmodified per-entity path,
  completely unaffected by this campaign, per Locked Design Decision 11.
- `AGENTS.md` documents the new capability (a new "GPU-Driven Rendering"
  section, mirroring every other feature's own precedent); `README.md`'s
  "Status" section has a new bullet.

### Regression / build commands (reference — see each phase for exact use)

```
cmake --build build
cd /d C:\Users\F5954\Documents\TAMANA\GreatTamanaEngine\build && ctest -C Debug --output-on-failure
```

Per Locked Design Decision 9: **only `PHASE7` runs a full build + full
`ctest` pass.** Phases 1–6 do a fast, targeted incremental compile check only.
