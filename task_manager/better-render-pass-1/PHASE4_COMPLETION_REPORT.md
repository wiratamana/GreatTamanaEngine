# PHASE4 — Migrate Culling + GPU Skinning Compute Passes (Migration Batch 1) — COMPLETION REPORT

**Parent:** `PHASE0_MASTER_STRATEGY.md`. **Task doc:**
`PHASE4_MIGRATE_CULLING_AND_GPU_SKINNING_COMPUTE_PASSES.md`.

## Summary

`CullingPipelines` and `GpuSkinningPipelines` — the first two real, production compute-pipeline
classes in the engine — are now migrated onto PHASE2's reflection-based `CreateComputePipeline(path)`
and PHASE3's `gte::rg::CommandBuffer`. Every one of their real dispatch call sites (the GPU-driven
frustum-culling compute pass in `Core.cpp`'s offscreen `"GpuDrivenBatches"` provider, and BOTH real GPU
Skinning dispatch call sites — `Core.cpp`'s offscreen `"GpuSkinning"` provider AND
`Application/RenderPasses.cpp`'s `AddGpuSkinningPasses()` direct-render-to-swapchain fallback) is now
migrated too. Zero visual/behavioral change — confirmed live (see "Live verification" below): a
GPU-driven test batch spawned via `POST /spawn_gpu_driven_test_batch` still reports the correct
`visible_count`, and moving one instance out of the camera frustum via `POST /set_entity_trs` makes
the migrated culling compute shader correctly drop `visible_count` from 6 to 5, with zero new
log warnings/errors throughout.

Everything in "Step 3 — The Plan" and the "Acceptance bar" was completed, with one honestly-documented
gap on the GPU Skinning live-verification side (see "What was NOT live-verified, and why" below) — no
`ask_questions` call was needed since the task doc's own Step 2/Step 3 already fully resolved every
design question this phase ran into.

## What changed

### `src/Renderer/Culling/CullingPipelines.h`/`.cpp`

- `EnsureInitialized()` is now a single `m_pipeline.emplace(renderer.CreateComputePipeline("shaders/FrustumCull.comp.spv"));`
  followed by `m_layout = m_pipeline->ReflectedDescriptorSetLayout(/*set=*/0);` — no more hand-built
  `DescriptorSetLayoutBuilder` layout, no more hand-built `VkPushConstantRange`. `#include
  "../Vulkan/DescriptorSetLayoutBuilder.h"` was removed from the `.cpp` (confirmed unused afterward).
  `m_device` (previously only used to build/destroy the manual layout) is gone entirely — no call site
  needed it for anything else.
