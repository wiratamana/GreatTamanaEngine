# PHASE6 — Migrate Editor Debug/Validation Compute Tooling (Migration Batch 3) — COMPLETION REPORT

**Parent:** `PHASE0_MASTER_STRATEGY.md`. **Task doc:**
`PHASE6_MIGRATE_EDITOR_DEBUG_COMPUTE_TOOLING.md`. **Pattern proven by:** `PHASE4_COMPLETION_REPORT.md`/
`PHASE5_COMPLETION_REPORT.md`.

## Summary

All four remaining Editor-only compute pipelines — `ComputeBlurValidation` (`BoxBlur.comp`),
`GBufferValidation`'s compute half (`GBufferCopy.comp`), `FrameDebuggerPreviewProcessing`
(`FrameDebuggerPreview.comp`), and `VolumeTexturePreviewRenderer` (`VolumeTexturePreview.comp`) — now
build their `ComputePipeline` via a path-only, reflection-based `renderer.CreateComputePipeline(path)`
call (PHASE2), with zero hand-built `DescriptorSetLayoutBuilder`/`VkPushConstantRange` remaining in any
of the four files. Every destructor's now-wrong `vkDestroyDescriptorSetLayout()` call (double-free —
the layout is now borrowed from each class's own `ComputePipeline`) was deleted, in all four files.

**Two of the four** (`ComputeBlurValidation`, `GBufferValidation`'s compute half) also had their real
dispatch call site migrated onto PHASE3's `gte::rg::CommandBuffer` (`ctx.Cmd()` →
`BindComputePipeline()`/`BindDescriptorSet()`/`SetPushConstants()`/`DispatchOverSize()`), since both are
declared through a real `RenderGraphBuilder::AddRenderPass()` with a genuine `rg::PassContext` available
in their `execute` lambda — repeating PHASE4/PHASE5's exact, already-proven pattern.

**The other two** (`FrameDebuggerPreviewProcessing`, `VolumeTexturePreviewRenderer`) do **NOT** have a
`rg::PassContext`/`CommandBuffer` available at all — both dispatch entirely inside a plain
`Renderer::ImmediateSubmit()` lambda using raw `vkCmdBindPipeline()`/`vkCmdBindDescriptorSets()`/
`vkCmdPushConstants()`/`vkCmdDispatch()` calls, since both are on-demand, non-per-frame dispatchers
invoked from ImGui UI construction / an HTTP request handler, never from inside an active render-graph
pass recording. Their dispatch call sites are therefore **architecturally incompatible** with
`CommandBuffer` — the exact same shape PHASE4 already documented for `GpuSkinningValidation.cpp` and
PHASE5 already documented for `AtmosphereLutRenderer::CaptureAerialPerspectiveVolumeSliceImmediate()`.
Only their `EnsureInitialized()` pipeline-creation code was migrated; their `RenderPreview()` dispatch
bodies (including the hand-maintained `kLocalSizeX`/`kLocalSizeY` constants) are left completely
untouched, per that same established precedent.

Everything in "Step 3 — The Plan" and the "Acceptance bar" was completed. No `ask_questions` call was
needed — the one genuine design fork this phase ran into (see "Deviation from the task doc" below) was
already resolved by PHASE4/PHASE5's own documented precedent for the identical architectural shape.

## Deviation from the task doc (found, documented, not silently glossed over)

