# PHASE2 — The Instanced Draw Primitive + Indirect Submit

## Parent
`PHASE0_MASTER_STRATEGY.md` — read it first. Also re-read
`PHASE1_COMPLETION_REPORT.md` before starting (it records the real
`Math/Mat4.h` storage convention this phase's shader needs to agree with).

## Step 1: The Goal (Where are we going?)

Give the engine two genuinely new, low-level Vulkan capabilities — completely
independent of the render graph and of any real culling shader — proven by a
small, throwaway, hand-driven smoke test, mirroring the compute-shader
campaign's own Phase 2 discipline ("get it compiling/dispatching at all,
fully independent of the live-device half that will consume it later"):

1. **A per-instance model-matrix source for the vertex shader** —
   `VertexLayout::PositionNormalInstanced` + `Shaders/MeshInstanced.vert`,
   reusing `Mesh.frag` unmodified — so ONE `vkCmdDrawIndexedIndirect(Count)`
   call can render N differently-positioned/oriented objects.
2. **`Renderer::SubmitIndirect()`** — the indirect-draw sibling of
   `Renderer::Submit()`, plus a real device-capability probe for
   `drawIndirectCount`, with BOTH the real (`vkCmdDrawIndexedIndirectCount`)
   and fallback (`vkCmdDrawIndexedIndirect`, fixed count) branches genuinely
   implemented (Locked Design Decision 4, PHASE0).
3. **`DrawStats`'s new "unknown/indirect" bucket** — since the CPU cannot know
   an indirect draw's real triangle/draw count without a blocking readback,
   which this campaign must never introduce.

## Step 2: The Situation (Where are we now?)

- `Pipeline.h`'s `VertexLayout` enum has exactly 3 values
  (`PositionColor`/`PositionNormal`/`PositionNormalUv`), and its ENTIRE
  push-constant convention (128 bytes, `model` then `viewProj`, vertex stage
  only — see `Pipeline.h`'s own class comment) is built around one draw call
  == one object. There is no descriptor-set-bound per-instance data source
  anywhere in any existing `Pipeline`.
- `Shaders/Mesh.vert`/`Mesh.frag` is the untextured `PositionNormal` pair this
  new instanced variant mirrors (confirmed present under `src/Shaders/`).
  `Shaders/TexturedMesh.vert`/`.frag` (the textured pair) is NOT touched by
  this campaign at all (Locked Design Decision 8, PHASE0).
- `Vulkan/DescriptorSetLayoutBuilder.h`'s `AddStorageBuffer()` already accepts
  an explicit `VkShaderStageFlags stageFlags` parameter (default
  `VK_SHADER_STAGE_COMPUTE_BIT`) — confirmed directly by reading the header —
  so building a `VK_SHADER_STAGE_VERTEX_BIT` storage-buffer descriptor set
  layout needs ZERO changes to that builder; just pass a different
  `stageFlags` argument.
- `Renderer.h`'s `Submit()` (line ~461) is the exact shape `SubmitIndirect()`
  mirrors; `FrameRecorder::IssueDrawCommand()` (`FrameRecorder.h`, line ~77)
  is the exact per-item Vulkan-call body a new `IssueIndirectDrawCommand()`
  mirrors, for the same "callable directly while a render-graph pass is being
  recorded" reason `IssueDrawCommand()` itself was extracted for.
- `VulkanDevice::CreateLogicalDevice()` (`src/Renderer/Vulkan/VulkanDevice.cpp`,
  line ~187) chains exactly one feature struct today
  (`VkPhysicalDeviceVulkan13Features`, `createInfo.pNext = &features13`). No
  `VkPhysicalDeviceVulkan12Features` chain link, and no capability QUERY
  (`vkGetPhysicalDeviceFeatures2`) of any kind, exists anywhere in this file.
  `VulkanDevice::TimestampCapability()`/`QueryTimestampCapability()`
  (same file, line ~254) is the exact "query once in the constructor, cache,
  expose via a const accessor, never re-check" precedent to mirror.
- `DrawStats.h`'s `DrawStats` struct has exactly two fields
  (`drawCallCount`/`triangleCount`), both assumed CPU-known at record time —
  confirmed by reading `AccumulateDrawStats()`'s own doc comment ("every draw
  has `instanceCount == 1`... no instancing exists anywhere in this engine
  yet").

## Step 3: The Plan

### 3.1 — `VertexLayout::PositionNormalInstanced` + `Shaders/MeshInstanced.vert`

- Add the new enumerator to `Pipeline.h`'s `VertexLayout` (append at the end
  — never renumber existing values, since nothing in this codebase currently
  serializes `VertexLayout` as a raw integer, but keep the same append-only
  discipline as every other enum in this campaign).
- `Shaders/MeshInstanced.vert` — same vertex INPUT attributes as `Mesh.vert`
  (position, normal — `MeshVertex` layout, unchanged), but:
  - Binds a `readonly buffer` (std430) at `set = 0, binding = 0` matching
    `GpuCullingInstanceInput`'s exact layout (PHASE1) — same struct, reused
    directly, since the culling shader already had to read the world matrix
    from here; the vertex shader reads the SAME array, indexed by
    `gl_InstanceIndex`.
  - Reads `viewProj` from the SAME 128-byte push-constant range every other
    vertex shader uses (still one value per draw call — the camera doesn't
    change per-instance) — but the `model` half of that push constant is
    UNUSED/ignored by this shader (the model matrix comes from the SSBO
    instead). Document this clearly in the shader's own header comment so a
    future reader isn't confused about why half the push constant appears
    unused here specifically.
  - Outputs exactly what `Mesh.frag` expects as input (world-space position,
    world-space normal) — confirm `Mesh.frag`'s actual input interface block
    before writing this (read the file), so the two genuinely link/match
    with zero fragment-shader changes.
- `Pipeline`'s constructor (`Pipeline.cpp`) needs a new descriptor-set-layout
  parameter path for this one `VertexLayout` value — mirror how
  `materialSetLayout`/`useMaterialTexture` already conditionally add ONE
  descriptor set for `PositionNormalUv`; add an analogous, SEPARATE
  `instanceBufferSetLayout` parameter (defaulted `VK_NULL_HANDLE`, meaningful
  only for `PositionNormalInstanced`) that becomes descriptor set 0 in this
  Pipeline's own `VkPipelineLayout` when non-null. Do NOT reuse/rename
  `materialSetLayout` for this — the two concepts (a material texture set, an
  instance-transform-buffer set) are unrelated and must never be silently
  conflated, even though neither is ever used simultaneously today (Locked
  Design Decision 8, PHASE0: textured batches are out of scope, so no
  Pipeline ever needs both at once — but keep them as two independently-named
  parameters anyway, for clarity and future-proofing).
- `GpuResourceFactory::CreatePipeline()`/`Renderer::CreatePipeline()` gain a
  new, small, dedicated overload (or a trailing defaulted parameter — decide
  based on which reads more clearly once the exact `Pipeline` constructor
  shape above is finalized; use `ask_questions` if genuinely unclear) for
  building a `PositionNormalInstanced` pipeline, forwarding a caller-supplied
  `VkDescriptorSetLayout` for the instance buffer.
- A NEW `GpuResourceFactory` (or a small, dedicated new class — mirror
  `GpuSkinningPipelines`'s own "owns its one shared descriptor-set-layout"
  shape) method builds and OWNS the ONE shared instance-buffer descriptor-set
  layout every `PositionNormalInstanced` pipeline is built against (mirrors
  `MaterialDescriptorSetLayout()`'s own "created once, for this factory's
  entire lifetime" precedent) — e.g.
  `InstanceBufferDescriptorSetLayout()`. Decide the exact ownership home
  (`GpuResourceFactory` itself, vs. a new dedicated class alongside PHASE3's
  `CullingPipelines`) during implementation; `ask_questions` if it's not
  obviously one or the other once the actual code is in front of you.

### 3.2 — `cmake/CompileShaders.cmake` registration

Add `gte_add_shader(gte_core "src/Shaders/MeshInstanced.vert")` to
`CMakeLists.txt` alongside every other shader registration — confirm `.vert`
files needing a NEW descriptor-set-layout binding compile identically to
every existing one (glslc doesn't care about C++-side descriptor layouts at
all; this is purely a GLSL-side `layout(set=0, binding=0) readonly buffer ...`
declaration, no new CMake mechanism needed).

### 3.3 — `Renderer::SubmitIndirect()` + `FrameRecorder::IssueIndirectDrawCommand()`

```cpp
// Renderer.h - mirrors Submit()'s own doc comment shape.
void SubmitIndirect(const Pipeline& pipeline, const Mesh& mesh, VkBuffer indirectBuffer,
    VkDeviceSize indirectOffset, std::uint32_t maxDrawCount, VkBuffer countBuffer,
    VkDeviceSize countBufferOffset, VkDescriptorSet instanceBufferDescriptorSet,
    const Mat4& viewProjMatrix = Mat4::Identity());
```

- `countBuffer == VK_NULL_HANDLE` means "use the fixed-`maxDrawCount`,
  no-count fallback" (`vkCmdDrawIndexedIndirect`); non-null means "use the
  real counted path" (`vkCmdDrawIndexedIndirectCount`) — but ALSO gate the
  counted branch on `Renderer::SupportsDrawIndirectCount()` (3.4 below); if a
  caller passes a non-null `countBuffer` on a device that doesn't actually
  support it, assert in debug builds (a caller-side logic error — PHASE4/5's
  own batch-resource code is what decides which mode to use PER BATCH, once,
  based on this same capability query, never per-frame) and fall back safely
  in release.
- Binds `pipeline`, binds `instanceBufferDescriptorSet` at set 0 (unlike
  `Submit()`'s optional `materialDescriptorSet`, this is NOT optional for an
  indirect submit — every indirect draw goes through the new instanced
  vertex-layout path, which always needs its instance buffer bound), pushes
  `viewProjMatrix` at the SAME push-constant offset every other draw uses
  (leave the `model` half of the pushed 128 bytes as whatever garbage/zero is
  convenient — it's never read by `MeshInstanced.vert`), binds
  `mesh.VertexBuffer()`/`mesh.IndexBuffer()`, then issues EXACTLY ONE
  `vkCmdDrawIndexedIndirectCount`/`vkCmdDrawIndexedIndirect` call.
- Mirror `IssueDrawCommand()`'s "thin, stateless Vulkan-call wrapper" shape as
  a new `FrameRecorder::IssueIndirectDrawCommand()` static method
  `Renderer::SubmitIndirect()` calls directly while inside a
  `BeginGraphPassRecording()`/`EndGraphPassRecording()` bracket — mirrors
  `Submit()`'s own existing relationship with `IssueDrawCommand()` exactly.
  There is deliberately NO legacy/queued (`FrameRecorder::m_drawQueue`)
  fallback path for indirect draws — same "day-one render-graph-only, no
  backward-compat path to preserve" precedent `Renderer::Dispatch()` already
  established for compute.

### 3.4 — `VulkanDevice` `drawIndirectCount` capability probe

- Add a `VkPhysicalDeviceVulkan12Features features12{}` struct to
  `CreateLogicalDevice()` (`VulkanDevice.cpp`), chained via
  `features13.pNext = &features12` (or vice versa — confirm the correct chain
  direction against the Vulkan spec: `VkDeviceCreateInfo::pNext` points at the
  head of the chain, each struct's own `pNext` points at the next link).
  Request `drawIndirectCount = VK_TRUE`.
- **Before enabling it, actually check it's supported** — call
  `vkGetPhysicalDeviceFeatures2()` with the SAME `VkPhysicalDeviceVulkan12Features`
  struct (a separate query call, before `vkCreateDevice()`), and only request
  `drawIndirectCount = VK_TRUE` in the actual device-creation chain if the
  query reported it as available — enabling an unsupported feature makes
  `vkCreateDevice()` fail outright, which would be a real regression for
  every device that doesn't support it (this is exactly why Locked Design
  Decision 4 requires both branches to genuinely work).
- Cache the QUERIED (not merely requested) result in a new
  `bool m_supportsDrawIndirectCount` member, exposed via a new
  `bool SupportsDrawIndirectCount() const noexcept` accessor — mirrors
  `TimestampCapability()`'s exact "queried once in the constructor, cached,
  const accessor, never re-checked" shape.
- Thread this new accessor up through `Renderer::VulkanContextInfo` (mirrors
  how `timestampCapability` is already threaded through that same struct) and
  expose a matching `Renderer::SupportsDrawIndirectCount() const noexcept`.

### 3.5 — `DrawStats`'s "unknown/indirect" bucket

- `DrawStats.h` gains an explicit way to represent "an indirect draw
  happened, but the CPU does not know its real draw/triangle count without a
  blocking readback it must never perform" — mirroring
  `GpuSampleStatus::Absent`'s own "never fabricate a bare numeric 0" rule
  (`GpuTiming.h`). Concretely: add a new field,
  `std::uint32_t indirectDrawCount = 0` (a COUNT OF INDIRECT DRAW CALLS
  ISSUED, which the CPU genuinely does know — e.g. "1" per `SubmitIndirect()`
  call — NOT a count of objects/triangles drawn by them), and leave
  `drawCallCount`/`triangleCount` completely unaffected by an indirect draw
  (never incremented by `SubmitIndirect()`/`IssueIndirectDrawCommand()`).
  Document clearly, right next to the field, that a consumer (the Editor's
  "Profiler"/"Render Graph" panels) must display this SEPARATELY from
  `drawCallCount`/`triangleCount`, never summed into them, since they measure
  fundamentally different things (a known exact CPU count vs. "at least one
  indirect batch ran, real count is GPU-side only").
- Add a matching case to `tests/Renderer/DrawStatsTests.cpp`.

### 3.6 — Throwaway hand-driven smoke test (temporary, deleted before this phase is done)

Mirroring the compute-shader campaign's own Phase 2 "build it, verify it
manually with validation layers enabled, then delete the throwaway call site"
discipline: build a hand-written array of 2-3 `GpuCullingInstanceInput`
entries with distinct world matrices (no real culling shader exists yet —
this is CPU-authored test data, uploaded directly via
`CreateDeviceLocalBuffer()`), a hand-written `IndirectDrawCommand` array (also
CPU-authored, no compute shader involved), and confirm
`Renderer::SubmitIndirect()` actually draws 2-3 differently-positioned copies
of a test mesh correctly, with validation layers clean, via a temporary call
site (in `main.cpp` or a throwaway test harness — never committed as
production code). Delete this call site once manually confirmed; PHASE3
onward provides the REAL producer of this data.

### What We Will NOT Do (this phase)

- No real culling shader, no render-graph pass declaration, no ECS/
  `RenderSystem` involvement — this phase proves the RAW Vulkan mechanism
  only, exactly like the compute-shader campaign's own Phase 2 scope.
- No textured (`PositionNormalUv`) instanced variant (Locked Design Decision
  8, PHASE0).
- No change to `Submit()`'s own existing behavior/signature.

### Compile check

Fast, targeted incremental compile check only. Manually verify the throwaway
smoke test (3.6) with validation layers enabled before considering this phase
done, then delete it. Write `PHASE2_COMPLETION_REPORT.md` (record the actual
`Pipeline`/`GpuResourceFactory` API shape decided in 3.1, and which device
this was verified on reported `drawIndirectCount` support as — needed so
PHASE3 knows which of its two shader branches was actually exercised here).
Commit.