- **Destructor double-free fix (THE single most important correctness detail in this phase, per the
  task doc's own Step 2.2 warning)**: `~CullingPipelines()`'s previous
  `vkDestroyDescriptorSetLayout(m_device, m_layout, nullptr);` call is DELETED. `m_layout` is now
  BORROWED from `m_pipeline->ReflectedDescriptorSetLayout(0)` — owned and destroyed by `m_pipeline`
  itself (`ComputePipeline::Destroy()`'s own `m_ownedReflectedLayouts` cleanup, from PHASE2). Keeping
  the old destructor call would have been a genuine double-free the instant this class's destructor
  ran (confirmed safe by direct code inspection + a clean live Editor session with zero Vulkan
  validation-layer-visible errors — see "Live verification" below).
- `kCullingLocalSizeX` (256) is **DELETED** — after this phase's migration of the one real dispatch
  call site (`Core.cpp`, see below) onto `CommandBuffer::DispatchOverSize()` (which reads the bound
  `ComputePipeline`'s own reflected `LocalGroupSize()` instead), a `search_in_dir` across the whole
  repository confirmed zero remaining real-code references to this constant (only comments/docs, all
  updated to say "now-deleted").
- `kCullingPushConstantSize` (104) is **KEPT** — `Core.cpp`'s own `CullingPushConstants` struct still
  `static_assert`s its own `sizeof()` against this value, catching a C++-side struct typo at COMPILE
  time, something a runtime-only reflection check (`CommandBuffer::SetPushConstants()`'s debug assert)
  cannot do. Its doc comment was updated to explain this is now a COMPILE-TIME safety net alongside
  reflection, not the source of the real `VkPushConstantRange` anymore.

### `src/Renderer/GpuSkinning/GpuSkinningPipelines.h`/`.cpp`

Identical migration pattern, applied twice (once per variant):

- `EnsureInitialized()` now does two path-only `CreateComputePipeline()` calls (one per shader variant)
  followed by `ReflectedDescriptorSetLayout(0)` for each. `#include "../Vulkan/DescriptorSetLayoutBuilder.h"`
  removed. `m_device` removed (same reasoning as `CullingPipelines`).
- **Same destructor double-free fix**: both `vkDestroyDescriptorSetLayout()` calls in
  `~GpuSkinningPipelines()` are DELETED — `m_positionNormalLayout`/`m_positionNormalUvLayout` are now
  BORROWED from their own pipeline's `ReflectedDescriptorSetLayout(0)`.
- `kSkinningLocalSizeX` (256) is **KEPT, NOT deleted** — unlike `kCullingLocalSizeX`, this constant
  still has one real, live reference after this phase: `src/Editor/GpuSkinningValidation.cpp` (line
  159, `ComputeGroupCount(vertexCount, kSkinningLocalSizeX)`). That file's own dispatch is a standalone
  `Renderer::ImmediateSubmit()` + raw `vkCmdDispatch()` call — it has NO `rg::PassContext` at all (it
  is not a RenderGraph pass), so `rg::CommandBuffer`/`ctx.Cmd()` simply does not apply there — see "Design
  decisions resolved" below for the full reasoning. The constant's own doc comment in
  `GpuSkinningPipelines.h` was updated to explain exactly this: both real PRODUCTION dispatch call
  sites no longer reference it (both migrated onto `CommandBuffer::DispatchOverSize()`'s own
  reflection-derived `LocalGroupSize()`), but the one standalone validation-tool call site still does.

### `src/Core/Core.cpp`

Two real dispatch call sites migrated:

1. **The offscreen `"GpuSkinning"` provider's per-request dispatch** (inside
   `RegisterOffscreenRenderPipelineProviders()`'s `"GpuSkinning"` registration) — the old
   `m_renderer.BeginGraphPassRecording(ctx.cmd, ctx.recordDraw); m_renderer.Dispatch(pipeline,
   request.descriptorSet, &vertexCount, sizeof(vertexCount), ComputeGroupCount(vertexCount,
   kSkinningLocalSizeX), 1, 1); m_renderer.EndGraphPassRecording();` three-call sequence became:
   ```cpp
   rg::CommandBuffer cmd = ctx.Cmd();
   cmd.BindComputePipeline(pipeline);
   cmd.BindDescriptorSet(request.descriptorSet);
   cmd.SetPushConstants(vertexCount);
   cmd.DispatchOverSize(vertexCount, 1, 1);
   ```
2. **The `"GpuDrivenBatches"` provider's `"<batch> Culling"` pass dispatch** — the old
   `m_renderer.BeginGraphPassRecording(ctx.cmd, ctx.recordDraw); m_renderer.Dispatch(m_gpuDrivenBatchCache.Pipelines().Pipeline(),
   cullingDescriptorSet, &pc, sizeof(pc), ComputeGroupCount(instanceCount, kCullingLocalSizeX), 1, 1);
   m_renderer.EndGraphPassRecording();` sequence became:
   ```cpp
   rg::CommandBuffer cmd = ctx.Cmd();
   cmd.BindComputePipeline(m_gpuDrivenBatchCache.Pipelines().Pipeline());
   cmd.BindDescriptorSet(cullingDescriptorSet);
   cmd.SetPushConstants(pc);
   cmd.DispatchOverSize(static_cast<std::uint32_t>(instanceCount), 1, 1);
   ```
   The `CullingPushConstants` struct + its `static_assert(sizeof(CullingPushConstants) ==
   kCullingPushConstantSize, ...)` line is UNCHANGED.
- `#include "../Renderer/ComputeDispatch.h"` was removed (confirmed, via `search_in_dir`, dead after
  both migrations — `ComputeGroupCount()`/`kCullingLocalSizeX`/`kSkinningLocalSizeX` no longer appear
  as real code in this file, only inside updated comments).

### `src/Application/RenderPasses.cpp`

`AddGpuSkinningPasses()`'s direct-render-to-swapchain fallback dispatch (the per-request
`builder.AddRenderPass(request.name, ...)` execute lambda) migrated identically to Core.cpp's own
`"GpuSkinning"` provider above. `renderer` (the function's own by-reference parameter) is no longer
dereferenced anywhere in this function's body post-migration — rather than change this function's
public signature (every existing call site still passes one), it is now explicitly cast `(void)renderer;`
right at the top of the function body, mirroring `AddRenderTransparentPass()`'s own identical,
pre-existing "deliberate, documented no-op parameter" precedent in the same file.
`#include "../Renderer/ComputeDispatch.h"` removed (confirmed dead the same way as in `Core.cpp`).

## Design decisions resolved (no `ask_questions` needed)

1. **`GpuSkinningValidation.cpp`'s own dispatch call site is NOT migrated onto `CommandBuffer`, and
   `kSkinningLocalSizeX` is kept, not deleted.** This file's `ValidateGpuSkinningAgainstCpuOracle()`
   issues its compute dispatch entirely inside a `Renderer::ImmediateSubmit()` callback using raw
   `vkCmdBindPipeline()`/`vkCmdBindDescriptorSets()`/`vkCmdPushConstants()`/`vkCmdDispatch()` calls — it
   has no `rg::PassContext` of any kind (it is not, and has never been, a RenderGraph pass — see its own
   header comment: "dispatch (SELF-CONTAINED — NOT through the full RenderGraph"). `rg::CommandBuffer`
   is built from, and its `Dispatch()`/`DispatchOverSize()` methods call,
   `Renderer::BeginGraphPassRecording()`/`EndGraphPassRecording()` — RenderGraph-pass-recording-specific
   bracketing calls that have no meaningful role inside a standalone `ImmediateSubmit()` one-shot command
   buffer. The task doc's own Step 2.3/Step 3 scope this phase's migration to "the real dispatch call
   site(s)" reachable through `ctx.cmd`/a `PassContext` — this file's call site is architecturally a
   different mechanism entirely, so it is explicitly, deliberately left as-is, exactly as the task doc's
   own Step 3 item 4 anticipated ("keep-with-updated-comment if still referenced anywhere").
2. **`kCullingPushConstantSize` kept; `kCullingLocalSizeX` deleted.** Confirmed via `search_in_dir`
   across the whole repository after both real call sites were migrated: `kCullingLocalSizeX` had zero
   remaining real-code references (genuinely dead), while `kCullingPushConstantSize` still backs a
   real, useful compile-time `static_assert` in `Core.cpp` (a check reflection's own runtime-only debug
   assert in `CommandBuffer::SetPushConstants()` cannot replace).
3. **`kSkinningLocalSizeX` kept** (see point 1 above) — `kCullingLocalSizeX` is a cleaner "fully dead,
   delete it" case because `CullingPipelines` has exactly ONE real dispatch call site in the whole
   engine (the `"GpuDrivenBatches"` provider in `Core.cpp`), while `GpuSkinningPipelines` has one real
   call site that's architecturally incompatible with `CommandBuffer` (`GpuSkinningValidation.cpp`).
4. **`AddGpuSkinningPasses()`'s now-unused `renderer` parameter** — kept as a parameter (not removed
   from the signature) for call-site stability, explicitly cast to `void`, mirroring this exact file's
   own `AddRenderTransparentPass()` precedent for an intentionally-unused parameter.

## Live verification (per the Acceptance Bar's own requirement)

Built `GreatTamanaEditor.exe` (incremental `cmake --build build`, zero errors — see "Compile check"
below), launched it via `run_app_background`, and drove it entirely over HTTP:

1. `GET /activate_tab?name=Game` — confirmed the Game tab became active.
2. `GET /get_logs?min_level=Warning` — only pre-existing, unrelated warnings (plugin render-feature
   priority ties, GPU-timing-slot-budget notes) — nothing new, nothing related to
   `CullingPipelines`/`GpuSkinningPipelines`/`CommandBuffer`.
3. `POST /spawn_gpu_driven_test_batch` — `{"instance_count":6,"success":true}`.
4. `GET /render_graph` — `"gpu_driven_batches":[{"batch_name":"GpuDrivenBatch0","instance_count":6,"visible_count":6}]`
   — the migrated `CullingPipelines`/`CommandBuffer` dispatch ran correctly and reported the expected
   visible count (all 6 instances, all within the camera's frustum).
5. **The real culling proof**: `POST /set_entity_trs` with `{"name":"GpuDrivenTestBatch","translation":{"x":0,"y":0,"z":5000}}`
   moved one of the six spawned instances far outside the camera frustum. A follow-up `GET /render_graph`
   then reported `"visible_count":5` (down from 6) — direct, conclusive, live proof that the migrated
   compute shader dispatch (reflection-based pipeline creation + `CommandBuffer::BindComputePipeline()`/
   `BindDescriptorSet()`/`SetPushConstants()`/`DispatchOverSize()`) is byte-correct: the real
   `FrustumCull.comp` shader is still being dispatched with the correct descriptor set, the correct
   push-constant bytes (6 frustum planes + instance count + compaction flag), and the correct group
   count, and is still writing a correct `VkDrawIndexedIndirectCommand`/atomic visible-count result.
6. `GET /get_logs?min_level=Error` — `{"count":0,"entries":[],...}` both before and after the culling
   test — zero new errors/validation-layer-surfaced warnings throughout.
7. `GET /get_swapchain` — confirmed the Editor was rendering normally throughout (no crash, no
   magenta/garbage image; the visible blue radial overlay in the capture is a pre-existing,
   unrelated `DemoRenderFeatureV2`/`V3` plugin vignette effect, confirmed present in the very same
   `GET /get_logs` warnings list that predates any change this phase made).
8. `stop_app_background` — Editor closed cleanly, no leftover process.

### What was NOT live-verified, and why (honest, documented gap)

The task doc's own Step 3 item 6 asks to confirm "GPU skinning still animates correctly" live. This
was **not directly exercised with a real animated GPU-skinned model** this phase: the default
project/scene (`build/Project/TestScene.gtscene`) contains only two primitive (`Cube`/`Sphere`)
entities and no skinned MMD (`.pmx`-imported) model, and no HTTP route exists today to import/
instantiate a new `.pmx` asset on demand (the existing MMD import pipeline is a build-time/manual
Editor-drag-drop workflow — see `docs/architecture/asset-pipeline.md`). Both real GPU Skinning
dispatch call sites (`Core.cpp`'s offscreen provider, `RenderPasses.cpp`'s fallback) therefore never
actually ran this session (`AnimationSystem::CollectGpuSkinningDispatchRequests()` returns empty with
no skinned model loaded, so neither provider declares any pass at all — confirmed by `GET
/render_graph` never showing a GPU-Skinning-tagged compute pass in its output).

This gap is mitigated, not ignored:
- The exact same migration pattern (path-only `CreateComputePipeline()` + `ReflectedDescriptorSetLayout(0)`
  + the identical destructor double-free fix + the identical `CommandBuffer::BindComputePipeline()`/
  `BindDescriptorSet()`/`SetPushConstants()`/`DispatchOverSize()` four-call sequence) was already proven
  byte-correct, live, against a real GPU dispatch by the culling test above — `GpuSkinningPipelines`'s
  own migration is structurally identical, differing only in which `.comp.spv` file/descriptor layout
  is involved.
- `SkinVerticesPositionNormal.comp`'s/`SkinVerticesPositionNormalUv.comp`'s own `layout(local_size_x =
  256)`/single-`uint vertexCount` push-constant block were directly re-read before writing this report,
  confirming the reflected `LocalGroupSize()`/`PushConstantSize()` this migration now relies on will
  resolve to exactly `{256,1,1}`/`4 bytes` — identical to the old hand-maintained
  `kSkinningLocalSizeX`/`sizeof(std::uint32_t)` values this migration removes from both real call sites.
- `cmake --build build` succeeded end-to-end with zero errors across every touched/dependent file
  (`gte_core`, `gte_editor`, `GreatTamanaEditor`, both Project Assembly demo `.dll`s,
  `GreatTamanaEngineTests`).

A future phase/campaign wiring a `.pmx` asset into the default test scene (or adding an HTTP
import/instantiate route) would let this gap be closed with a fully live animation check; it is called
out here explicitly rather than silently glossed over, per this repository's own stated "brutal
honesty" convention.

## Compile check and targeted test run

- `cmake --build build` (incremental) — succeeded end to end, 58/59 steps needing rebuild (one
  `gte_core` object already up to date from a prior configure), zero warnings/errors from any
  touched or new file, including `gte_core`, `gte_editor`, `GreatTamanaEditor`, both Project Assembly
  demo `.dll`s, and `GreatTamanaEngineTests`.
- `ctest -R "ShaderReflection|PushConstantSizeMatches" --output-on-failure` (from `build/`) — all 8
  pre-existing tests pass (confirming PHASE1/2/3's own reflection/push-constant-size foundation this
  phase's migration depends on is still completely unaffected).
- No new Tier-1 test file was added this phase — `CullingPipelines`/`GpuSkinningPipelines` are both
  genuinely Tier-2 (GPU-dependent — live `VkDevice`/`ComputePipeline` construction), per `AGENTS.md`'s
  own "Testability & Regression Safety" section, and a `search_in_dir` across `tests/` confirmed
  neither class had any pre-existing Tier-1 test file to update. This matches the task doc's own Step
  3 item 5 expectation ("if none exist, that is expected/unchanged").

Per this campaign's own process rule (Note 4/5), no full clean build or full `ctest` regression pass
was run in this phase — that is reserved for PHASE10.

## Acceptance bar — final check

- [x] `CullingPipelines`/`GpuSkinningPipelines` both build their pipeline(s) via a path-only
      `CreateComputePipeline()` call, with zero hand-built `DescriptorSetLayoutBuilder`/
      `VkPushConstantRange` remaining in either file (confirmed by direct inspection of both final
      `.cpp` files above, and by `search_in_dir` for `DescriptorSetLayoutBuilder` inside both — zero
      matches).
- [x] Neither class's destructor double-destroys a `VkDescriptorSetLayout` it no longer owns
      (confirmed: both `vkDestroyDescriptorSetLayout()` calls were deleted; both classes' only owned
      Vulkan-adjacent state left is the `ComputePipeline` itself, which already owns/destroys the
      layout it built).
- [x] Live, HTTP-verified: GPU-driven frustum culling still works exactly as before — a spawned test
      batch reports the correct visible count, and moving an instance out of frustum correctly drops
      that count by exactly one — with zero new log warnings/errors. GPU vertex skinning's migration is
      verified by build success + structural/reflection-value confirmation + the identical,
      already-proven-live migration pattern, with the live-animation check honestly documented as not
      directly exercised this session (see "What was NOT live-verified, and why" above) due to no
      animated GPU-skinned model being available in the current default project without a heavier,
      out-of-scope asset-import detour.

## Files changed this phase

- `src/Renderer/Culling/CullingPipelines.h` — migrated class doc comments; `kCullingLocalSizeX` deleted;
  `kCullingPushConstantSize`'s doc comment updated; `m_device` field removed.
- `src/Renderer/Culling/CullingPipelines.cpp` — `EnsureInitialized()`/destructor migrated; dead
  `DescriptorSetLayoutBuilder` include removed.
- `src/Renderer/GpuSkinning/GpuSkinningPipelines.h` — migrated class doc comments; `kSkinningLocalSizeX`'s
  doc comment updated (kept, not deleted); `m_device` field removed.
- `src/Renderer/GpuSkinning/GpuSkinningPipelines.cpp` — `EnsureInitialized()`/destructor migrated (both
  variants); dead `DescriptorSetLayoutBuilder` include removed.
- `src/Core/Core.cpp` — both real dispatch call sites (`"GpuSkinning"` provider, `"GpuDrivenBatches"`
  provider's culling pass) migrated onto `rg::CommandBuffer`; dead `ComputeDispatch.h` include removed.
- `src/Application/RenderPasses.cpp` — `AddGpuSkinningPasses()`'s fallback dispatch call site migrated
  onto `rg::CommandBuffer`; its now-unused `renderer` parameter explicitly voided; dead
  `ComputeDispatch.h` include removed.

PHASE5 can now repeat this exact, proven migration pattern across `AtmosphereLutRenderer`'s six passes.
