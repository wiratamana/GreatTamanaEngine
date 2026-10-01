# PHASE7 — Migrate Plugin Render Operation Registry + Final Migration Audit (Migration Batch 4) — COMPLETION REPORT

**Parent:** `PHASE0_MASTER_STRATEGY.md`. **Task doc:**
`PHASE7_MIGRATE_PLUGIN_RENDER_OPERATION_REGISTRY.md`. **Pattern proven by:** `PHASE4_COMPLETION_REPORT.md`/
`PHASE5_COMPLETION_REPORT.md`/`PHASE6_COMPLETION_REPORT.md`.

## Summary

All three real `PluginRenderOperationRegistry.cpp` compute pipelines — the shared "uber ops" pipeline
(`RenderFeatureOps.comp`, built in `EnsureBuiltinsRegistered()`), `RegisterBoxBlur()`'s `BoxBlur.comp`
pipeline, and the shared blend pipeline (`RenderFeatureBlend.comp`, also built in
`EnsureBuiltinsRegistered()`) — now build via a path-only, reflection-based
`m_renderer.CreateComputePipeline(path)` call (PHASE2), with their `VkDescriptorSetLayout` sourced from
each pipeline's own `ReflectedDescriptorSetLayout(/*set=*/0)` afterward, exactly matching the pattern
PHASE4/5/6 already established. Zero change to `plugins/gte_plugin_abi/` or any plugin `.dll`'s own
source — this is a purely internal, host-side implementation-detail change, confirmed by `git status`/the
file list below.

