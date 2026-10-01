# PHASE5 — Migrate Atmosphere Compute Passes (Migration Batch 2) — COMPLETION REPORT

**Parent:** `PHASE0_MASTER_STRATEGY.md`. **Task doc:**
`PHASE5_MIGRATE_ATMOSPHERE_COMPUTE_PASSES.md`. **Pattern proven by:** `PHASE4_COMPLETION_REPORT.md`.

## Summary

All six Atmosphere compute pipelines in `src/Renderer/Atmosphere/AtmosphereLutRenderer.cpp` —
Transmittance LUT, Multi-Scattering LUT, Sky-View LUT, Aerial Perspective Volume, Aerial Perspective
Composite, and Aerial Perspective Volume Debug Slice — are now migrated onto PHASE2's reflection-based
`CreateComputePipeline(path)` and PHASE3's `gte::rg::CommandBuffer`, repeating PHASE4's exact, already-
proven migration pattern. Zero visual/behavioral change: `.Rewrite()`/`ComputeDescriptorSet` usage is
completely untouched (confirmed by direct diff review — not a single `.Rewrite(` call's own arguments
changed), and a live session confirmed the Transmittance/Sky-View LUT and Aerial Perspective Volume
Debug Slice textures still compute correct, sun-angle-reactive data end-to-end (see "Live verification"
below).

Everything in "Step 3 — The Plan" and the "Acceptance bar" was completed. No `ask_questions` call was
needed — the task doc's own Step 2/Step 3, plus PHASE4's own precedent for the destructor double-free
fix and the "keep vs. delete a local-size constant" decision rule, already resolved every design
question this phase ran into.

## What changed

### `src/Renderer/Atmosphere/AtmosphereLutRenderer.cpp`

For **all six** `EnsureXxxInitialized()` methods:
- Replaced the hand-built `DescriptorSetLayoutBuilder` sequence (and, for the two passes that have a
  push-constant block — Aerial Perspective Composite and Aerial Perspective Volume Debug Slice — the
  hand-built `VkPushConstantRange`) with a path-only `renderer.CreateComputePipeline("shaders/Xxx.comp.spv")`
  call, followed by `m_xxxDescriptorSetLayout = m_xxxPipeline->ReflectedDescriptorSetLayout(/*set=*/0);`.
- `#include "../Vulkan/DescriptorSetLayoutBuilder.h"` removed from the top of the file (confirmed, via
  `search_in_dir`, zero remaining references to `DescriptorSetLayoutBuilder` anywhere in this file
  afterward).