The task doc's own Step 2 claims all four files already follow "the EXACT same shape as PHASE4/PHASE5's
already-migrated examples: `DescriptorSetLayoutBuilder` + a hand-built `VkPushConstantRange` +
`renderer.CreateComputePipeline(path, layouts, range)` inside an `EnsureInitialized()`-style lazy-init
method, and a `BeginGraphPassRecording`/`Dispatch`/`EndGraphPassRecording` bracket inside the pass's
`execute` callback." Direct inspection of the live source (both BEFORE this phase touched them) showed
this is only TRUE for the pipeline-creation half across all four files — but the dispatch-call-site half
is only true for `ComputeBlurValidation`/`GBufferValidation`. `FrameDebuggerPreviewProcessing::RenderPreview()`
and `VolumeTexturePreviewRenderer::RenderPreview()` both issue their dispatch via
`renderer.ImmediateSubmit([&](VkCommandBuffer cmd) { ... raw vkCmd* calls ... })` — there is no
`rg::AddRenderPass()`/`execute` lambda/`rg::PassContext` anywhere in either class at all (confirmed by
`search_in_dir` for `AddRenderPass`/`AddPass`/`PassContext` inside both files — zero matches in either).
Both classes' own header comments already say so explicitly (`FrameDebuggerPreviewProcessing.h`: "this
class's own `RenderPreview()` issues its ... calls directly inside a `renderer.ImmediateSubmit(...)`
lambda — NEVER `renderer.Dispatch()`"; `VolumeTexturePreviewRenderer.h`: "mirrors
`GpuSkinningValidation.cpp`'s own ... shape ... This class NEVER calls `Renderer::Dispatch()`").

This is the exact same architectural shape PHASE4 already found and documented for
`GpuSkinningValidation.cpp`'s standalone validation-tool dispatch, and PHASE5 already found and
documented for `AtmosphereLutRenderer::CaptureAerialPerspectiveVolumeSliceImmediate()` — both of those
were explicitly, deliberately left un-migrated at the dispatch-call-site level, with only their shared
`EnsureInitialized()` migrated (since it was ALSO used by a real, separately-migrated RenderGraph-pass
dispatch elsewhere in the same class). In this phase's case, `FrameDebuggerPreviewProcessing`/
`VolumeTexturePreviewRenderer` have no SEPARATE real-pass call site at all — `RenderPreview()`'s
`ImmediateSubmit()` dispatch is each class's ONLY dispatch call site — so the resolution is simpler than
PHASE4/5's "keep the constant because a second call site still needs it" case: there is only ever one
call site, and it is the one that can't use `CommandBuffer`. This was resolved directly from the
established precedent, with no `ask_questions` call needed.

## What changed

### `src/Editor/ComputeBlurValidation.cpp`

- `EnsureInitialized()`: replaced the hand-built `DescriptorSetLayoutBuilder` + manual
  `VkPushConstantRange` with `m_pipeline.emplace(renderer.CreateComputePipeline("shaders/BoxBlur.comp.spv"));`
  followed by `m_descriptorSetLayout = m_pipeline->ReflectedDescriptorSetLayout(/*set=*/0);`.
- Destructor: the `vkDestroyDescriptorSetLayout()` call was deleted (double-free fix — see PHASE4's own
  precedent).
- `AddPass()`'s `execute` lambda: replaced `renderer.BeginGraphPassRecording(ctx.cmd, ctx.recordDraw);
  renderer.Dispatch(*m_pipeline, m_descriptorSet.Native(), pushConstants, sizeof(pushConstants),
  groupCounts.width, groupCounts.height, groupCounts.depth); renderer.EndGraphPassRecording();` with
  `auto cmd = ctx.Cmd(); cmd.BindComputePipeline(*m_pipeline); cmd.BindDescriptorSet(m_descriptorSet.Native());
  cmd.SetPushConstants(pushConstants, sizeof(pushConstants)); cmd.DispatchOverSize(sceneExtent.width,
  sceneExtent.height);` — the last line now reads the bound pipeline's own reflected `LocalGroupSize()`
  instead of the hand-restated `kBoxBlurLocalSizeX`/`kBoxBlurLocalSizeY` constants (both now **deleted** —
  confirmed, via `search_in_dir`, zero remaining real-code references anywhere in this file).
- The now-unused `&renderer` lambda capture was removed from the `execute` lambda (this pass's dispatch
  no longer needs a direct `Renderer&` reference — mirrors PHASE5's own identical precedent for the six
  Atmosphere passes).
- `#include "../Renderer/ComputeDispatch.h"` and `#include "../Renderer/Vulkan/DescriptorSetLayoutBuilder.h"`
  both removed (confirmed dead via `search_in_dir`).

### `src/Editor/GBufferValidation.cpp`