As an OPTIONAL bonus (per the task doc's own Step 5 "nice to have, not required" framing), the real
`Dispatch()`-shaped consumer call site in `PluginRenderPassBuilderAdapter_v3.cpp`
(`CommandRecorderAdapter::Dispatch()`) was also migrated onto PHASE3's `gte::rg::CommandBuffer` — this
adapter genuinely holds a real `rg::PassContext& m_ctx` member (confirmed by direct reading of
`PluginRenderPassBuilderAdapter_v3.h`), so `m_ctx.Cmd()` was directly available, making this a clean,
low-risk, ABI-surface-safe change exactly as the task doc anticipated.

The final, campaign-wide audit (Step 6) is complete — see "Final audit result" below. One small, genuinely
pre-existing (not introduced by this campaign), zero-risk dead-code item was found and cleaned up along the
way (see "Incidental cleanup" below).

No `ask_questions` call was needed — every design fork this phase ran into was already resolved either by
the task doc itself or by PHASE4/5/6's own established precedent.

## What changed

### `src/Core/Plugins/PluginRenderOperationRegistry.cpp`

- **`RegisterBoxBlur()`**: replaced the hand-built `DescriptorSetLayoutBuilder` (`AddCombinedImageSampler(0)`
  + `AddStorageImage(1)`) + manual `VkPushConstantRange` (8 bytes, `sizeof(std::uint32_t) * 2`) with
  `m_boxBlurPipeline.emplace(m_renderer.CreateComputePipeline("shaders/BoxBlur.comp.spv"));` followed by
  `m_boxBlurDescriptorSetLayout = m_boxBlurPipeline->ReflectedDescriptorSetLayout(/*set=*/0);`.
- **`EnsureBuiltinsRegistered()`**: migrated BOTH remaining pipeline-construction sites in this one method:
  - The shared "uber ops" pipeline: replaced the hand-built `DescriptorSetLayoutBuilder`
    (`AddStorageImage(0)`) + manual `VkPushConstantRange` (`sizeof(RenderFeatureOpsPushConstants)`, 64
    bytes) with `m_opsPipeline.emplace(m_renderer.CreateComputePipeline("shaders/RenderFeatureOps.comp.spv"));`
    followed by `m_opsDescriptorSetLayout = m_opsPipeline->ReflectedDescriptorSetLayout(/*set=*/0);`.
  - The shared blend pipeline: replaced the hand-built `DescriptorSetLayoutBuilder`
    (`AddCombinedImageSampler(0)` + `AddCombinedImageSampler(1)` + `AddStorageImage(2)`) + manual
    `VkPushConstantRange` (`sizeof(RenderFeatureBlendPushConstants)`, 16 bytes) with
    `m_blendPipeline.emplace(m_renderer.CreateComputePipeline("shaders/RenderFeatureBlend.comp.spv"));`
    followed by `m_blendDescriptorSetLayout = m_blendPipeline->ReflectedDescriptorSetLayout(/*set=*/0);`.
- **`RegisterBlitFullscreen()` left completely untouched**, exactly per the task doc's own explicit
  instruction (Step 6's dedicated call-out): `m_blitDescriptorSetLayout`/`blitLayoutBuilder` back a GRAPHICS
  `Pipeline` (`m_blitPipeline`), never a `ComputePipeline` — out of scope for this whole campaign (R2 only
  ever concerned compute pipeline creation).
- **Destructor check — confirmed, no fix needed**, exactly as the task doc predicted:
  `PluginRenderOperationRegistry` has no destructor at all (re-confirmed by `search_in_dir` for
  `vkDestroyDescriptorSetLayout`/`~PluginRenderOperationRegistry` — zero hits in this file, before or after
  this phase's own changes). `m_opsDescriptorSetLayout`/`m_blendDescriptorSetLayout`/
  `m_boxBlurDescriptorSetLayout` now become OWNED by each respective `ComputePipeline` instance (PHASE2's
  ownership design) and will genuinely be destroyed for the first time whenever that `ComputePipeline` is
  ever destroyed — a real (if inconsequential, given this registry's process-lifetime ownership)
  improvement over the prior permanent leak, not a regression. `m_blitDescriptorSetLayout` (the ONE
  genuinely untouched layout, backing the graphics `m_blitPipeline`) still leaks for the process lifetime,
  unchanged — out of scope, as documented above.
- `#include "../../Renderer/Vulkan/DescriptorSetLayoutBuilder.h"` is **kept** — still genuinely required by
  `RegisterBlitFullscreen()`'s own untouched `DescriptorSetLayoutBuilder blitLayoutBuilder(m_device);` call.

### `src/Core/Plugins/PluginRenderPassBuilderAdapter_v3.cpp` (optional bonus, per task doc Step 2.3/Step 5)

- `CommandRecorderAdapter::Dispatch()`: replaced
  `Renderer& renderer = m_operationRegistry.GetRenderer(); renderer.BeginGraphPassRecording(m_ctx.cmd,
  m_ctx.recordDraw); renderer.Dispatch(*op->computePipeline, descriptorSet, scratch,
  static_cast<std::uint32_t>(op->maxParamBytes), groupsX, groupsY, groupsZ); renderer.EndGraphPassRecording();`
  with `rg::CommandBuffer cmd = m_ctx.Cmd(); cmd.BindComputePipeline(*op->computePipeline);
  cmd.BindDescriptorSet(descriptorSet); cmd.SetPushConstants(scratch,
  static_cast<std::uint32_t>(op->maxParamBytes)); cmd.Dispatch(groupsX, groupsY, groupsZ);` — `Dispatch()`
  (not `DispatchOverSize()`) was the correct choice here since `groupsX`/`groupsY`/`groupsZ` are already
  exact, caller-supplied group counts (from a real plugin's own `IPluginCommandRecorder::Dispatch()` call),
  never a texture extent needing ceiling-division.
- `CommandRecorderAdapter::DrawFullscreenTriangle()` is **completely untouched** — it issues a raw, direct
  `vkCmdBindPipeline`/`vkCmdBindDescriptorSets`/`vkCmdBindVertexBuffers`/`vkCmdPushConstants`/`vkCmdDraw`
  sequence against `m_ctx.cmd` with NO `BeginGraphPassRecording()`/`EndGraphPassRecording()` bracket at all
  (mirrors `AtmosphereSkyBackgroundRenderer::Draw()`'s own established precedent) — a GRAPHICS draw, not a
  `ComputePipeline`/compute dispatch, genuinely out of scope for this whole campaign (R1's `CommandBuffer`
  does have a `Draw()` method, but migrating this call site was never asked for by the task doc, and this
  path has its own pre-existing, already-correct recording discipline that doesn't need `CommandBuffer`'s
  bracket-management benefit at all, since it never opens one in the first place).
- `Renderer& renderer = m_operationRegistry.GetRenderer();` is now dead in this file (confirmed —
  `search_in_dir` for `GetRenderer` across `src/Core/Plugins/*.cpp` returns zero hits post-migration); the
  `PluginRenderOperationRegistry::GetRenderer()` accessor itself is left in place (a small, header-only,
  zero-cost `noexcept` inline method, already public API documented for exactly this kind of use — removing
  a public accessor is out of scope for a migration phase and could affect future/other callers).

## Incidental cleanup (found during the mandated Step 6 audit, not a migration target itself)

`src/Core/Plugins/RenderFeatureCompositor.cpp` carried a stale
`#include "../../Renderer/Vulkan/DescriptorSetLayoutBuilder.h"` with **zero real code reference** to
`DescriptorSetLayoutBuilder` anywhere in that file (confirmed via `search_in_dir` — only the `#include`
itself matched). This is a pre-existing leftover from BEFORE the `editor-core-separation-9` campaign moved
the ops/blend pipeline construction out of `RenderFeatureCompositor` and into
`PluginRenderOperationRegistry` (see `PluginRenderOperationRegistry.h`'s own header comment: *"This is ALSO
the new PERMANENT home of the two pipelines RenderFeatureCompositor used to own directly"*) — it predates
this whole `better-render-pass-1` campaign and was never touched by PHASE2-6 either. Since this dead include
was directly surfaced by this phase's own mandated full-repository `DescriptorSetLayoutBuilder` grep (Step
6) and removing an unused `#include` with zero real-code reference is a genuinely zero-risk, trivial
cleanup (confirmed: `ComputeDispatch.h`, the file's other Renderer-side include, IS still used —
`ComputeGroupCount3D()` — so only the one dead line was removed), it was deleted rather than left in place
to confuse a future reader of the audit result. This does **not** change this file's behavior in any way.

## Final campaign-wide audit (Step 6) — captured verbatim

`search_in_dir(path=src/, content="DescriptorSetLayoutBuilder", filter="*.cpp")` (real, non-comment hits
only, after this phase's own changes):

| File | Hit | Classification |
|---|---|---|
| `Core/Plugins/PluginRenderOperationRegistry.cpp:120` | `DescriptorSetLayoutBuilder blitLayoutBuilder(m_device);` | **Documented exception** — `RegisterBlitFullscreen()`'s own GRAPHICS `Pipeline` (`m_blitPipeline`), never a `ComputePipeline` — explicitly called out by this phase's own task doc, Step 6, as the ONE construction inside this same file that must not be touched. |
| `Renderer/Atmosphere/AtmosphereSkyBackgroundRenderer.cpp:113` | `DescriptorSetLayoutBuilder layoutBuilder(device);` | **Documented exception** — a hand-rolled GRAPHICS pipeline (`vkCreateGraphicsPipelines`, the `"DrawSkyBackground"` pass), never a `ComputePipeline`/`CreateComputePipeline()` call. Confirmed, matches PHASE0's own prediction exactly. |
| `Renderer/ComputePipeline.cpp:52` | `DescriptorSetLayoutBuilder layoutBuilder(device);` | **Documented exception** — the reflection path's own internal implementation (built in PHASE2). |
| `Renderer/GpuResourceFactory.cpp:116` | `DescriptorSetLayoutBuilder instanceBufferLayoutBuilder(m_device);` | **Documented exception** — `InstanceBufferDescriptorSetLayout()`, a GRAPHICS pipeline descriptor-set layout (GPU-driven instanced rendering), never compute — explicitly out of scope for R2. |
| `Renderer/Vulkan/DescriptorSetLayoutBuilder.cpp` (multiple) | the class's own method definitions | **Documented exception** — the class definition itself. |
| `Core/Plugins/RenderFeatureCompositor.cpp:12` | `#include "../../Renderer/Vulkan/DescriptorSetLayoutBuilder.h"` | **Found, investigated, and removed** (see "Incidental cleanup" above) — a genuinely dead, pre-existing, zero-risk leftover include with no real code reference; not a migration target, not a hazard, now cleaned up. |

`search_in_dir(path=src/, content="VkPushConstantRange", filter="*.cpp")` (real, non-comment hits only):

| File | Hit | Classification |
|---|---|---|
| `Editor/AssetPreviewMesh.cpp:216` | `VkPushConstantRange pushConstantRange{};` | **Documented exception** — a plain GRAPHICS-pipeline push-constant construction (Editor preview mesh), zero `DescriptorSetLayoutBuilder`/`ComputePipeline` involvement. |
| `Editor/BoneViewerWindow.cpp:291` | `VkPushConstantRange pushConstantRange{};` | **Documented exception** — same shape, Bone Viewer gizmo graphics pipeline. |
| `Editor/SceneGridRenderer.cpp:170` | `VkPushConstantRange pushConstantRange{};` | **Documented exception** — same shape, Scene grid graphics pipeline. |
| `Renderer/Atmosphere/AtmosphereSkyBackgroundRenderer.cpp:205` | `VkPushConstantRange pushConstantRange{};` | **Documented exception** — same hand-rolled graphics pipeline named above. |
| `Renderer/Pipeline.cpp:202` | `VkPushConstantRange pushConstantRange{};` | **Documented exception** — the engine's own shared 128-byte model+viewProj vertex-stage push constant, every ordinary mesh `Pipeline`'s shared shape. |
| `Renderer/ComputePipeline.cpp:12,25,86` | parameter/local declarations | **Documented exception** — the reflection path's own internal implementation (PHASE2) / the manual-path escape hatch's own parameter type. |
| `Renderer/GpuResourceFactory.cpp:328` / `Renderer/Renderer.cpp:352` | `std::optional<VkPushConstantRange> pushConstantRange` | **Documented exception** — `CreateComputePipeline()`'s own pass-through parameter, forwarded unchanged to `ComputePipeline`'s constructor; this IS the manual-override escape hatch's own plumbing, not a new hand-built call site. |

**Audit conclusion**: zero remaining production `DescriptorSetLayoutBuilder`/manual-`VkPushConstantRange`-
for-a-compute-pipeline call site exists anywhere in `src/` outside this campaign's own reflection internals
(`ComputePipeline.cpp`, `GpuResourceFactory.cpp`/`Renderer.cpp`'s pass-through parameters) and the explicitly
documented, genuinely exotic/out-of-scope cases this phase (`RegisterBlitFullscreen()`) and PHASE0's own
evidence table already named (`AtmosphereSkyBackgroundRenderer.cpp`, `GpuResourceFactory.cpp`'s
`InstanceBufferDescriptorSetLayout()`, and four plain graphics-pipeline `VkPushConstantRange` constructions
with zero compute involvement at all). R2's own explicit "do not leave two competing conventions live
indefinitely" requirement is confirmed satisfied by direct, full-repository grep, not merely assumed.

## Design decisions resolved (no `ask_questions` needed)

1. **All three real `PluginRenderOperationRegistry` compute pipelines were migrated** (the task doc's own
   Step 1 correctly anticipated "2-3 distinct methods" — this phase confirmed, by direct reading, exactly
   three: `RegisterBoxBlur()`'s own pipeline, plus the two built directly inside
   `EnsureBuiltinsRegistered()` itself — the shared "uber ops" and shared blend pipelines — with
   `RegisterUberOp()` itself confirmed to only ever READ the already-built `m_opsPipeline`/
   `m_opsDescriptorSetLayout`, never build them, exactly as Step 2.2 of the task doc predicted).
2. **The OPTIONAL `CommandBuffer` migration of `CommandRecorderAdapter::Dispatch()` WAS attempted and
   landed** — `m_ctx` is a genuine `rg::PassContext&` member (confirmed by direct reading of the adapter's
   own header), so `m_ctx.Cmd()` was directly available with zero new plumbing, making this the "clean,
   low-risk, ABI-surface-safe change" case the task doc's own Step 2.3 described, not the "only a raw
   `VkCommandBuffer` + `Renderer&`" harder case.
3. **`CommandRecorderAdapter::DrawFullscreenTriangle()` was NOT migrated onto `CommandBuffer`** — it is a
   GRAPHICS draw issued via raw `vkCmd*` calls with no `BeginGraphPassRecording()`/`EndGraphPassRecording()`
   bracket at all (mirroring `AtmosphereSkyBackgroundRenderer::Draw()`'s own precedent) — genuinely out of
   this phase's scope (R2/R1's `CommandBuffer` work concerned compute pipelines/dispatch primarily; this
   call site's own pre-existing recording discipline already works correctly and doesn't need
   `CommandBuffer`'s bracket-management benefit, since it never opens one).
4. **The stale `RenderFeatureCompositor.cpp` `#include` was removed** — see "Incidental cleanup" above; a
   zero-risk, genuinely dead leftover surfaced by this phase's own mandated audit, not a new migration
   target.

## Live verification (per the Acceptance Bar's own requirement)

Built `GreatTamanaEditor.exe` (incremental `cmake --build build`, zero errors), launched it via
`run_app_background` with every demo plugin (including `plugins/demo_render_feature_v3/` and
`demo_render_feature_v3_second/`) present in `build/plugins/`, and drove it entirely over HTTP:

1. `GET /get_logs?min_level=Error` — `{"count":0,...}` immediately after startup.
2. `GET /activate_tab?name=Game` — confirmed the Game tab became active.
3. `GET /get_swapchain` — a real, correct rendered frame: a salmon/pink background with a visibly
   **soft-edged, radially-blended blue vignette blob** — direct, visual, conclusive proof that ALL THREE
   migrated pipelines are working together correctly, live: `gte.builtin.radial_vignette` (the migrated
   shared "uber ops" `RenderFeatureOps.comp` pipeline) draws the blue radial shape into a plugin's private
   target, `gte.builtin.box_blur` (the migrated `BoxBlur.comp` pipeline, driven through this phase's own
   newly-migrated `CommandBuffer` dispatch call site) downsamples/blurs it (confirmed via the
   `"DemoRenderFeatureV3_Downsample"`/`"DemoRenderFeatureV3_UpsamplePresent"` pass names appearing in the
   log, below), and the migrated shared blend pipeline (`RenderFeatureBlend.comp`,
   `"DemoRenderFeatureV3_Game_Blend"`) composites the result back over the scene — the soft, blurred edge
   on the vignette is the direct visual signature of the blur pipeline actually running correctly through
   its migrated reflection-based construction and migrated dispatch call site.
4. `GET /get_logs?min_level=Warning` — 39 entries, every single one a pre-existing, unrelated warning
   category already documented by PHASE6's own completion report baseline (render-feature priority
   tie-breaks between demo plugins declaring the same stage priority; GPU-timing-slot-budget exhaustion
   notes for `"DemoRenderFeatureV3_Downsample"`/`"DemoRenderFeatureV3_UpsamplePresent"`/
   `"DemoRenderFeatureV3_Game_Blend"`) — zero new warning category, zero warning mentioning any file this
   phase touched.
5. `GET /get_logs?min_level=Error` — `{"count":0,...}` again, confirmed still clean after the full capture
   sequence.
6. `stop_app_background` — Editor closed cleanly, no leftover process.

## Compile check and targeted test run

- `cmake --build build` (incremental, from the repository root) — succeeded end to end (10/10 steps needing
  rebuild: `PluginRenderOperationRegistry.cpp.obj`, `PluginRenderPassBuilderAdapter_v3.cpp.obj`,
  `RenderFeatureCompositor.cpp.obj`, `libgte_core.a`, `GreatTamanaEditor.exe`, both `_v3` demo plugin
  relink steps that were already up to date did not need rebuilding, both Project Assembly `.dll`s, and
  `GreatTamanaEngineTests.exe`), zero warnings/errors from any of the three touched files or any dependent
  target.
- `ctest -R "ShaderReflection|PushConstantSizeMatches|CommandBuffer" --output-on-failure` (from `build/`) —
  all 8 pre-existing tests pass (confirming PHASE1/2/3's own reflection/push-constant-size/`CommandBuffer`
  foundation this phase's migration depends on is still completely unaffected).
- No new Tier-1 test file was added this phase — `PluginRenderOperationRegistry`/
  `PluginRenderPassBuilderAdapter_v3` are both genuinely Tier-2 (GPU-dependent — live `VkDevice`/
  `ComputePipeline`/`Renderer` construction), per `AGENTS.md`'s own "Testability & Regression Safety"
  section, and a `search_in_dir` across `tests/` confirmed neither has a pre-existing Tier-1 test file to
  update. This matches PHASE4/5/6's own identical precedent and the task doc's own Step 3 expectation (no
  Tier-1 test item listed for this phase).

Per this campaign's own process rule (Note 4/5), no full clean build or full `ctest` regression pass was run
in this phase — that is reserved for PHASE10.

## Acceptance bar — final check

- [x] All three `PluginRenderOperationRegistry` compute pipelines build via path-only
      `CreateComputePipeline()` (confirmed by direct inspection of the final `.cpp` file).
- [x] The campaign-wide audit (Step 6) is complete, and its result is captured verbatim in this report (see
      "Final campaign-wide audit" above) — R2's own explicit "do not leave two competing conventions live
      indefinitely" requirement is verifiably satisfied, not merely assumed.
- [x] Zero change to any file under `plugins/gte_plugin_abi/` or any plugin `.dll`'s own source (confirmed —
      every file this phase touched lives under `src/Core/Plugins/`).
- [x] Live, HTTP-verified: both demo plugins (the `_v3` box-blur/blend/vignette chain) still render
      correctly, zero new log warnings/errors.

## Files changed this phase

- `src/Core/Plugins/PluginRenderOperationRegistry.cpp` — `RegisterBoxBlur()`'s pipeline, the shared "uber
  ops" pipeline, and the shared blend pipeline (both built in `EnsureBuiltinsRegistered()`) all migrated
  onto reflection-based `CreateComputePipeline()`; `RegisterBlitFullscreen()`'s own GRAPHICS pipeline left
  completely untouched.
- `src/Core/Plugins/PluginRenderPassBuilderAdapter_v3.cpp` — `CommandRecorderAdapter::Dispatch()`'s real
  dispatch call site migrated onto `rg::CommandBuffer` (optional bonus, Step 2.3/Step 5);
  `DrawFullscreenTriangle()` left completely untouched (raw graphics draw, no `BeginGraphPassRecording()`
  bracket to migrate).
- `src/Core/Plugins/RenderFeatureCompositor.cpp` — removed one genuinely dead, pre-existing
  `#include "../../Renderer/Vulkan/DescriptorSetLayoutBuilder.h"` line found during the mandated Step 6
  audit (zero behavior change).

This closes the fourth and final migration batch of this campaign. PHASE8 (TextureDesc usage field +
resource pool audit) and PHASE9 (scaffolding fix + screen post-process convenience API) remain independent,
side-slotted work; PHASE10 is the only remaining phase allowed to run a full clean build + full `ctest`
regression pass.
