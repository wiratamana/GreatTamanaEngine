# PHASE7 — Migrate Plugin Render Operation Registry + Final Migration Audit (Migration Batch 4)

**Parent:** `PHASE0_MASTER_STRATEGY.md` (read it first). Depends on `PHASE4`/`PHASE5`/`PHASE6`
(must run LAST among the migration batches — its own audit step needs every other migration
already landed).

---

## Step 1 — The Goal

1. Migrate the last three compute pipelines in
   `src/Core/Plugins/PluginRenderOperationRegistry.cpp` (`RegisterUberOp()`'s shared
   `RenderFeatureOps.comp` pipeline, `RegisterBoxBlur()`'s `BoxBlur.comp` pipeline, and the
   `RenderFeatureBlend.comp` pipeline — confirm the exact third registration function's name by
   reading the file directly, since only two were quoted in `PHASE0`'s own evidence table) onto
   reflection-based pipeline creation — **WITHOUT changing one single byte of the `gte_plugin_abi`
   ABI surface** (`IPluginRenderPassBuilder_v3`/`IPluginCommandRecorder`/`PluginRenderOpInfo`
   themselves are untouched; this is a purely internal, host-side implementation-detail change).
2. Run the final, campaign-wide audit: confirm, via a full-repository grep, that ZERO production
   `DescriptorSetLayoutBuilder`/manual-`VkPushConstantRange`-for-a-compute-pipeline call site
   remains anywhere outside this campaign's own new reflection internals and any explicitly
   documented, genuinely exotic escape-hatch case.

---

## Step 2 — The Situation

### 2.1 `PluginRenderOperationRegistry.cpp`'s real shape (confirmed, ~lines 39-69 for
`RegisterBoxBlur()`, mirrored by the other two registration functions)

```cpp
DescriptorSetLayoutBuilder layoutBuilder(m_device);
m_boxBlurDescriptorSetLayout =
    layoutBuilder.AddCombinedImageSampler(/*binding=*/0).AddStorageImage(/*binding=*/1).Build();

VkPushConstantRange pushConstantRange{};
pushConstantRange.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
pushConstantRange.offset = 0;
pushConstantRange.size = sizeof(std::uint32_t) * 2;

m_boxBlurPipeline.emplace(m_renderer.CreateComputePipeline("shaders/BoxBlur.comp.spv",
    std::vector<VkDescriptorSetLayout>{ m_boxBlurDescriptorSetLayout }, pushConstantRange));

PluginRenderOpInfo info;
info.id = "gte.builtin.box_blur";
// ...
info.computePipeline = &(*m_boxBlurPipeline);
info.descriptorSetLayout = m_boxBlurDescriptorSetLayout; // <-- still needs a real value after migration
InsertOp(std::move(info));
```

