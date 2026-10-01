# PHASE4 — Migrate Culling + GPU Skinning Compute Passes (Migration Batch 1)

**Parent:** `PHASE0_MASTER_STRATEGY.md` (read it first). Depends on `PHASE3` (`CommandBuffer`,
reflection-based `CreateComputePipeline`).

---

## Step 1 — The Goal

Migrate the FIRST real production compute passes onto PHASE2's reflection-based pipeline creation
and PHASE3's `CommandBuffer`, with **zero visual/behavioral change** — this is the phase that
proves the whole migration pattern works end-to-end, before repeating it at scale in PHASE5/6/7.
Targets (chosen because they are the smallest, most recently-written, most structurally-similar
pair in the engine — `CullingPipelines.h`'s own header comment states it "Mirrors
`GpuSkinningPipelines.h` EXACTLY"):

1. `src/Renderer/Culling/CullingPipelines.h/.cpp` (1 pipeline, `FrustumCull.comp`) — production,
   always-on, Game-View-only GPU-driven culling path (render-pass-5 campaign).
2. `src/Renderer/GpuSkinning/GpuSkinningPipelines.h/.cpp` (2 pipelines,
   `SkinVerticesPositionNormal.comp`/`SkinVerticesPositionNormalUv.comp`) — production, opt-in
   (per-model, runtime-switchable) GPU skinning path.

---

## Step 2 — The Situation

### 2.1 `CullingPipelines::EnsureInitialized()` today (confirmed, `CullingPipelines.cpp`)

```cpp
void CullingPipelines::EnsureInitialized(Renderer& renderer)
{
    if (IsInitialized()) return;
    const Renderer::VulkanContextInfo context = renderer.GetVulkanContextInfo();
    m_device = context.device;

    VkPushConstantRange pushConstantRange{};
    pushConstantRange.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
    pushConstantRange.offset = 0;
    pushConstantRange.size = kCullingPushConstantSize; // = 104, hand-restated

    DescriptorSetLayoutBuilder layoutBuilder(m_device);
    m_layout = layoutBuilder.AddStorageBuffer(/*binding=*/0)
                   .AddStorageBuffer(/*binding=*/1)
                   .AddStorageBuffer(/*binding=*/2)
                   .Build();

    m_pipeline.emplace(renderer.CreateComputePipeline(
        "shaders/FrustumCull.comp.spv", std::vector<VkDescriptorSetLayout>{ m_layout }, pushConstantRange));
}
```

`m_layout` is a class member, exposed via `DescriptorSetLayout()`, and destroyed in
`~CullingPipelines()`. `kCullingPushConstantSize`/`kCullingLocalSizeX` are file-scope constants in
`CullingPipelines.h`, restated by hand from `Shaders/FrustumCull.comp`'s own GLSL.

### 2.2 What the migrated version looks like

```cpp
void CullingPipelines::EnsureInitialized(Renderer& renderer)
{
    if (IsInitialized()) return;
    const Renderer::VulkanContextInfo context = renderer.GetVulkanContextInfo();
    m_device = context.device;

    m_pipeline.emplace(renderer.CreateComputePipeline("shaders/FrustumCull.comp.spv")); // path only
    m_layout = m_pipeline->ReflectedDescriptorSetLayout(/*set=*/0); // still available to callers via DescriptorSetLayout()
}
```

`m_device`/`m_layout` stay as class members (`DescriptorSetLayout()`'s public accessor keeps
working unmodified — nothing outside this class needs to change) — but `m_layout` is now BORROWED
from `m_pipeline` (owned by `ComputePipeline` itself per PHASE2's ownership design), NOT built and
owned locally — **`~CullingPipelines()`'s existing `vkDestroyDescriptorSetLayout(m_device,
m_layout, nullptr);` call must be DELETED**, since `m_layout` is no longer this class's own handle
to destroy (`ComputePipeline`'s own destructor now owns that responsibility). Getting this
double-free/dangling-destroy exactly right is the single most important correctness detail in
this whole migration batch — verify it explicitly for EVERY migrated class in this phase (and
PHASE5/6/7).

`kCullingPushConstantSize`/`kCullingLocalSizeX` — once every real call site that used to reference
them is migrated to instead read `m_pipeline->PushConstantSize()`/`m_pipeline->LocalGroupSize()`
live off the pipeline, these two file-scope constants become genuinely dead. Confirm (via
`search_in_dir` across the whole repo) that nothing else references them before deleting them —
if anything still does (e.g. a test), either migrate that reference too or leave the constant in
place with an updated comment explaining it is now redundant-but-kept-for-that-one-caller.

### 2.3 The real dispatch call site (wherever `CullingPipelines::Pipeline()`/`DescriptorSetLayout()`
are actually consumed — locate this via `search_in_dir` for `CullingPipelines` usage outside its
own file; PHASE4's own strategy document, `render-pass-5`, names the consumer as the per-batch
GPU-driven-batch resource cache, `src/Renderer/Culling/GpuDrivenBatchCache.h/.cpp`, and the real
pass declaration inside `Application.cpp`'s `"GpuDrivenBatches"` provider). Find the EXACT
`ctx.cmd`/`renderer.Dispatch(...)` call site(s) issuing the real per-frame culling dispatch and
migrate them onto `ctx.Cmd().BindComputePipeline(cullingPipelines.Pipeline()).SetPushConstants(pushConstants)
.BindDescriptorSet(theRealDescriptorSet).DispatchOverSize(instanceCount, 1, 1)` (or whichever
exact shape `CommandBuffer` ended up with in PHASE3 — confirm `DispatchOverSize`'s 1D-vs-3D
calling convention matches how `ComputeGroupCount()` is invoked here today, since frustum culling
dispatches over a flat instance count, not a 2D/3D extent — `DispatchOverSize(instanceCount, 1,
1)` should degrade correctly to the existing 1D `ComputeGroupCount()` call via
`ComputeGroupCount3D()`'s own per-axis application).

### 2.4 `GpuSkinningPipelines` mirrors the exact same shape, twice

`EnsureInitialized()` builds TWO layouts (`m_positionNormalLayout`/`m_positionNormalUvLayout`) and
TWO pipelines the identical way — apply the EXACT same migration pattern to both, and apply the
EXACT same "delete the now-wrong destructor `vkDestroyDescriptorSetLayout()` calls" fix to
`~GpuSkinningPipelines()`.

---

## Step 3 — The Plan

1. Migrate `CullingPipelines::EnsureInitialized()` per Step 2.2. Delete the now-dead
   `vkDestroyDescriptorSetLayout()` call in `~CullingPipelines()`. Delete
   `#include "../Vulkan/DescriptorSetLayoutBuilder.h"` from `CullingPipelines.cpp` if nothing else
   in that file still needs it (confirm via a final read of the file after this edit).
2. Migrate the real culling dispatch call site (Step 2.3) onto `CommandBuffer`.
3. Repeat steps 1-2 for `GpuSkinningPipelines` (both pipelines) and its own real dispatch call
   site(s) — locate via `search_in_dir` for `GpuSkinningPipelines` usage (expect a per-model GPU
   skinning resource cache class, per `docs/conventions/gpu-vertex-skinning.md`).
4. Decide, and document in the completion report, the disposition of `kCullingPushConstantSize`/
   `kCullingLocalSizeX`/`kSkinningLocalSizeX` (delete if genuinely dead, keep-with-updated-comment
   if still referenced anywhere — e.g. a test file).
5. Compile-check (incremental). Run any existing Tier-1 tests covering these two classes (search
   `tests/` for `CullingPipelines`/`GpuSkinningPipelines` test files — if none exist, that is
   expected/unchanged, since these are Tier-2 GPU-dependent classes per `AGENTS.md`).
6. **Live visual verification** (this phase is the pattern-proving phase — do this thoroughly):
   `run_app_background` the Editor (or use whatever launch convention this repo's own
   `BUILDING.md`/prior campaign reports establish), use `gte_send_request` to capture the Game
   View before/after (or compare against a prior campaign's own documented baseline screenshot if
   one exists in `task_manager/render-pass-5/`), confirming GPU-driven culling still visibly works
   (objects still cull correctly when moved out of frustum — reuse `render-pass-5`'s own
   `POST /spawn_gpu_driven_test_batch` HTTP endpoint if it still exists, per that campaign's own
   documented live-verification methodology) and GPU skinning still animates correctly. Use
   `GET /get_logs` to confirm zero new `GTE_LOG_ERROR`/validation-layer-surfaced warnings appear.
   `stop_app_background` when done.
7. Write `PHASE4_COMPLETION_REPORT.md` (include the exact before/after call-site diffs for both
   classes, and explicitly restate the destructor double-free fix as its own, separately-called-out
   finding — future phases will hit the identical fix, so document it clearly enough that PHASE5/6/7
   can just point back at this report's own wording rather than re-deriving it), commit.

### Acceptance bar for this phase

- `CullingPipelines`/`GpuSkinningPipelines` both build their pipeline(s) via a path-only
  `CreateComputePipeline()` call, with zero hand-built `DescriptorSetLayoutBuilder`/
  `VkPushConstantRange` remaining in either file.
- Neither class's destructor double-destroys a `VkDescriptorSetLayout` it no longer owns.
- Live, HTTP-verified: GPU-driven frustum culling and GPU vertex skinning both still work exactly
  as before, zero new log warnings/errors.