- `EnsureInitialized()`: the GRAPHICS pipeline (`m_gbufferPipeline`, `Renderer::CreatePipeline()`'s
  N-format overload) is **completely untouched** — genuinely out of scope for this whole campaign (task
  doc's own Step 3 item 2: "Do not touch the separate, graphics-side MRT write pass... the graphics
  `Pipeline`/`CreatePipeline()` path is untouched by this whole campaign"). Only the COMPUTE half
  (`m_copyPipeline`, `GBufferCopy.comp`) was migrated: replaced the hand-built `DescriptorSetLayoutBuilder` +
  manual `VkPushConstantRange` with `m_copyPipeline.emplace(renderer.CreateComputePipeline("shaders/GBufferCopy.comp.spv"));`
  followed by `m_copyDescriptorSetLayout = m_copyPipeline->ReflectedDescriptorSetLayout(/*set=*/0);`.
- Destructor: the `vkDestroyDescriptorSetLayout(m_device, m_copyDescriptorSetLayout, nullptr);` call was
  deleted (same double-free fix).
- The graphics "GBufferValidation" pass's own `execute` lambda (`vkCmdBindPipeline`/`vkCmdDraw` against
  `m_gbufferPipeline`) is **byte-for-byte unchanged** — it is a `Pipeline`/graphics dispatch, not a
  `ComputePipeline` one, and this campaign never touches the graphics `Pipeline` path.
- The compute "GBufferValidationCopy" pass's own `execute` lambda: replaced the manual
  `BeginGraphPassRecording`/`Dispatch`/`EndGraphPassRecording` sequence with
  `ctx.Cmd()` → `BindComputePipeline(*m_copyPipeline)` → `BindDescriptorSet(m_copyDescriptorSet.Native())` →
  `SetPushConstants(pushConstants, sizeof(pushConstants))` → `DispatchOverSize(sceneExtent.width,
  sceneExtent.height)`. `kGBufferCopyLocalSizeX`/`kGBufferCopyLocalSizeY` both **deleted** (confirmed
  unreferenced via `search_in_dir`). The now-unused `&renderer` capture was removed from this ONE lambda
  only (the graphics pass's own lambda still captures/uses `&renderer` for its unmodified
  `BeginGraphPassRecording`/`EndGraphPassRecording` bracket).
- `#include "../Renderer/ComputeDispatch.h"` and `#include "../Renderer/Vulkan/DescriptorSetLayoutBuilder.h"`
  both removed.

### `src/Editor/FrameDebuggerPreviewProcessing.cpp`

- `EnsureInitialized()`: replaced the hand-built `DescriptorSetLayoutBuilder` + manual
  `VkPushConstantRange` (sized `sizeof(PushConstants)`, the file's own 3-field `channel`/`levelsBlack`/
  `levelsWhite` struct) with `m_pipeline.emplace(renderer.CreateComputePipeline("shaders/FrameDebuggerPreview.comp.spv"));`
  followed by `m_descriptorSetLayout = m_pipeline->ReflectedDescriptorSetLayout(/*set=*/0);`.
- Destructor: the `vkDestroyDescriptorSetLayout()` call was deleted (same double-free fix).
- `RenderPreview()`'s own `ImmediateSubmit()` dispatch body is **completely unchanged** — still raw
  `vkCmdBindPipeline`/`vkCmdBindDescriptorSets`/`vkCmdPushConstants`/`vkCmdDispatch` calls, still using
  the hand-maintained `kLocalSizeX`/`kLocalSizeY` constants (both **kept**, per this phase's own
  "Deviation from the task doc" analysis above — this is this class's ONLY dispatch call site, and it is
  architecturally incompatible with `CommandBuffer`).
- `#include "../Renderer/Vulkan/DescriptorSetLayoutBuilder.h"` removed (confirmed dead).

### `src/Renderer/VolumeTexturePreviewRenderer.cpp`

- `EnsureInitialized()`: replaced the hand-built `DescriptorSetLayoutBuilder` + manual
  `VkPushConstantRange` (sized `sizeof(PushConstants)`) with
  `m_pipeline.emplace(renderer.CreateComputePipeline("shaders/VolumeTexturePreview.comp.spv"));` followed
  by `m_descriptorSetLayout = m_pipeline->ReflectedDescriptorSetLayout(/*set=*/0);`.
