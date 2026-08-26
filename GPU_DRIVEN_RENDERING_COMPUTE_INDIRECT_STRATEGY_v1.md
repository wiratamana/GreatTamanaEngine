# GPU_DRIVEN_RENDERING_COMPUTE_INDIRECT_STRATEGY_v1.md

### Scaffolding for GPU-Driven Rendering: Compute Shaders + Indirect Draw Buffers, integrated into `gte::rg::RenderGraph`

This is a companion document to
`RENDERGRAPH_FUTURE_TODO_DELIBERATELY_NOT_IMPLEMENTED.md` (read that first —
Section D there is this document's own dependency map) and follows the
exact same phased-strategy-doc discipline established by
`RENDERGRAPH_PHASE0_MASTER_STRATEGY_v2.md` through
`RENDERGRAPH_PHASE8_COMPLETION_REPORT.md`: each sub-phase gets a Goal /
Situation / Plan / What We Will NOT Do / Their Role structure, is
independently landable and testable, and produces its own
`GPUDRIVEN_PHASEn_COMPLETION_REPORT.md` once implemented.

This document is exactly the "real, concrete need" that
`RENDERGRAPH_PHASE9_ADVANCED_FUTURE_SCOPE_STRATEGY_v2.md` said would justify
building compute-pass support — see that file's own item 3.3 ("What
first: identify the first real compute workload, THEN extend Phase 1's
enum/Phase 5's planner together, in one small, focused follow-up phase").
That workload is now identified: **GPU frustum culling that populates an
indirect draw buffer, consumed by a single `vkCmdDrawIndexedIndirectCount`
call** — this also happens to be exactly the missing, still-unproven
"genuine cross-pass READ" validation `RENDERGRAPH_PHASE0_MASTER_STRATEGY_v2.md`'s
own V2 Revision Note 4 has been waiting on since Phase 7.

---

## Step 1: The Goal

Teach the engine to run compute shaders at all, teach it to draw via
indirect command buffers at all, and teach `RenderGraph` to treat both as
first-class declared passes/resources — with automatic barrier synthesis,
culling, and resource pooling working for them exactly the same way they
already work for graphics passes and texture resources. By the end of this
campaign, a NEW render pass can:

1. Run a compute shader that reads per-instance transform/bounding-sphere
   data from a storage buffer, tests each instance against the camera
   frustum, and writes ONE `VkDrawIndexedIndirectCommand` per surviving
   instance into an indirect-draw buffer, plus an atomic draw-count into a
   companion count buffer.
2. Hand that buffer off, through the render graph's own declared
   read/write dependency system (a real `ReadBuffer()` this time, not a
   theoretical one), to a graphics pass that issues exactly ONE
   `vkCmdDrawIndexedIndirectCount` call instead of one `vkCmdDrawIndexed`
   call per surviving object.
3. Have the barrier between step 1 and step 2 synthesized automatically
   by Phase 5's existing `RequiredStateFor()`/`RequiresBarrier()`
   machinery, extended with a compute-shader-write → indirect-command-read
   transition it does not know about today.

This is deliberately scoped as ONE validated feature end-to-end (GPU
frustum culling for the existing primitive/mesh rendering path), not a
generic "compute framework" built speculatively — matching this codebase's
own repeatedly-stated discipline (see `RENDERGRAPH_PHASE9_...`'s own
closing words: "build only what is needed, when the need is real and
demonstrated, never speculatively").

## Step 2: The Situation

Everything needed for this milestone is either completely absent or
present only as an unused stub. Concretely, as of the end of Phase 8:

- **No compute shader exists anywhere.** `Pipeline.h/.cpp` hardcodes a
  graphics pipeline (`VkGraphicsPipelineCreateInfo`, vertex+fragment
  stages, `VkPipelineRenderingCreateInfo` for dynamic rendering — see
  `Pipeline.cpp`). There is no `VkComputePipelineCreateInfo` anywhere, no
  `.comp` shader source under `src/Shaders/`, and `GpuResourceFactory` has
  no `CreateComputePipeline()` method.
- **`ResourceAccess` (`RenderGraphTypes.h`) has no compute-shader or
  indirect-buffer access kinds.** Its five values
  (`ColorAttachmentWrite`/`DepthStencilAttachmentReadWrite`/`ShaderRead`/
  `TransferSrc`/`TransferDst`) were deliberately scoped to graphics-only
  needs (Phase 1's own doc: "scoped to exactly what Phases 1-8's
  graphics-only MVP needs"). `IsWriteAccess()`/`ToString()`/
  `RequiredStateFor()` are all written as exhaustive switches with **no
  `default:` case** specifically so adding a new enumerator forces every
  one of these call sites to be revisited — this is the extension point
  this campaign uses.
- **`RenderGraphBuilder::PassBuilder::ReadBuffer()`/`WriteBuffer()`
  already exist but have never been called by any real pass.** Phase 2's
  own completion report flags this explicitly: "no real Phase 1-8 pass
  exercises this path yet... exists purely so `CreateBuffer()`'s
  `BufferHandle` has SOME way to be used inside a pass at all." This
  campaign is the first real consumer.
- **`RenderGraphResourcePool::AcquireBuffer()` already builds a
  `BufferMemoryUsage::GpuOnly` buffer via `Renderer::CreateBuffer()`, but
  nothing ever calls it in production.** Confirmed by direct inspection of
  `RenderGraph.cpp`'s `EnsureBufferResolved()` — fully wired, fully
  untested against a real workload.
- **No indirect draw call exists anywhere.** `FrameRecorder::IssueDrawCommand()`
  (the shared, extracted-in-Phase-7 per-draw-item body both the legacy
  path and `Renderer::Submit()`'s render-graph redirect call into) only
  ever issues `vkCmdDraw`/`vkCmdDrawIndexed`, with CPU-supplied
  `vertexCount`/`indexCount`/instance count of exactly 1. There is no
  `VkDrawIndexedIndirectCommand` struct, no GPU buffer laid out to hold an
  array of them, and `DrawStats.h`'s own `AccumulateDrawStats()`
  explicitly documents "every draw has `instanceCount == 1`... no
  instancing exists anywhere in this engine yet" as a load-bearing
  assumption that must be revisited the moment this changes.
- **`RenderSystem::CollectRenderables()` performs zero culling of any
  kind.** Every `MeshRenderer` in the `Registry` is walked and submitted
  unconditionally, every frame (`RenderSystem.cpp`). There is no
  per-instance bounding-sphere/AABB data anywhere in the ECS today either
  — `Transform`/`MeshRenderer` carry no bounds field.
- **`Renderer::Submit()`'s current shape is fundamentally per-draw,
  CPU-driven.** It takes a `Pipeline&`, `Mesh&`, model matrix, view-proj
  matrix, and an optional material descriptor set, called once per visible
  entity from `RenderSystem::Draw()`. This is the call site GPU-driven
  rendering is explicitly meant to REPLACE for the culled/instanced case —
  but it must remain fully functional for anything NOT yet migrated to the
  new path (UI meshes, Editor previews, a single hero object, etc.) —
  exactly the same "strangler fig, zero regression to what already works"
  discipline Phase 7 used for the render graph migration itself.
- **The barrier planner (`RenderGraphBarrierPlanner.h/.cpp`) has no
  compute-shader or indirect-command-read state.** `RequiredStateFor()`'s
  exhaustive switch would fail to compile the moment a new `ResourceAccess`
  enumerator is added without a matching case — this is the intended,
  designed-in safety net this campaign relies on rather than fights.
- **`RenderGraph::PassContext`'s single-color-attachment-per-pass
  execution model already has a "no color write → no
  `vkCmdBeginRendering` bracket" branch** (`RenderGraph.cpp`'s
  `ExecuteCompiledGraph()`: "A pass with no `ColorAttachmentWrite` write
  ... gets no vkCmdBeginRendering bracket at all - its execute callback is
  invoked with a zero-extent PassContext and is expected to record
  whatever non-rendering Vulkan work it needs directly against `cmd`").
  **This means a pure compute pass is ALREADY representable by the
  existing execution engine, today, with zero changes** — the gap is
  entirely in (a) `ResourceAccess`/barrier support for compute-shader
  read/write and indirect-command-read, and (b) there being no way yet to
  bind a compute pipeline / dispatch / build an indirect buffer at all.

## Step 3: The Plan

Six sub-phases (A-F), a validation feature (G), and Editor tooling (H) —
deliberately more numerous, smaller phases than the original 9-phase Render
Graph campaign, since each individual piece here is small and this is new,
higher-risk Vulkan surface area (first-ever compute pipeline in this
engine) that benefits from being landable/reviewable in isolation.

### Phase A — Compute pipeline infrastructure (no RenderGraph integration yet)

**Goal:** Get a `VkComputePipeline` compiling, binding, and dispatching at
all, completely independent of the render graph — mirrors how the original
campaign's Phase 1 built pure vocabulary with zero live-device dependency
before touching execution.

**Plan:**
- New `src/Renderer/ComputePipeline.h/.cpp` — an RAII wrapper analogous to
  `Pipeline`, but for `VK_PIPELINE_BIND_POINT_COMPUTE`: one
  `VkPipelineLayout` (built from a caller-supplied array of
  `VkDescriptorSetLayout`s plus an optional push-constant range — mirror
  `Pipeline`'s existing 128-byte push-constant convention where
  reasonable), one `VkPipeline` built from a single `VkPipelineShaderStageCreateInfo`
  (`VK_SHADER_STAGE_COMPUTE_BIT`) loaded from a compiled `.comp.spv` file
  via the SAME SPIR-V-loading helper `Pipeline.cpp` already has (extract it
  into a small shared free function, e.g. `Vulkan/ShaderModule.h`, rather
  than duplicating the file-read+`vkCreateShaderModule` dance a second
  time).
- `GpuResourceFactory::CreateComputePipeline(shaderSpirvPath, descriptorSetLayouts, pushConstantSize)`
  — same shape/ownership convention as `CreatePipeline()`.
- New descriptor-set-layout helpers on `GpuResourceFactory`, analogous to
  the existing `MaterialDescriptorSetLayout()`: a fixed
  **culling-pass descriptor set layout** with two storage-buffer bindings
  (binding 0 = read-only per-instance input SSBO — world matrix + bounding
  sphere; binding 1 = read-write indirect-command output SSBO) and one
  additional binding for the draw-count buffer (binding 2, if
  `VK_KHR_draw_indirect_count`/Vulkan 1.2 core `vkCmdDrawIndexedIndirectCount`
  is available — see Phase C below for the fallback path when it isn't).
- Compile a first, TRIVIAL compute shader (`Shaders/Passthrough.comp` — one
  thread writes a fixed value into a storage buffer) purely to prove the
  whole pipeline/descriptor/dispatch path end to end, via a throwaway,
  temporary test call site (mirrors Phase 6's own "build it with a
  THROWAWAY two-pass test scene FIRST" discipline) — never shipped, deleted
  once Phase B's real integration lands.
- `cmake/CompileShaders.cmake`'s `gte_add_shader()` already compiles
  `.vert`/`.frag` via `glslc` generically by file extension — confirm it
  handles `.comp` with zero changes (glslc infers stage from extension
  already); add the new shader file(s) to `CMakeLists.txt`/staging exactly
  like every other shader.

**Tests:** `ComputePipeline`/`GpuResourceFactory::CreateComputePipeline()`
fall into the same accepted Tier-2 ("needs a live `VkDevice`, no automated
coverage yet") bucket `Pipeline`/`Buffer`/`RenderTexture` already occupy —
manual verification via the throwaway dispatch above, run with validation
layers enabled, is this phase's accepted bar (see `TESTING.md`).

**What We Will NOT Do:** No RenderGraph involvement yet. No indirect draw
yet. No real culling shader yet — the passthrough shader above is
deliberately trivial and thrown away.

### Phase B — `ResourceAccess`/barrier extension for compute + indirect consumption

**Goal:** Extend Phase 1's `ResourceAccess` enum and Phase 5's barrier
planner with exactly the values this milestone needs — no more.

**Plan:**
- Add to `RenderGraphTypes.h`'s `ResourceAccess` enum:
  - `ComputeShaderRead` — a buffer/image read by a compute shader
    (`VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT` /
    `VK_ACCESS_2_SHADER_STORAGE_READ_BIT`).
  - `ComputeShaderWrite` — a buffer/image written by a compute shader
    (same stage, `VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT`).
  - `IndirectCommandRead` — a buffer consumed as the argument buffer to
    `vkCmdDrawIndexedIndirect(Count)`
    (`VK_PIPELINE_STAGE_2_DRAW_INDIRECT_BIT` /
    `VK_ACCESS_2_INDIRECT_COMMAND_READ_BIT`).
- Update `IsWriteAccess()`/`ToString()`/`RequiredStateFor()` (all three
  deliberately have NO `default:` case — this addition will not compile
  until all three are updated, which is the intended, designed-in
  guardrail). This is the exact mechanical step Phase 1's own "standing
  rule" and "no default case" discipline was built for.
- Add regression tests to `RenderGraphTypesTests.cpp` (one case per new
  enumerator, matching the existing per-enumerator test pattern exactly)
  and to `RenderGraphBarrierPlannerTests.cpp` (a hand-simulated
  ComputeShaderWrite → IndirectCommandRead sequence, confirming exactly
  one barrier is emitted with the correct access/stage masks — mirroring
  Phase 5's own "three-pass hand-simulated sequence" test shape).
- `PassBuilder::ReadBuffer()`/`WriteBuffer()` need no signature change —
  they already take a `ResourceAccess` parameter with a default; a compute
  pass simply passes the new enumerators explicitly.

**What We Will NOT Do:** No `ShaderReadWrite` combined read/write value
(Phase 9's own backlog names this — keep the two-value read/write split
this campaign already uses everywhere else, e.g.
`DepthStencilAttachmentReadWrite` is the one deliberate exception and it
stays that way for depth specifically). No storage-IMAGE-specific access
kind yet — this milestone's culling shader only touches buffers.

### Phase C — Indirect draw buffer format + `Renderer` support for issuing indirect draws

**Goal:** Define the indirect-command buffer's exact binary layout, and
teach the engine to actually ISSUE `vkCmdDrawIndexedIndirect(Count)` from
it — independent of who populated it (a compute shader, or, for testing,
hand-written CPU data).

**Plan:**
- New `src/Renderer/IndirectDrawTypes.h` — a plain, `#pragma pack`-free
  (Vulkan's own struct is already tightly specified) mirror of
  `VkDrawIndexedIndirectCommand` (`indexCount`, `instanceCount`,
  `firstIndex`, `vertexOffset`, `firstInstance`) plus a small,
  engine-specific **per-instance CULLING INPUT** struct (NOT the indirect
  command itself — the data a compute shader reads to decide whether to
  emit one): world matrix (or a `TransformHandle`-shaped index into a
  separate transform SSBO, TBD during implementation) + bounding-sphere
  center/radius + a `MeshHandle`/submesh index for the compute shader to
  copy into the correct `firstIndex`/`indexCount`/`vertexOffset` fields of
  the command it emits.
- `Renderer` gains `SubmitIndirect(const Pipeline&, const Mesh&,
  VkBuffer indirectBuffer, VkDeviceSize indirectOffset, std::uint32_t
  maxDrawCount, VkBuffer countBuffer = VK_NULL_HANDLE, VkDeviceSize
  countBufferOffset = 0, const Mat4& viewProjection = Mat4::Identity())` —
  the indirect-draw sibling of `Submit()`. Internally issues
  `vkCmdDrawIndexedIndirectCount` when `countBuffer != VK_NULL_HANDLE` and
  the device supports it (probe via `VkPhysicalDeviceVulkan12Features::drawIndirectCount`
  at device-creation time, mirroring `VulkanDevice::TimestampCapability()`'s
  own capability-probing precedent exactly), else falls back to
  `vkCmdDrawIndexedIndirect` with a FIXED `maxDrawCount` (meaning: on a
  device without `drawIndirectCount`, the culling compute shader must
  additionally either (a) write a full `maxDrawCount`-sized command array
  where culled-out entries have `indexCount = 0` — a valid, harmless
  degenerate draw — or (b) the CPU reads back the count each frame, which
  defeats the entire point and must NOT be the chosen fallback). Document
  this capability split as clearly as `GpuTimingSlot`'s own
  synchronous-vs-pipelined split is documented.
- `AccumulateDrawStats()`/`DrawStats.h` needs a THIRD counting mode for an
  indirect draw: since the CPU genuinely does not know the real
  `drawCallCount`/`triangleCount` for an indirect draw without reading the
  count buffer back (a synchronous stall this milestone must never
  introduce — see `AGENTS.md`'s "Profiling" section on never adding a new
  GPU wait purely to fetch a number sooner), `DrawStats` gains an explicit
  "unknown/indirect" representation (mirroring `GpuSampleStatus::Absent`'s
  own "never fabricate a bare numeric 0" rule) rather than silently
  reporting 0 draws or a wrong number. A later, OPTIONAL readback (once
  Phase D's culling pass already computed the count on the GPU and it is
  convenient to expose, e.g. via a small compute-shader-side atomic counter
  copied back next frame, never blocking) can fill this in for display
  purposes only — never for correctness.
- `FrameRecorder`/`RenderGraph::PassContext` need a matching
  `recordIndirectDraw` hook alongside the existing `recordDraw` (mirrors
  `PassContext::recordDraw`'s own "fused, per-draw-call-site" discipline —
  see `AGENTS.md`'s "Profiling" `AccumulateDrawStats()` rule) so a pass's
  `execute` callback still reports its stats at the exact call site that
  issues the real Vulkan command, never from a separate pass.

**What We Will NOT Do:** No `vkCmdDrawIndirect` (non-indexed) support —
every mesh in this engine that matters for this milestone already has an
index buffer (see `README.md`'s "Rendering" section). No multi-draw across
DIFFERENT pipelines/materials in one indirect call — this milestone's
culling pass targets one pipeline/one material batch at a time, exactly
matching how `RenderSystem::Draw()` already submits per-pipeline today; a
true bindless multi-material indirect batch is explicitly Phase 9-adjacent
future work (see the companion TODO document's C.4).

### Phase D — The real GPU culling compute shader + pass declaration

**Goal:** Write the actual feature: a compute shader that frustum-culls a
batch of instances and emits an indirect-draw command buffer, declared as
a real `RenderGraphBuilder::AddPass()`-style compute pass.

**Plan:**
- `RenderGraphBuilder::PassBuilder` gains no new METHOD (`ReadBuffer()`/
  `WriteBuffer()` already exist and already take a `ResourceAccess`) —
  this phase is pure shader/orchestration work, not builder-API surface
  area. A new convenience, `AddComputePass(name, setup, execute)`, MAY be
  added as a thin alias of `AddPass()` purely for readability at call
  sites (a compute pass never declares a color/depth write) — evaluate
  during implementation whether this earns its own name or whether
  `AddPass()` alone reads clearly enough; if added, it must be a pure
  naming convenience with zero behavioral difference, so `RenderGraph::Execute()`
  needs no new branch to support it (a pass's behavior is already fully
  determined by what it declares in `writes`, per Phase 6's existing "no
  color write → no rendering bracket" logic).
- `Shaders/FrustumCull.comp` — one thread per instance: reads this
  instance's world-space bounding sphere (already-transformed on the CPU
  once per frame into the input SSBO, OR transformed in-shader from a
  local-space sphere + the instance's world matrix — prefer the latter,
  since it means the CPU-side per-frame cost is just one SSBO upload of
  already-known Transform data, not a second CPU-side culling pass that
  would defeat the entire purpose), tests against 6 frustum planes derived
  from the view-projection matrix (passed as a push constant or a small
  uniform buffer), and on success does an atomic increment against the
  count buffer to reserve a slot, then writes a fully-populated
  `VkDrawIndexedIndirectCommand` into that slot of the output buffer.
- The compute PASS declares: `ReadBuffer(instanceInputHandle,
  ComputeShaderRead)`, `WriteBuffer(indirectCommandHandle,
  ComputeShaderWrite)`, `WriteBuffer(drawCountHandle, ComputeShaderWrite)`.
- The GRAPHICS pass that consumes the result declares:
  `ReadBuffer(indirectCommandHandle, IndirectCommandRead)`,
  `ReadBuffer(drawCountHandle, IndirectCommandRead)`, plus its usual
  `WriteColorAttachment()`/`WriteDepthStencilAttachment()` — and its
  `execute` callback calls the new `Renderer::SubmitIndirect()` (Phase C)
  instead of a per-entity `Renderer::Submit()` loop.
- `RenderGraph::Execute()`'s existing barrier-application loop
  (`ApplyUsageBarrierIfNeeded()`) needs ZERO changes to correctly order
  these two passes — Phase 3's compiler already builds a dependency edge
  from any WRITE to a later READ of the same `BufferHandle`
  (`RenderGraphCompiler.cpp`'s existing RAW-edge logic operates identically
  for buffers and textures already, per its own `ResourceKind` branch).
  **This is the concrete proof, called out in Step 1 above, that Phases
  1-6's original design already generalizes correctly to this workload
  without modification** — only Phase 1's enum and Phase 5's
  `RequiredStateFor()` needed new cases (Phase B above).
- `MeshInstantiationSystem`/`RenderSystem` need a way to opt a batch of
  entities INTO the GPU-driven path (a per-`MeshRenderer` or per-material-batch
  flag, or — simpler for a first cut — a wholly separate, parallel
  "instanced batch" concept that coexists with the existing per-entity
  `Renderer::Submit()` path rather than replacing it) — start with the
  SIMPLEST possible integration (a single, hardcoded test batch of
  primitive-shape instances, mirroring how the original Render Graph
  campaign's own Phase 6 validated itself with a "throwaway two-pass test
  scene" before ever touching real Game/Scene/Present passes).

**Tests:** The compute shader's actual culling correctness is Tier 2 (needs
a live device) — but the PURE frustum-plane-extraction math (given a
view-projection `Mat4`, produce 6 plane equations) and the
PURE sphere-vs-frustum test itself should be written as ordinary,
Tier-1-testable C++ functions FIRST (mirroring this engine's own repeated
"extract the pure math, test it without a live device, THEN write the thin
GPU-side mirror of it" pattern — see `GpuTiming.h`/`RenderGraphBarrierPlanner.h`),
even though the actual GPU compute shader is a separate, non-testable GLSL
implementation of the same math. This gives a hand-verifiable reference
implementation to manually validate the shader's output against.

**What We Will NOT Do:** No occlusion culling (frustum only, for this
milestone). No hierarchical/two-pass culling (a single dispatch, one
thread per instance — the "two-phase occlusion culling" technique some
engines use is explicitly out of scope, a natural Phase-9-style follow-up
once frustum culling is proven). No LOD selection in the same shader (a
separate, later concern).

### Phase E — GPU timing wiring for the new passes

**Goal:** Close the single highest-priority gap the Render Graph campaign
itself already flagged three completion reports in a row (see the
companion TODO document's Section B.1), specifically so THIS milestone can
be honestly evaluated.

**Plan:** Generalize `GpuTimingService`'s fixed 3-slot `VkQueryPool` into
one keyed by `RenderGraphNameSlotTable` (already fully built, already
exercised on every `Execute()` call, just not yet connected to a real
query pool — see `RenderGraph.h`'s own "GPU TIMING NOTE"). This is
independent, pre-existing debt, not something this milestone introduces —
but it must land alongside this milestone, not after, per the companion
document's Section D.5 reasoning: you cannot evaluate whether GPU-driven
culling is actually a win without real per-pass GPU milliseconds for both
the new compute pass and the indirect graphics pass it feeds.

**What We Will NOT Do:** No change to `Profiling::GpuPass`'s fixed
3-value enum (`GameView`/`SceneView`/`Present`) — the new compute/indirect
passes' timing is surfaced through the Editor's existing "Render Graph"
panel (Phase 8, already generic over an arbitrary pass count), not through
"Profiler" (see the companion TODO document's Section A.9 — that remains
explicitly deferred).

### Phase F — Barrier/execution regression safety

**Goal:** Prove the new barrier cases don't regress anything already
shipped.

**Plan:** Full `ctest` run (currently 623 tests) before and after every
sub-phase above, exactly like every prior Render Graph phase's own
"Verification performed" section. Add an explicit regression test to
`RenderGraphBarrierPlannerTests.cpp` proving a `ComputeShaderWrite` →
`IndirectCommandRead` transition produces the EXACT expected
`srcStageMask`/`dstStageMask`/`srcAccessMask`/`dstAccessMask` pair
(`COMPUTE_SHADER`/`SHADER_STORAGE_WRITE` → `DRAW_INDIRECT`/
`INDIRECT_COMMAND_READ`), mirroring Phase 5's own "field-for-field regression
test" discipline exactly.

### Phase G — Validation: prove the whole thing end-to-end

**Goal:** The concrete "does this actually work and actually help" check —
mirrors Phase 7's own deferred Step 3.7 (a real cross-pass texture read,
never completed) but delivers it via a BUFFER read instead, which this
milestone needed anyway.

**Plan:** Spawn a real test scene with a meaningful instance count (e.g.
a grid of primitive-shape entities, reusing `PrimitiveMeshGenerator`/
`Game::CreatePrimitiveEntity()` — no new asset pipeline needed), enable the
GPU-driven path for that batch, and manually verify via:
- The Editor's "Render Graph" panel (Phase 8) showing the new compute pass
  and the indirect graphics pass, in the correct order, with the correct
  declared reads/writes, and NEITHER culled.
- Validation layers reporting zero new warnings/errors.
- The "Profiler"/"Render Graph" panel's real GPU-timing numbers (once
  Phase E lands) showing the indirect pass's draw count dropping as the
  camera looks away from parts of the grid — the actual, visible proof
  culling is happening.
- A manual visual comparison against the same scene rendered through the
  ORIGINAL per-entity `Renderer::Submit()` path — pixel-identical output,
  proving this is a pure performance change, not a rendering-behavior
  change (mirroring Phase 7's own "zero observable behavior difference"
  requirement for the original render-graph cutover).

### Phase H — Editor debug tooling extension

**Goal:** Make the new machinery observable, matching this engine's
established "Memory"/"Profiler"/"Render Graph" panel philosophy.

**Plan:** `RenderGraphSnapshot`/`BuildRenderGraphSnapshot()` (Phase 8)
already generalize over an arbitrary declared pass/resource set with no
changes needed — a compute pass and its buffer resources will already
show up correctly in the "Render Graph" panel the moment Phase D's real
pass exists, since that panel's data model was deliberately built
pass/resource-shape-agnostic from day one. The only genuinely NEW Editor
work worth considering: a small "instances culled this frame" readout
(count buffer's final value, read back non-blockingly on a LATER frame —
mirroring `GpuTimingService::ReadPresentResultIfAvailable()`'s own
"read back a past frame's result at the point synchronization already
proves it's safe" pattern exactly, never a new blocking GPU wait).

## Step 4: What We Will NOT Do (campaign-wide)

- We will **not** build a generic, speculative "compute pass framework"
  divorced from this one real feature — every piece of new API surface
  above exists because Phase D's culling pass specifically needs it.
- We will **not** introduce async compute / a second (compute) queue in
  this campaign — see the companion TODO document's Section A.2; this
  stays entirely on the single existing graphics queue, submitted via the
  existing offscreen/present regimes (`ExecuteTimingMode`), for the whole
  duration of this campaign.
- We will **not** build bindless/descriptor-indexing infrastructure now —
  see the companion TODO document's Section C.4; this milestone's culling
  batch is single-material by design.
- We will **not** attempt GPU compute skinning in this same campaign, even
  though it is a natural second compute workload (see the companion TODO
  document's Section C.5) — land GPU-driven culling first, fully proven,
  before starting a second, unrelated compute feature. Note it explicitly
  as the next candidate once this campaign closes.
- We will **not** touch `RenderGraphResourcePool`'s memory-aliasing
  behavior, MRT support, or any other Phase-9-backlog item not directly
  required by compute+indirect support.
- We will **not** remove/replace the existing per-entity
  `Renderer::Submit()` path — it remains the correct, simpler mechanism for
  anything not opted into GPU-driven batching (UI, Editor previews, single
  hero objects), exactly mirroring how the render graph migration itself
  never removed `AssetPreviewMesh`/`BoneViewerWindow`'s own independent
  Vulkan pipelines.

## Step 5: Their Role

- Land Phases A→F in order — do not skip to Phase D "because it's the
  interesting part." Phase A alone (a working compute dispatch with
  nothing else attached) is the single highest-risk, first-of-its-kind
  Vulkan surface area in this whole campaign; get it manually verified with
  validation layers clean before building anything on top of it.
- Treat Phase B's "no `default:` case forces every switch to be revisited"
  guardrail as a feature, not friction — if the compiler doesn't force you
  to touch `RequiredStateFor()`/`IsWriteAccess()`/`ToString()`, you added
  the new `ResourceAccess` value incorrectly.
- Phase E (GPU timing) is not optional busywork bolted on afterward — it
  is what makes Phase G's validation claim ("this is actually faster")
  honest rather than assumed. Do not skip it to ship sooner.
- Once Phase G's validation lands, write a campaign-level
  `GPUDRIVEN_CAMPAIGN_COMPLETION_REPORT.md`, mirroring
  `RENDERGRAPH_CAMPAIGN_COMPLETION_REPORT.md`'s own shape — including an
  honest "what remains open" section (async compute, bindless, GPU
  skinning, occlusion culling all belong there, not silently dropped).