`PluginRenderOpInfo::descriptorSetLayout`/`computePipeline` are plain fields on a host-owned
struct — migrating the CONSTRUCTION of `m_boxBlurDescriptorSetLayout`/`m_boxBlurPipeline` (source
it from `m_boxBlurPipeline->ReflectedDescriptorSetLayout(0)` AFTER construction, exactly like
every prior migration batch) requires ZERO change to `PluginRenderOpInfo` itself, ZERO change to
anything reachable from `gte_plugin_abi/`, and ZERO change to how a real plugin `.dll` calls
`IPluginCommandRecorder::Dispatch(opId, ...)` — this registry is 100% host-side, per `AGENTS.md`'s
own "Plugin Architecture" section (*"`PluginRenderOperationRegistry` remains 100%
host-authored/curated"*).

### 2.2 `RegisterUberOp()` is called for MULTIPLE op codes sharing ONE pipeline
(`m_opsPipeline`/`m_opsDescriptorSetLayout`) — confirm, via reading the file in full, exactly how
many distinct calls share this one pipeline (the excerpt above shows `RegisterUberOp()` reads
`m_opsPipeline`/`m_opsDescriptorSetLayout` as ALREADY-BUILT member state — find the ONE place that
actually BUILDS them, e.g. an `EnsureBuiltinsRegistered()`-style method referenced in a comment
near line 87, and migrate THAT one construction site) — do not assume `RegisterUberOp()` itself
builds the pipeline; it clearly only USES an already-built one.

### 2.3 The actual `Dispatch()`/`SubmitIndirect`-shaped call site consuming these pipelines lives
elsewhere (likely `PluginRenderPassBuilderAdapter*.cpp` — confirm via `search_in_dir` for
`computePipeline`/`PluginRenderOpInfo` usage). Migrating THAT call site onto `CommandBuffer` is
OPTIONAL for this phase (lower priority than the pipeline-construction migration, which is this
phase's real, required deliverable) — attempt it ONLY if, once you are looking at the real
adapter code, it is a clean, low-risk, ABI-surface-safe change (the adapter is host-side code, so
using `CommandBuffer` internally is not an ABI violation by itself — but confirm the adapter
actually has access to a `PassContext`/`ctx.Cmd()`-shaped object at its real call site before
attempting this; if it only has a raw `VkCommandBuffer` + `Renderer&` at that point, either thread
a `CommandBuffer` through by constructing one directly, mirroring `PassContext::Cmd()`'s own
construction logic, or leave this call site on the raw `Renderer::Dispatch()` path and document
why in the completion report — this is explicitly a "nice to have, not required" sub-goal for
this phase, never a blocker).

---

## Step 3 — The Plan

1. Read `PluginRenderOperationRegistry.cpp` in full. Identify every real pipeline-construction
   site (expect 2-3 distinct methods: `RegisterBoxBlur()`, whichever method builds
   `m_opsPipeline`, and whichever method builds the `RenderFeatureBlend.comp` pipeline).
2. Migrate each one onto reflection-based `CreateComputePipeline(path)` + `ReflectedDescriptorSetLayout(0)`,
   per the established pattern from `PHASE4`/`PHASE5`/`PHASE6`.
3. Confirm `PluginRenderOpInfo::descriptorSetLayout`/`::computePipeline` are populated identically
   afterward (same type, same meaning — only the SOURCE of the `VkDescriptorSetLayout` value
   changed, from a local builder call to a reflected accessor).
4. **Destructor check — CONFIRMED, no fix needed here.** Unlike every `Ensure*Initialized()` class
   migrated in PHASE4/5/6, `PluginRenderOperationRegistry` has NO destructor at all today (confirmed:
   `search_in_dir` for `vkDestroyDescriptorSetLayout` across `src/Core/Plugins/` returns zero hits) —
   `m_opsDescriptorSetLayout`/`m_blendDescriptorSetLayout`/`m_boxBlurDescriptorSetLayout` (and the
   unrelated, never-migrated `m_blitDescriptorSetLayout` — see Step 6 below) simply leak for the
   entire process lifetime today, which is harmless in practice (this registry is a single,
   `Core`-owned instance living until process exit, exactly like `GpuResourceFactory`'s own
   `m_materialSetLayout`/`m_instanceBufferSetLayout` would too if their own `Destroy()` were ever
   skipped). There is therefore no double-destroy risk to fix in this file — do NOT add a new
   destructor as part of this migration; it is out of scope and unnecessary. One genuine, positive
   side effect worth noting in the completion report: after migrating `m_opsPipeline`/
   `m_blendPipeline`/`m_boxBlurPipeline` onto the reflection path, their own descriptor-set layouts
   become OWNED by each respective `ComputePipeline` instance (per `PHASE2`'s ownership design) and
   WILL genuinely be destroyed for the first time whenever that `ComputePipeline` is ever destroyed
   — a real (if inconsequential, given this registry's process-lifetime ownership) improvement over
   today's permanent leak, not a regression.
5. Attempt the OPTIONAL `CommandBuffer` migration of the real adapter dispatch call site per Step
   2.3, time/risk permitting — document the outcome either way.
6. **Final campaign-wide audit** (this phase's second deliverable): run
   `search_in_dir(path=src/, content="DescriptorSetLayoutBuilder", filter="*.cpp")` and
   `search_in_dir(path=src/, content="VkPushConstantRange", filter="*.cpp")` across the WHOLE
   `src/` tree. Confirm every remaining hit is one of:
   - Inside `src/Renderer/Vulkan/DescriptorSetLayoutBuilder.h/.cpp` itself (the class definition).
   - Inside `src/Renderer/ComputePipeline.cpp`/`GpuResourceFactory.cpp` (the reflection path's own
     internal implementation, built in `PHASE2`).
   - Inside `src/Renderer/GpuResourceFactory.cpp`'s pre-existing, UNRELATED
     `InstanceBufferDescriptorSetLayout()` construction (a GRAPHICS pipeline descriptor-set layout,
     never compute — explicitly out of scope for R2, which only ever concerned compute pipeline
     creation). **Note, confirmed by direct reading**: `MaterialDescriptorSetLayout()` does NOT
     actually appear in this grep at all — it builds `m_materialSetLayout` via a raw
     `vkCreateDescriptorSetLayout()` call, never `DescriptorSetLayoutBuilder` — so do not expect to
     find it here; if it DOES turn up, something else has changed and needs investigating, not
     assuming it is this same, already-known exception.
   - **Inside `src/Renderer/Atmosphere/AtmosphereSkyBackgroundRenderer.cpp` (confirmed, ~line 113)**
     — a hand-rolled GRAPHICS pipeline (`vkCreateGraphicsPipelines`, the `"DrawSkyBackground"` pass),
     never a `ComputePipeline`/`CreateComputePipeline()` call — its own
     `DescriptorSetLayoutBuilder(device).AddCombinedImageSampler(...)` call (binding 0, the Sky-View
     LUT sampler, fragment-stage-only) and its own hand-built `VkPushConstantRange` (fragment-stage
     push constants for `invViewProjection`/etc.) are both explicitly out of scope for R2 (compute
     pipelines only) — was NOT named in `PHASE0`'s own 8-file inventory (that inventory is
     specifically a `CreateComputePipeline()` call-site count, and this file never calls it), so
     confirm this distinction explicitly in the audit write-up rather than treating it as a missed
     migration target.
   - **Inside `src/Core/Plugins/PluginRenderOperationRegistry.cpp` itself (confirmed, ~line 122-124,
     `RegisterBlitFullscreen()`)** — `m_blitDescriptorSetLayout`/`blitLayoutBuilder` back
     `gte.builtin.blit_fullscreen`'s own GRAPHICS `Pipeline` (`m_blitPipeline`, a `Pipeline`, never a
     `ComputePipeline`) — this is the ONE `DescriptorSetLayoutBuilder` construction INSIDE THIS SAME
     FILE that must NOT be touched by this phase's own migration (Step 1/2 above only migrate the
     THREE compute pipelines: `m_opsPipeline`/`m_blendPipeline`/`m_boxBlurPipeline`) — call this out
     explicitly before starting Step 1/2 so the blit layout is never accidentally "migrated" into a
     `ComputePipeline` call it has no business being part of.
   - **Any plain GRAPHICS-pipeline `VkPushConstantRange` construction with zero `DescriptorSetLayoutBuilder`/
     `ComputePipeline` involvement at all** — confirmed, current examples: `src/Renderer/Pipeline.cpp`
     itself (the engine's own 128-byte model+viewProj vertex-stage push constant, every ordinary mesh
     pipeline's shared shape), `src/Editor/AssetPreviewMesh.cpp`, `src/Editor/BoneViewerWindow.cpp`, and
     `src/Editor/SceneGridRenderer.cpp` (each its own small, independent, vertex-stage-only graphics
     pipeline for an Editor preview/gizmo/grid draw). The `VkPushConstantRange` grep (unlike the
     `DescriptorSetLayoutBuilder` grep) has no way to distinguish compute from graphics on its own, so
     expect several hits like these that were never part of R2's scope at all (R2 only ever concerned
     COMPUTE pipeline creation) — confirm each such hit is genuinely a `vkCreateGraphicsPipelines()`-based
     `Pipeline`, not a `ComputePipeline`, and move on; this is not a migration target and does not need an
     `ask_questions` escalation merely for existing. Only a hit that is ALSO reachable from a
     `ComputePipeline`/`CreateComputePipeline()` call site is ever in scope.
   - A genuinely NEW, explicitly-documented exotic manual-override case this campaign's own
     migration work legitimately could not express via reflection (if any exists at all — none
     were identified during this campaign's own initial investigation, beyond the two confirmed,
     pre-existing GRAPHICS-pipeline exceptions named above; if PHASE2-7's real implementation work
     surfaced a genuinely new one, it must be called out here by name, with a one-paragraph
     justification, not silently left in place).
   Any OTHER hit is a real, unmigrated call site this phase must either migrate itself (if small)
   or escalate via `ask_questions` (if it reveals a call site this campaign's planning missed
   entirely).
7. Compile-check (incremental).
8. **Live verification**: run the Editor with the demo plugins loaded (`plugins/demo_render_feature_v3/`,
   per `AGENTS.md`'s own "Plugin Architecture" section) and confirm `gte.builtin.box_blur`/the
   uber-ops compute path/the blend compositing all still work, via `gte_send_request` capture and
   `GET /get_logs`.
9. Write `PHASE7_COMPLETION_REPORT.md` (include the full audit result — the exact list of every
   remaining `DescriptorSetLayoutBuilder`/`VkPushConstantRange` hit and its justification, per
   Step 6), commit.

### Acceptance bar for this phase

- All three (or however many are actually found in Step 1) `PluginRenderOperationRegistry`
  compute pipelines build via path-only `CreateComputePipeline()`.
- The campaign-wide audit (Step 6) is complete, and its result is captured verbatim in the
  completion report — this is the phase that proves R2's own explicit "do not leave two competing
  conventions live indefinitely" requirement is actually, verifiably satisfied, not merely
  assumed.
- Zero change to any file under `plugins/gte_plugin_abi/` or any plugin `.dll`'s own source.
- Live, HTTP-verified: both demo plugins still render correctly, zero new log warnings/errors.