- Destructor: the `vkDestroyDescriptorSetLayout()` call was deleted (same double-free fix);
  `m_volumeSampler`'s own `vkDestroySampler()` call is untouched (unrelated to the pipeline/reflection
  change — this class's own sampler, never owned by `ComputePipeline`).
- `RenderPreview()`'s own `ImmediateSubmit()` dispatch body is **completely unchanged** — same reasoning
  as `FrameDebuggerPreviewProcessing` above; `kLocalSizeX`/`kLocalSizeY` both **kept**.
- `#include "Vulkan/DescriptorSetLayoutBuilder.h"` removed (confirmed dead).

## Design decisions resolved (no `ask_questions` needed)

1. **`FrameDebuggerPreviewProcessing`/`VolumeTexturePreviewRenderer`'s dispatch call sites are NOT
   migrated onto `CommandBuffer`; their `kLocalSizeX`/`kLocalSizeY` constants are kept, not deleted.**
   See "Deviation from the task doc" above for the full reasoning — both classes' only dispatch call site
   runs inside a plain `Renderer::ImmediateSubmit()` lambda with no `rg::PassContext` of any kind,
   architecturally identical to PHASE4's `GpuSkinningValidation.cpp`/PHASE5's
   `CaptureAerialPerspectiveVolumeSliceImmediate()` precedent.
2. **`ComputeBlurValidation`/`GBufferValidation`'s compute half ARE migrated onto `CommandBuffer`, and
   their local-size constants ARE deleted.** Both are declared through a real
   `RenderGraphBuilder::AddRenderPass()` with a genuine `rg::PassContext& ctx` parameter in their
   `execute` lambda — `ctx.Cmd()` is directly available, exactly like PHASE4/PHASE5's own migrated
   passes.
