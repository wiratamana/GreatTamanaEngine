# PHASE2 — Completion Report: The Instanced Draw Primitive + Indirect Submit

## Status: DONE

Implemented exactly as scoped in
`PHASE2_INSTANCED_DRAW_PRIMITIVE_AND_INDIRECT_SUBMIT.md`, building directly on
PHASE1's already-locked `AABB`/`GpuCullingInstanceInput`/`IndirectDrawCommand`/
`ResourceAccess::VertexShaderStorageRead` (no changes needed to any of PHASE1's
own types/functions — the `Mat4::Data()` transpose-free packing convention
PHASE1 confirmed was used as-is).

## What was built

### New files

- **`src/Shaders/MeshInstanced.vert`** — the new instanced-draw vertex
  shader. Same vertex INPUT attributes as `Mesh.vert` (position, normal —
  `MeshVertex` layout). Reads its per-instance model matrix from a
  `readonly buffer InstanceBuffer` at `set = 0, binding = 0` (a
  `GpuCullingInstanceInput[]`, matching PHASE1's struct byte-for-byte),
  indexed by `gl_InstanceIndex`, instead of the push-constant `model` half
  every other vertex shader uses (`pc.model` is declared but deliberately
  unused — documented in the shader's own header comment). Reuses
  `Shaders/Mesh.frag` completely unmodified. Registered in `CMakeLists.txt`
  immediately after `Mesh.vert`/`Mesh.frag`, unconditional (not
  `GTE_ENABLE_EDITOR`-gated), matching that pair's own precedent.

### Modified files

- **`src/Renderer/Pipeline.h`/`.cpp`** — new `VertexLayout::PositionNormalInstanced`
  enumerator (appended at the end). Both `Pipeline` constructors (single-format
  and the MRT `std::span<const VkFormat>` overload) gained a new, trailing,
  defaulted `VkDescriptorSetLayout instanceBufferSetLayout = VK_NULL_HANDLE`
  parameter — appended strictly AFTER the existing `debugName` parameter (not
  inserted next to `materialSetLayout`), specifically so every existing
  positional call site (which only ever supplies arguments up through
  `debugName`) keeps compiling completely unmodified. `Pipeline.cpp`'s vertex-
  input switch shares the `PositionNormal`/`PositionNormalInstanced` case
  (identical `MeshVertex` binding/attributes for both). The descriptor-set-
  layout assembly resolves `setLayoutToUse = materialSetLayout != VK_NULL_HANDLE
  ? materialSetLayout : instanceBufferSetLayout`, with a debug-only `assert`
  that the two are never both non-null on the same `Pipeline` (Locked Design
  Decision 8 — no Pipeline needs both).
- **`src/Renderer/GpuResourceFactory.h`/`.cpp`** — owns the ONE shared
  instance-buffer descriptor-set-layout (mirrors `MaterialDescriptorSetLayout()`'s
  own "created once in the constructor, exposed via a const accessor"
  precedent exactly — this was the "obvious, not ambiguous" choice the phase
  doc allowed skipping `ask_questions` for): `InstanceBufferDescriptorSetLayout()`,
  built via `DescriptorSetLayoutBuilder(device).AddStorageBuffer(0,
  VK_SHADER_STAGE_VERTEX_BIT).Build()` in the constructor, destroyed in
  `Destroy()`. A real `VkDescriptorSet` built against this layout is allocated
  through the EXISTING `AllocateComputeDescriptorSet()`/`m_computeDescriptorPool`
  — no new, dedicated pool was needed, since the descriptor TYPE
  (`VK_DESCRIPTOR_TYPE_STORAGE_BUFFER`) is identical to a compute shader's own
  binding; only the consuming shader STAGE differs, which a `VkDescriptorPool`
  does not care about. Both `CreatePipeline()` overloads gained a matching
  trailing `bool useInstanceBuffer = false` parameter (mirroring the existing
  `useMaterialTexture` bool exactly, as a separate, unrelated flag), resolving
  to `m_instanceBufferSetLayout` when true.
- **`src/Renderer/Renderer.h`/`.cpp`** — `SubmitIndirect()` (mirrors `Submit()`'s
  shape, with the exact signature the phase doc specified), `SupportsDrawIndirectCount()`
  (forwards to `VulkanDevice`), `InstanceBufferDescriptorSetLayout()` (forwards
  to `GpuResourceFactory`), and a matching trailing `bool useInstanceBuffer`
  parameter on both `CreatePipeline()` overloads. `VulkanContextInfo` gained a
  new `bool supportsDrawIndirectCount` field (mirrors `timestampCapability`).
  `SubmitIndirect()` asserts (debug builds) that it's called inside an active
  `BeginGraphPassRecording()`/`EndGraphPassRecording()` bracket (same "day-one
  render-graph-only, no legacy fallback" precedent as `Dispatch()`) and that
  `mesh.HasIndexBuffer()` (Locked Design Decision 6 — indexed-only). Never
  touches `DrawStats::drawCallCount`/`triangleCount` — see below.
- **`src/Renderer/FrameRecorder.h`/`.cpp`** — new
  `FrameRecorder::IssueIndirectDrawCommand()` static method, the exact
  indirect-draw sibling of `IssueDrawCommand()`: binds `pipeline`, ALWAYS binds
  `instanceBufferDescriptorSet` at descriptor set 0 (never optional, unlike
  `IssueDrawCommand()`'s `materialDescriptorSet`), pushes `viewProj` at the same
  push-constant offset (the `model` half is left zero-initialized — never read
  by `MeshInstanced.vert`), always binds an index buffer (indexed-only), then
  issues **exactly one** `vkCmdDrawIndexedIndirectCount` (when
  `countBuffer != VK_NULL_HANDLE` AND `supportsDrawIndirectCount`) or
  `vkCmdDrawIndexedIndirect` (the fixed-`maxDrawCount`, degenerate-padded
  fallback) — BOTH branches genuinely implemented and reviewed per Locked
  Design Decision 4, gated by a debug-only `assert` if a caller passes a real
  `countBuffer` on a device that doesn't actually support it.
- **`src/Renderer/Vulkan/VulkanDevice.h`/`.cpp`** — new
  `QueryDrawIndirectCountSupport()` free function (a SEPARATE
  `vkGetPhysicalDeviceFeatures2()` call using `VkPhysicalDeviceVulkan12Features`,
  called BEFORE `vkCreateDevice()`), cached in a new `m_supportsDrawIndirectCount`
  member (queried once in `CreateLogicalDevice()`, mirrors
  `TimestampCapability()`'s exact "queried once, cached, const accessor, never
  re-checked" shape), exposed via `SupportsDrawIndirectCount() const noexcept`.
  The `VkPhysicalDeviceVulkan12Features` struct is only ever requested
  (`drawIndirectCount = VK_TRUE`) in the actual device-creation chain when the
  query already reported it available — chained as
  `createInfo.pNext = &features13; features13.pNext = &features12;`.
- **`src/Renderer/DrawStats.h`** — new `DrawStats::indirectDrawCount` field (a
  COUNT OF INDIRECT DRAW CALLS ISSUED, never a triangle/object count) plus a
  new `AccumulateIndirectDrawStats(DrawStats&)` pure accumulator, mirroring
  `AccumulateDrawStats()`'s own shape. **Deliberately NOT wired into
  `Renderer::SubmitIndirect()` itself yet** — this phase's own explicit "no
  render-graph/ECS wiring yet" scope means there is no real per-pass
  `PassContext`/`recordDraw`-shaped callback for an indirect draw to report
  through yet (that plumbing is PHASE5's job, once a real render-graph pass
  actually calls `SubmitIndirect()`). `drawCallCount`/`triangleCount` are
  therefore left completely untouched by `SubmitIndirect()`, exactly as the
  phase doc required.
- **`tests/Renderer/DrawStatsTests.cpp`** — 3 new cases:
  `IndirectDrawCountDefaultsToZero`, `AccumulateIndirectDrawStatsIncrementsOnlyIndirectDrawCount`,
  `AccumulateDrawStatsAndAccumulateIndirectDrawStatsAreIndependent`.
- **`CMakeLists.txt`** — registered `Shaders/MeshInstanced.vert` (see above).

## Deviations / decisions made without needing `ask_questions`

Both of the phase doc's own explicitly-flagged "decide during implementation,
ask if unclear" points were resolved by direct mirroring of an existing,
unambiguous precedent already in this codebase, so neither required
`ask_questions`:

1. **Trailing defaulted parameter vs. a new dedicated overload for
   `CreatePipeline()`'s instanced-pipeline path** — resolved as a trailing
   `bool useInstanceBuffer` parameter, mirroring the already-existing
   `useMaterialTexture` bool exactly (same shape, same "resolve the real
   `VkDescriptorSetLayout` from this factory's own persistent member" logic).
2. **Ownership home for the shared instance-buffer descriptor-set-layout**
   (`GpuResourceFactory` itself vs. a new dedicated class alongside PHASE3's
   future `CullingPipelines`) — resolved as `GpuResourceFactory`, mirroring
   `MaterialDescriptorSetLayout()`'s own already-shipped precedent
   byte-for-byte (same "created once in the constructor, exposed via a
   `noexcept` accessor" shape). `CullingPipelines` (PHASE3) is expected to
   depend on `Renderer::InstanceBufferDescriptorSetLayout()` the same way it
   will depend on any other `Renderer`-owned resource — no coupling issue.

## Manual smoke test (section 3.6) — what was actually verified

A hand-driven smoke test was built, run, manually verified with a live engine
session, and then fully deleted (both the temporary call site and its
supporting `#include`s), exactly as required. **One real, confirmed snag
during this process, worth recording for future phases**: the smoke test's
FIRST placement (inside `RenderPasses.cpp`'s own free-standing
`AddRenderOpaquePass()` function) never actually ran, because that function is
DEAD CODE in the currently-running engine configuration — the real, live
"RenderOpaque" `rg::RenderPassProvider` (`Application.cpp`,
`RegisterOffscreenRenderPipelineProviders()`) duplicates that pass's setup/
execute logic INLINE rather than calling `RenderPasses::AddRenderOpaquePass()`
(confirmed by `search_in_dir` finding zero real call sites for that function
anywhere, only its own definition and comments referencing it). The smoke
test was moved into `Application.cpp`'s real "RenderOpaque" provider's
`desc.execute` lambda, inside the `isGameView` branch only, and confirmed
reachable via a temporary `GTE_LOG_INFO`/`GET /get_logs` round trip before
trusting any visual result. **This is a pre-existing structural fact about
the current codebase (not something this phase introduced or changed)** —
recorded here so PHASE3/PHASE5 (which need to wire the REAL culling
compute pass + indirect draw into the actual Game View pipeline) know to
target `Application.cpp`'s real "RenderOpaque" provider, never the
`RenderPasses.cpp` free function of the same name.

Two builds of the smoke test were run and screenshotted via
`GET /get_game_view`, both showing 3 hand-authored `GpuCullingInstanceInput`
instances (world matrices at X = -1.5/0/+1.5, Z = 5, via
`PackCullingInstanceInput()`) and a hand-authored `IndirectDrawCommand[3]`
array, drawn through `Renderer::SubmitIndirect()` with a dedicated, hardcoded
smoke-test camera (independent of the real scene camera):

1. **Fallback branch** (`countBuffer = VK_NULL_HANDLE`, `maxDrawCount = 3`) —
   `vkCmdDrawIndexedIndirect` — confirmed all 3 quads rendered, at their 3
   distinct world positions.
2. **Real, counted branch** (`countBuffer` pointing at a hand-authored
   `uint32_t` value of `2`, deliberately less than the 3 real commands) —
   `vkCmdDrawIndexedIndirectCount` — confirmed only 2 of the 3 quads rendered
   (the count buffer's runtime value was genuinely read at draw time, not
   ignored).

Both runs produced zero `Warning`/`Error`-level entries via `GET /get_logs`
(`min_level=Warning` returned `count: 0` both times).

### Which device this was verified on, and validation layers

- **`Renderer::SupportsDrawIndirectCount()` reported `TRUE`** on this
  development machine — confirmed via a temporary `GTE_LOG_INFO`/
  `GET /get_logs` round trip (also since deleted). **Both of `SubmitIndirect()`'s
  two code paths were therefore actually exercised and manually verified on
  this machine** (branch 1 above was forced deliberately regardless of this
  machine's real capability, by passing `countBuffer = VK_NULL_HANDLE`
  explicitly; branch 2 above is the one this machine's own capability probe
  would select naturally). **PHASE3 needs to know**: this machine's real
  device genuinely supports `drawIndirectCount` — the real
  `vkCmdDrawIndexedIndirectCount` shader branch PHASE3 plans to build is
  exercisable in this same environment, not merely compiled.
- **Validation layers are NOT available on this development machine** — the
  engine's own startup log records `[Vulkan] Validation was requested but
  VK_LAYER_KHRONOS_validation is not available on this system - continuing
  without it.` This is a pre-existing environment fact (confirmed via
  `GET /get_logs`, not something this phase's code caused) — "verified with
  validation layers enabled" in the strict sense was not literally possible in
  this session; verification instead relied on: (a) zero `Warning`/`Error`
  Logger entries, (b) the visually-correct 3-quad / 2-quad screenshots above,
  and (c) the complete absence of any crash/assert across two full build-run-
  verify cycles. A future phase/session run on a machine with the validation
  layer installed should re-run this same smoke-test shape (or PHASE3's own
  real culling shader) with validation genuinely enabled at least once.

## Compile check (per Locked Design Decision 9 — targeted only, no full build)

- `cmake --build build --target GreatTamanaEngine --config Debug` — succeeds
  cleanly (includes the new `MeshInstanced.vert` shader compiling to
  `.spv` and staging correctly).
- `cmake --build build --target GreatTamanaEngineTests --config Debug` —
  succeeds cleanly.
- Targeted test run: `DrawStatsTest.*:CullingTypes.*:RenderGraphResourceAccessTest.*:RenderGraphBarrierPlannerTest.*`
  — **62/62 tests passed** (10 new/existing `DrawStatsTest` cases including
  this phase's 3 new ones, 10 `CullingTypes`, 12 `RenderGraphResourceAccessTest`,
  30 `RenderGraphBarrierPlannerTest` — all from PHASE1, unaffected, confirming
  zero regression). No full `cmake --build build` + full `ctest` pass was run,
  per `PHASE0_MASTER_STRATEGY.md`'s Locked Design Decision 9 (reserved for
  PHASE7 only).

## What We Did NOT Do (matches the phase doc's own scope)

- No real culling shader, no render-graph pass declaration, no ECS/
  `RenderSystem` involvement.
- No textured (`PositionNormalUv`) instanced variant.
- No change to `Submit()`'s own existing behavior/signature.
- No wiring of `DrawStats::indirectDrawCount` into any real per-pass return
  value yet (see above — PHASE5's job).

## Next phase

PHASE3 (`PHASE3_FRUSTUM_CULL_COMPUTE_SHADER_AND_PIPELINE.md`, a dedicated
double-check phase) can now depend on: `VertexLayout::PositionNormalInstanced`
+ `Shaders/MeshInstanced.vert` both real and working; `Renderer::SubmitIndirect()`
proven correct on both the real-counted and fallback code paths on this
machine; `Renderer::SupportsDrawIndirectCount()` reporting `TRUE` here (so
PHASE3's own real culling shader's counted-output path is directly
exercisable in this same environment, not just compiled); and
`GpuResourceFactory::InstanceBufferDescriptorSetLayout()`/
`Renderer::AllocateComputeDescriptorSet()` as the established way to build a
real instance-buffer descriptor set. PHASE3 should also note the
`RenderPasses.cpp` vs. `Application.cpp` "RenderOpaque" dead-code-vs-real-
provider distinction recorded above when it comes time (PHASE5) to wire a
real pass into the actual Game View pipeline.