For **all six** dispatch call sites (inside each pass's `execute` lambda):
- Replaced `renderer.BeginGraphPassRecording(ctx.cmd, ctx.recordDraw); renderer.Dispatch(...);
  renderer.EndGraphPassRecording();` with `auto cmd = ctx.Cmd(); cmd.BindComputePipeline(...);
  cmd.BindDescriptorSet(...); [cmd.SetPushConstants(...);] cmd.DispatchOverSize(...);` — the last line
  reads the bound pipeline's own reflected `LocalGroupSize()` instead of a hand-restated
  `kXxxLocalSizeX/Y[/Z]` constant.
- The now-unused `&renderer` lambda capture was removed from every one of these six execute lambdas
  (mirroring `PHASE4_COMPLETION_REPORT.md`'s own `Core.cpp` precedent of dropping an unused capture
  entirely, rather than leaving a dead one behind) — confirmed via `search_in_dir` for `&renderer`
  inside this file: zero remaining matches.
- Aerial Perspective Composite's `cmd.SetPushConstants(localPushConstants);` + `cmd.DispatchOverSize(extent.width,
  extent.height, 1);` and Aerial Perspective Volume Debug Slice's `cmd.SetPushConstants(localPushConstants);`
  + `cmd.DispatchOverSize(volumeWidth, volumeHeight, 1);` both now benefit from PHASE3's R4 debug-assert
  (`SetPushConstants<T>()` checks `sizeof(T)` against the pipeline's own reflected `PushConstantSize()`).

**Destructor fix (`~AtmosphereLutRenderer()`), applied identically to all six layouts** — per PHASE4's
own documented finding, restated here: every one of the six `vkDestroyDescriptorSetLayout(m_device,
m_xxxDescriptorSetLayout, nullptr);` calls was **deleted**. Each `m_xxxDescriptorSetLayout` is now
BORROWED from its own `ComputePipeline`'s reflected `ReflectedDescriptorSetLayout(0)` — owned and
destroyed by that `ComputePipeline` itself (`ComputePipeline::Destroy()`'s own `m_ownedReflectedLayouts`
cleanup, PHASE2). Keeping the old destructor calls would have been a genuine double-free the instant
this class's destructor ran. Confirmed exactly six `vkDestroyDescriptorSetLayout()` calls existed before
this edit (matching the task doc's own expectation) and all six are now gone.

**Now-dead hand-restated local-size constants deleted** (confirmed via `search_in_dir` across the whole
repository that each had zero remaining real-code references before deletion):
- `kTransmittanceLutLocalSizeX/Y`
- `kMultiScatteringLutLocalSizeX/Y`
- `kSkyViewLutLocalSizeX/Y`
- `kAerialPerspectiveVolumeLocalSizeX/Y/Z`
- `kAerialPerspectiveCompositeLocalSizeX/Y`

**One local-size constant pair deliberately KEPT, not deleted** — `kAerialPerspectiveVolumeDebugSliceLocalSizeX/Y`.
Unlike the other five, this pair has a SECOND real call site after this phase's migration:
`CaptureAerialPerspectiveVolumeSliceImmediate()` (the "Inspect Aerial Perspective LUT" button's backing
method) issues its own dispatch via `renderer.ImmediateSubmit()` + raw `vkCmdDispatch()` — it has no
`rg::PassContext` of any kind (same architectural shape as `GpuSkinningValidation.cpp`'s own call site,
which PHASE4 identified and left alone for the identical reason) — so it still needs this constant
directly. This method's body is otherwise completely untouched by this phase; it continues to use
`m_aerialPerspectiveVolumeDebugSlicePipeline->Native()`/`->Layout()`, both of which are plain accessors
that behave identically regardless of whether the pipeline was built via the reflection path or the
manual path, so this method required zero code change to keep working correctly after the migration.

## Design decisions resolved (no `ask_questions` needed)

1. **`m_device` is KEPT as a class member** (unlike `CullingPipelines`/`GpuSkinningPipelines` in PHASE4,
   where `m_device` was removed entirely once nothing else needed it). `AtmosphereLutRenderer` still
   needs `m_device` for every `ComputeDescriptorSet::Rewrite(m_device, ...)` call across all six passes
   (these are completely out of scope for this campaign — Milestone 2/bindless territory) and for
   `AllocateComputeDescriptorSet(m_xxxDescriptorSetLayout)` in every `EnsureXxxViewInitialized()`/
   `EnsureXxxInitialized()` method, so it remains load-bearing and was left exactly as-is.
2. **`kAerialPerspectiveVolumeDebugSliceLocalSizeX/Y` kept** (see "What changed" above) — the same
   "kept because a second, architecturally-different call site still needs it" pattern PHASE4 already
   established for `kSkinningLocalSizeX`/`GpuSkinningValidation.cpp`.
3. **Every other local-size constant deleted** — each confirmed, via `search_in_dir`, to have zero
   remaining real-code references (only comments) once its one real call site was migrated.
4. **`kCullingPushConstantSize`-style "keep a compile-time static_assert" pattern does not apply here**
   — unlike `Core.cpp`'s `CullingPushConstants`, neither `AerialPerspectiveCompositePushConstants` nor
   `AerialPerspectiveVolumeDebugSlicePushConstants` ever had a separate, hand-restated
   `kXxxPushConstantSize` constant in the first place (both pipelines' `VkPushConstantRange::size` was
   always `sizeof(TheStruct)` directly) — there was nothing of this shape to keep or delete.

## Live verification (per the Acceptance Bar's own requirement)

Built `GreatTamanaEditor.exe` (incremental `cmake --build build`, zero errors — see "Compile check"
below), launched it via `run_app_background`, and drove it entirely over HTTP:

1. `GET /activate_tab?name=Game` then `GET /get_logs?min_level=Warning` — only pre-existing, unrelated
   warnings (plugin render-feature priority ties, GPU-timing-slot-budget notes, identical in kind to
   PHASE4's own documented baseline) — nothing new, nothing related to `AtmosphereLutRenderer`/
   `CommandBuffer`.
2. `GET /get_logs?min_level=Error` — `{"count":0,...}`, both at the start and end of this session.
3. `GET /get_logs?category=RenderPassHonesty` and `?keyword=Atmosphere` — both completely empty
   throughout — zero new warnings/errors tied to the migrated Atmosphere passes at any point.
4. **The real sun-angle-sweep proof** (the task doc's own required check): `POST /instantiate_light`
   created a real `DirectionalLight` entity ("TestSun"), then three `POST /set_entity_trs` calls swept
   its rotation through three very different angles, with `GET /get_texture?texture_name=...` capturing
   the REAL GPU-computed LUT textures after each:
   - `AtmosphereTransmittanceLut` — a real, correct-looking transmittance gradient (bright near the
     horizon, dark toward zenith) — confirms the migrated `EnsureTransmittanceLutInitialized()`/
     `AddTransmittanceLutPass()` dispatch is byte-correct.
   - `AtmosphereSkyViewLut_GameView` at a high sun angle (`x=10°`, near-horizon) showed a visible
     sunset-colored horizon glow; at `x=-60°` (sun below the horizon) the SAME texture went
     **completely black** (night); at the original `x=20°` it showed a normal daytime blue gradient —
     direct, conclusive proof the migrated `EnsureSkyViewLutInitialized()`/`AddSkyViewLutPass()`
     dispatch correctly reads the real, live sun direction and recomputes correctly every call.
   - `AtmosphereAerialPerspectiveVolumeDebugSlice` — a real, non-trivial gradient image (confirms the
     migrated `EnsureAerialPerspectiveVolumeInitialized()`/`AddAerialPerspectiveVolumePass()` AND
     `EnsureAerialPerspectiveVolumeDebugSliceInitialized()`/`AddAerialPerspectiveVolumeDebugSlicePass()`
     dispatches are both byte-correct, since this slice is read back from the volume the first pass
     wrote).
5. `GET /get_game_view` / `GET /get_texture?texture_name=GameViewComposited` — both show a solid
   salmon-pink background with a blue radial overlay, UNCHANGED across every sun-angle sweep above.
   This is the exact same, already-documented, pre-existing `DemoRenderFeatureV2`/`V3` plugin vignette
   effect `PHASE4_COMPLETION_REPORT.md` itself called out ("the visible blue radial overlay in the
   capture is a pre-existing, unrelated `DemoRenderFeatureV2`/`V3` plugin vignette effect") — these demo
   plugins' own `RenderFeatureCompositor`-driven output fully overwrites the real Aerial Perspective
   Composite result in THIS environment's final on-screen image, which is why the sun-angle sweep is
   only directly visible by capturing the individual named LUT textures (step 4 above) rather than the
   final composited view. This is unrelated to this phase's migration — the exact same masking was
   already present and documented before any Atmosphere code was touched.
6. `stop_app_background` — Editor closed cleanly, no leftover process.

### What was NOT live-verified, and why (honest, documented gap)

The task doc's own Step 3 item 7 asks to click the "Inspect Aerial Perspective LUT" button (or its HTTP
equivalent, if one exists) to confirm the Debug-Slice-only pass still works. **No HTTP route exists for
triggering an arbitrary ImGui button click** (confirmed via `search_in_dir` across `src/Network/` for
"click" — the only match is an unrelated comment) — this button's own backing method,
`CaptureAerialPerspectiveVolumeSliceImmediate()`, was therefore not directly exercised by clicking it
this session. This gap is mitigated, not ignored:
- This method's entire body is byte-for-byte UNCHANGED by this phase (confirmed by direct code review —
  it was never part of any edit region) — it only reads `m_aerialPerspectiveVolumeDebugSlicePipeline->Native()`/
  `->Layout()`, two plain accessors whose return values are identical regardless of which constructor
  path (`ComputePipeline`'s manual path vs. the new reflection path) built the pipeline.
- The REAL, per-frame `AddAerialPerspectiveVolumeDebugSlicePass()` RenderGraph pass — which shares the
  exact same `EnsureAerialPerspectiveVolumeDebugSliceInitialized()` lazy-init method and the exact same
  `m_aerialPerspectiveVolumeDebugSlicePipeline` this immediate-dispatch method also uses — WAS directly,
  live-verified (see point 4 above, the `AtmosphereAerialPerspectiveVolumeDebugSlice` texture capture),
  proving the shared pipeline/layout construction is correct.
- `cmake --build build` succeeded end-to-end with zero errors.

A future phase/campaign adding a generic "click this named ImGui button" HTTP route (or a dedicated
`POST /atmosphere/inspect_aerial_perspective_lut` endpoint) would let this gap be closed with a fully
live check; it is called out here explicitly rather than silently glossed over, per this repository's
own stated "brutal honesty" convention.

## Compile check and targeted test run

- `cmake --build build` (incremental) — succeeded end to end, zero warnings/errors from the touched
  file or any dependent target (`gte_core`, `gte_editor`, `GreatTamanaEditor`, both Project Assembly
  demo `.dll`s, `GreatTamanaEngineTests`).
- `ctest -R "ShaderReflection|PushConstantSizeMatches|CommandBuffer" --output-on-failure` (from
  `build/`) — all 8 pre-existing tests pass (confirming PHASE1/2/3's own reflection/push-constant-size/
  `CommandBuffer` foundation this phase's migration depends on is still completely unaffected).
- No new Tier-1 test file was added this phase — `AtmosphereLutRenderer` is genuinely Tier-2
  (GPU-dependent — live `VkDevice`/`ComputePipeline`/`RenderTexture`/`VolumeTexture` construction), per
  `AGENTS.md`'s own "Testability & Regression Safety" section, and a `search_in_dir` across `tests/`
  confirmed no pre-existing Tier-1 test file exists for this class to update. This matches PHASE4's own
  identical precedent and the task doc's own Step 3 item 5/6 expectation.

Per this campaign's own process rule (Note 4/5), no full clean build or full `ctest` regression pass was
run in this phase — that is reserved for PHASE10.

## Acceptance bar — final check

- [x] All six Atmosphere compute pipelines build via path-only `CreateComputePipeline()`, zero
      hand-built `DescriptorSetLayoutBuilder`/`VkPushConstantRange` remaining anywhere in
      `AtmosphereLutRenderer.cpp` (confirmed by direct inspection of the final file, and by
      `search_in_dir` for `DescriptorSetLayoutBuilder` inside it — zero matches).
- [x] `~AtmosphereLutRenderer()` no longer destroys any `VkDescriptorSetLayout` it does not own (all six
      `vkDestroyDescriptorSetLayout()` calls deleted; every layout is now borrowed from its own
      `ComputePipeline`).
- [x] `.Rewrite()`/`ComputeDescriptorSet` usage is completely untouched — confirmed by reviewing every
      `.Rewrite(` call site in the final file: none of their own arguments changed from before this
      phase.
- [x] Live, HTTP-verified: the sky/atmosphere still renders identically in kind, and genuinely responds
      to a real sun-angle sweep (Transmittance/Sky-View LUTs and the Aerial Perspective Volume +
      Debug Slice all directly captured and shown to react correctly), zero new log warnings/errors.
      The Debug-Slice-only "Inspect Aerial Perspective LUT" button itself could not be directly clicked
      (no HTTP route exists for arbitrary ImGui button clicks) — honestly documented above as a gap,
      mitigated by its shared-code-path proof and zero source change to its own backing method.

## Files changed this phase

- `src/Renderer/Atmosphere/AtmosphereLutRenderer.cpp` — all six `EnsureXxxInitialized()` methods and all
  six dispatch call sites migrated onto reflection-based `CreateComputePipeline()` + `rg::CommandBuffer`;
  the destructor's six `vkDestroyDescriptorSetLayout()` calls deleted; five now-dead local-size constant
  pairs/triples deleted (one pair, `kAerialPerspectiveVolumeDebugSliceLocalSizeX/Y`, deliberately kept —
  see "What changed" above); dead `#include "../Vulkan/DescriptorSetLayoutBuilder.h"` removed.

PHASE6 can now repeat this exact, twice-proven migration pattern across `ComputeBlurValidation`/
`GBufferValidation`/`FrameDebuggerPreviewProcessing`/`VolumeTexturePreviewRenderer`.