3. **`GBufferValidation`'s GRAPHICS pass (`m_gbufferPipeline`) is completely untouched.** The task doc's
   own Step 3 item 2 explicitly scopes this phase to the compute half only ("the graphics
   `Pipeline`/`CreatePipeline()` path is untouched by this whole campaign") — confirmed by direct
   re-reading of that pass's own `execute` lambda: it still issues `vkCmdBindPipeline`/
   `vkCmdBindVertexBuffers`/`vkCmdDraw` directly against `m_gbufferPipeline->Native()`, unchanged, and
   still captures `&renderer` for its own `BeginGraphPassRecording`/`EndGraphPassRecording` bracket (a
   graphics pass, not a compute one — `CommandBuffer::Draw()` exists but this phase's task doc never
   asked for graphics passes to be touched at all, matching Decision D1/the campaign's own narrow
   per-file scope).

## Live verification (per the Acceptance Bar's own requirement)

Built `GreatTamanaEditor.exe` (incremental `cmake --build build`, zero errors — see "Compile check"
below), launched it via `run_app_background`, and drove it entirely over HTTP:

1. `GET /activate_tab?name=Scene` — confirmed the Scene tab became active.
2. `GET /get_logs?min_level=Warning` — only pre-existing, unrelated warnings (plugin render-feature
   priority ties, GPU-timing-slot-budget notes — identical in kind to PHASE4/PHASE5's own documented
   baseline) — nothing new, nothing related to any of this phase's four migrated classes.
3. **`ComputeBlurValidation`**: `GET /render_graph/set_blur_enabled?enabled=true` then
   `GET /get_texture?texture_name=BlurredSceneOutput` — **9201 bytes**, a real, correct blurred-sky
   gradient PNG — the exact same byte count PHASE3's own `CommandBuffer` smoke test already proved
   byte-identical against the pre-migration manual-dispatch baseline, confirming this phase's PERMANENT
   migration (the same code PHASE3 only temporarily/throwaway-tested) produces the identical result.
4. **`GBufferValidation`**: `GET /render_graph/set_gbuffer_enabled?enabled=true` then
   `GET /get_texture?texture_name=GBufferAlbedo` and `?texture_name=GBufferVisualized` — both a real,
   correct checkerboard test-pattern PNG (1019 bytes each) — confirms both the unmigrated graphics half
   (albedo) AND the migrated compute-copy half (visualized, a byte-for-byte copy of albedo) are both
   still producing correct output.
5. **`FrameDebuggerPreviewProcessing`**: `GET /frame_debugger/open` → `GET /frame_debugger/enable?value=true`
   → `GET /frame_debugger/capture` (117 events captured) → `GET /frame_debugger/select_event?index=20`
   (`"DemoRenderFeatureV2_Game_Blend"` compute dispatch) → `GET /frame_debugger/set_channel?value=r` →
   `GET /frame_debugger/set_levels?black=0.1&white=0.9` → `GET /get_swapchain` — the Frame Debugger's own
   preview pane shows a real, correctly R-channel-isolated, correctly Levels-stretched (high-contrast
   black/white) grayscale image, with the toolbar itself reflecting `R` selected and `Black 0.10`/
   `White 0.90` — direct, visual, conclusive proof the migrated `FrameDebuggerPreviewRenderer::RenderPreview()`
   dispatch (reflection-based pipeline creation, unmigrated-but-still-correct raw dispatch) is byte-correct.
6. **`VolumeTexturePreviewRenderer`**: `GET /get_texture?texture_name=AtmosphereAerialPerspectiveVolume_GameView` —
   a real, correct frustum-shaped haze gradient PNG (11841 bytes), visually matching every prior
   campaign's own documented baseline (atmosphere-scattering-2/3) — confirms the migrated
   `VolumeTexturePreviewRenderer::RenderPreview()` dispatch is byte-correct too.
7. `GET /get_logs?min_level=Error` — `{"count":0,...}` at every single checkpoint throughout the whole
   session (before any toggle, after enabling blur/gbuffer, after the Frame Debugger capture/select/
   channel/levels sequence, after the volume-texture capture) — zero new log warnings/errors anywhere.
8. Cleanup: `GET /render_graph/set_blur_enabled?enabled=false`, `GET /render_graph/set_gbuffer_enabled?enabled=false`,
   `GET /frame_debugger/enable?value=false` — all confirmed `"success":true`, logs still clean.
9. `stop_app_background` — Editor closed cleanly, no leftover process.

### A small, unrelated API-shape note found along the way (not a bug, just documented for the next reader)

`GET /frame_debugger/enable` and `GET /frame_debugger/set_channel` both take a query parameter named
`value` (e.g. `?value=true`, `?value=r`), NOT `enabled`/`channel` as one might guess from
`GET /render_graph/set_blur_enabled?enabled=...`'s own different convention — confirmed directly from
`NetworkRoutes.h`'s own parsing code after an initial `?enabled=true`/`?channel=r` attempt both correctly
returned a descriptive `400` validation error. This is pre-existing, unrelated to this phase's own
changes, and not a tool malfunction — included here only so a future reader isn't surprised by the same
initial mismatch.

## Compile check and targeted test run

- `cmake --build build` (incremental, from the repository root) — succeeded end to end (12/12 steps
  needing rebuild), zero warnings/errors from any of the four touched files or any dependent target
  (`gte_core`, `gte_editor`, `GreatTamanaEditor`, both Project Assembly demo `.dll`s,
  `GreatTamanaEngineTests`).
- `ctest -R "ShaderReflection|PushConstantSizeMatches|CommandBuffer" --output-on-failure` (from `build/`)
  — all 8 pre-existing tests pass (confirming PHASE1/2/3's own reflection/push-constant-size/
  `CommandBuffer` foundation this phase's migration depends on is still completely unaffected).
- No new Tier-1 test file was added this phase — all four migrated classes are genuinely Tier-2
  (GPU-dependent — live `VkDevice`/`ComputePipeline`/`RenderTexture`/`Texture2D` construction), per
  `AGENTS.md`'s own "Testability & Regression Safety" section, and a `search_in_dir` across `tests/`
  confirmed none of the four has a pre-existing Tier-1 test file to update. This matches PHASE4/PHASE5's
  own identical precedent and the task doc's own Step 3 expectation (no Tier-1 test item listed for this
  phase).

Per this campaign's own process rule (Note 4/5), no full clean build or full `ctest` regression pass was
run in this phase — that is reserved for PHASE10.

## Acceptance bar — final check

- [x] All four compute pipelines build via path-only `CreateComputePipeline()`, zero hand-built
      `DescriptorSetLayoutBuilder`/`VkPushConstantRange` remaining in any of the four files (confirmed by
      direct inspection of all four final `.cpp` files, and by `search_in_dir` for
      `DescriptorSetLayoutBuilder` across `src/` — zero remaining `#include`s or real-code references,
      only historical/comment mentions in the four files this phase touched).
- [x] All four destructors fixed (no double-destroy of a `VkDescriptorSetLayout` the respective
      `ComputePipeline` now owns) — confirmed: all four `vkDestroyDescriptorSetLayout()` calls deleted;
      `VolumeTexturePreviewRenderer`'s unrelated `m_volumeSampler` destroy call correctly left in place.
- [x] Live, HTTP-verified: all four debug/validation features still work identically — `ComputeBlurValidation`
      reproduces PHASE3's own byte-identical baseline (9201 bytes), `GBufferValidation` shows correct
      albedo/visualized checkerboard output, `FrameDebuggerPreviewProcessing` correctly applies a live
      Channel/Levels transform to a real captured event's preview, and `VolumeTexturePreviewRenderer`
      correctly renders the Aerial Perspective volume's frustum-shaped haze gradient — zero new log
      warnings/errors at any checkpoint throughout the whole session.

## Files changed this phase

- `src/Editor/ComputeBlurValidation.cpp` — `EnsureInitialized()`/destructor/dispatch call site all
  migrated onto reflection-based `CreateComputePipeline()` + `rg::CommandBuffer`; dead
  `ComputeDispatch.h`/`DescriptorSetLayoutBuilder.h` includes removed; `kBoxBlurLocalSizeX/Y` deleted.
- `src/Editor/GBufferValidation.cpp` — the COMPUTE half's (`m_copyPipeline`) `EnsureInitialized()`/
  destructor/dispatch call site migrated onto reflection-based `CreateComputePipeline()` +
  `rg::CommandBuffer`; the GRAPHICS half (`m_gbufferPipeline`) left completely untouched; dead
  `ComputeDispatch.h`/`DescriptorSetLayoutBuilder.h` includes removed; `kGBufferCopyLocalSizeX/Y` deleted.
- `src/Editor/FrameDebuggerPreviewProcessing.cpp` — `EnsureInitialized()`/destructor migrated onto
  reflection-based `CreateComputePipeline()`; `RenderPreview()`'s own raw `ImmediateSubmit()` dispatch
  body (including `kLocalSizeX/Y`) deliberately left unchanged (architecturally incompatible with
  `CommandBuffer` — see "Deviation from the task doc" above); dead `DescriptorSetLayoutBuilder.h` include
  removed.
- `src/Renderer/VolumeTexturePreviewRenderer.cpp` — `EnsureInitialized()`/destructor migrated onto
  reflection-based `CreateComputePipeline()`; `RenderPreview()`'s own raw `ImmediateSubmit()` dispatch
  body (including `kLocalSizeX/Y`) deliberately left unchanged (same reasoning); dead
  `DescriptorSetLayoutBuilder.h` include removed.

PHASE7 can now run its full-repository grep audit (migrating `PluginRenderOperationRegistry`'s three
host-side pipelines) confirming zero remaining production `DescriptorSetLayoutBuilder`/manual-
`VkPushConstantRange` call site exists anywhere outside this campaign's own new reflection internals and
the two documented, architecturally-exotic escape-hatch cases this phase/PHASE4/PHASE5 each left in place
(`GpuSkinningValidation.cpp`, `CaptureAerialPerspectiveVolumeSliceImmediate()`,
`FrameDebuggerPreviewProcessing::RenderPreview()`, `VolumeTexturePreviewRenderer::RenderPreview()`).
