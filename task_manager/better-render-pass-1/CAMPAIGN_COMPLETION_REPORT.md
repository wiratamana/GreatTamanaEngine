# better-render-pass-1 — Render Pass Authoring Rehaul (Milestone 1) — CAMPAIGN COMPLETION REPORT

**Parent:** `PHASE0_MASTER_STRATEGY.md`. This report is the single, top-level summary of the whole
ten-phase campaign, mirroring `render-pass-7/CAMPAIGN_COMPLETION_REPORT.md`'s own shape. See each
`PHASEn_COMPLETION_REPORT.md` in this same folder for full per-phase detail.

## What this campaign set out to do

Implement Milestone 1 of
`C:\Users\F5954\Documents\TAMANA\GreatTamanaEngin-Ideas\better-render-pass-1\Investigation_Report_Render_Pass_Rehaul.txt`
("Case of the Copy-Pasted Red Tint"), Decision D2's own fixed sequencing: a new engine-owned
`gte::rg::CommandBuffer` (R1), SPIRV-Reflect-based reflection-driven compute pipeline creation (R2),
type-safe push constants (R4), a genuine `TextureDesc::usage` field unlocking pooled storage images
(R5), a fixed idempotency bug in the screen-pass scaffolding tool (R6), and a simpler
`Core::AddScreenPostProcessPass()` convenience API (Decision D3) — while explicitly deferring the real
bindless resource system (R3, Milestone 2) and the Atmosphere dirty-flag optimization (R8) to a future
`better-render-pass-2` campaign.

## What shipped, phase by phase

**PHASE1 — SPIRV-Reflect Dependency + Shader Reflection Core.** `src/Renderer/Vulkan/ShaderReflection.h/.cpp`
— a pure, Tier-1-testable `ReflectComputeShader()` function reading a compiled `.comp.spv`'s descriptor
bindings, push-constant block, and `layout(local_size_x/y/z)` work-group size via the already-vendored
`spirv_reflect` third-party library. Zero engine call site touched. Deviation: the task doc's own CMake
wiring instruction (`gte_add_shader(GreatTamanaEngineTests ...)`) would have produced a real Ninja
"multiple rules generate the same output" error, since `GreatTamanaEditor` already declared the same
`.spv` output — fixed by having the test target depend on the already-declared generated file directly
instead of re-declaring it. 2 new tests.

**PHASE2 — Reflection-Based Compute Pipeline Creation.** `ComputePipeline`'s constructor now has an
additive REFLECT path, selected purely by whether the caller omitted both `descriptorSetLayouts` and
`pushConstantRange` — zero new enum parameter (the task doc explicitly forbade one, to avoid silently
overriding any of the 16 not-yet-migrated call sites). New `ReflectedDescriptorSetLayout(set)`/
`PushConstantSize()`/`LocalGroupSize()` accessors; `m_ownedReflectedLayouts` ensures `ComputePipeline`
itself owns and destroys any layout it built via reflection, with exception-safe cleanup on every
failure path. 3 new tests (`GroupDescriptorBindingsBySet()`), plus a live, windowed verification
(temporary instrumentation, fully reverted) confirming the reflection path resolves the exact same
binding/push-constant-size layout as `ComputeBlurValidation.cpp`'s own pre-existing manual-path pipeline.

**PHASE3 — Engine-Owned CommandBuffer + Type-Safe Push Constants.** `gte::rg::CommandBuffer`
(`src/Renderer/RenderGraph/CommandBuffer.h/.cpp`), obtained via a new `PassContext::Cmd()`, gives any
pass's `execute` callback `BindComputePipeline`/`BindDescriptorSet`/`SetPushConstants<T>()`/`Dispatch`/
`DispatchOverSize`/`Draw`. `SetPushConstants<T>()` debug-asserts `sizeof(T)` against the bound
pipeline's reflected `PushConstantSize()` via the pure, Tier-1-tested `PushConstantSizeMatches()` (R4).
Pure, additive infrastructure — zero real pass body touched yet. 3 new tests, plus a live, byte-identical
(9201-byte texture capture, 113123-byte swapchain capture) smoke test proving `CommandBuffer`'s dispatch
path is indistinguishable from the manual `Renderer::Dispatch()` sequence it replaces.

**PHASE4 — Migrate Culling + GPU Skinning Compute Passes (Migration Batch 1).** `CullingPipelines` and
`GpuSkinningPipelines` — the first two real production compute-pipeline classes — migrated onto
path-only `CreateComputePipeline()` + `CommandBuffer`. Found and fixed a real destructor double-free
latent in both classes (each had its own `vkDestroyDescriptorSetLayout()` call that would now double-free
a layout `ComputePipeline` itself owns). `kCullingLocalSizeX` deleted (zero remaining real-code
references); `kCullingPushConstantSize`/`kSkinningLocalSizeX` kept (a live compile-time `static_assert`,
and a second, architecturally-incompatible `GpuSkinningValidation.cpp` call site, respectively). Live,
HTTP-verified: a spawned GPU-driven test batch's visible count correctly drops from 6 to 5 when one
instance is moved out of the camera frustum. **Honest, documented gap**: GPU Skinning's own live
animation was not directly exercised this phase (no skinned `.pmx` model in the default test scene,
no HTTP import route) — mitigated by the identical, already-proven migration pattern and a direct
re-read of both skinning shaders' own `layout(local_size_x)`/push-constant shape confirming the
reflected values match exactly.

**PHASE5 — Migrate Atmosphere Compute Passes (Migration Batch 2).** All six `AtmosphereLutRenderer`
passes (Transmittance/Multi-Scattering/Sky-View/Aerial-Perspective-Volume/Composite/Debug-Slice)
migrated identically. Five now-dead local-size constant pairs/triples deleted;
`kAerialPerspectiveVolumeDebugSliceLocalSizeX/Y` kept (a second, architecturally-incompatible
`CaptureAerialPerspectiveVolumeSliceImmediate()` call site). `m_device` kept (still needed for
`ComputeDescriptorSet::Rewrite()`/`AllocateComputeDescriptorSet()`, out of this campaign's scope).
Live, HTTP-verified: a real sun-angle sweep (via `POST /instantiate_light` + `POST /set_entity_trs`)
produces correct, visibly different Transmittance/Sky-View LUT and Aerial-Perspective-Volume output at
each angle. **Honest, documented gap**: the "Inspect Aerial Perspective LUT" button's own backing method
could not be directly clicked (no HTTP route for an arbitrary ImGui button click) — mitigated since its
body is byte-for-byte unchanged and shares the same already-verified pipeline/layout construction.

**PHASE6 — Migrate Editor Debug/Validation Compute Tooling (Migration Batch 3).** `ComputeBlurValidation`
and `GBufferValidation`'s compute half (`GBufferCopy.comp`) — both declared through a real
`RenderGraphBuilder::AddRenderPass()` with a genuine `rg::PassContext` — fully migrated onto
`CommandBuffer`. `FrameDebuggerPreviewProcessing`/`VolumeTexturePreviewRenderer` had only their
`EnsureInitialized()` pipeline construction migrated — their own dispatch runs entirely inside a plain
`Renderer::ImmediateSubmit()` lambda with no `rg::PassContext` at all, architecturally incompatible with
`CommandBuffer` (the identical shape PHASE4/5 already found for `GpuSkinningValidation.cpp`/
`CaptureAerialPerspectiveVolumeSliceImmediate()`). All four destructors' double-free fixed. Live,
HTTP-verified: `ComputeBlurValidation` reproduces PHASE3's own byte-identical 9201-byte baseline;
`GBufferValidation` shows correct albedo/visualized checkerboard output; `FrameDebuggerPreviewProcessing`
correctly applies a live Channel/Levels transform; `VolumeTexturePreviewRenderer` renders the correct
frustum-shaped haze gradient.

**PHASE7 — Migrate Plugin Render Operation Registry + Final Migration Audit (Migration Batch 4).**
`PluginRenderOperationRegistry`'s three host-side pipelines (shared "uber ops", `BoxBlur`, shared blend)
migrated identically; `RegisterBlitFullscreen()`'s own GRAPHICS pipeline left untouched (out of scope).
Optional bonus: `PluginRenderPassBuilderAdapter_v3.cpp`'s `CommandRecorderAdapter::Dispatch()` (a real
`rg::PassContext&` member was available) also migrated onto `CommandBuffer`; `DrawFullscreenTriangle()`
left untouched (a raw graphics draw with no recording bracket to migrate). The mandated full-repository
audit (captured verbatim in `PHASE7_COMPLETION_REPORT.md`) confirms **zero remaining production
`DescriptorSetLayoutBuilder`/manual-`VkPushConstantRange`-for-a-compute-pipeline call site** exists
anywhere outside this campaign's own reflection internals and a small, fully enumerated set of genuinely
exotic graphics-pipeline exceptions — R2's own "no two competing conventions" requirement is satisfied,
confirmed by direct grep, not merely assumed. One incidental, zero-risk dead `#include` removed from
`RenderFeatureCompositor.cpp`, surfaced by the audit itself. Live, HTTP-verified: both demo plugins'
box-blur/blend/vignette chain still renders correctly end to end.

**PHASE8 — `TextureDesc::usage` Field + Resource Pool Audit (R5).** New `TextureUsage` bitmask enum
(`None`/`Sampled`/`Storage`/`TransferSrc`/`TransferDst`) and `TextureDesc::usage` field (appended at the
struct's end, automatically covered by its existing `= default` `operator==`).
`RenderGraphResourcePool::AcquireTexture()` now threads `HasFlag(desc.usage, TextureUsage::Storage)`
into `Renderer::CreateRenderTexture()`'s `allowStorageImageAccess` — a pooled/transient `CreateTexture()`
resource can now genuinely become a storage image for the first time. `TransferSrc`/`TransferDst` are
deliberately vocabulary-only (zero wired `CreateRenderTexture()` plumbing), documented plainly rather
than implying a capability that doesn't exist. 7 new tests. Live-verified via a temporary (fully
reverted, zero net diff) probe pass: a real compute shader successfully wrote a solid magenta fill into
a pooled texture through its storage-image descriptor binding. **Honest, out-of-scope finding, not
acted on**: `PluginRenderResourceTranslation.cpp`'s `ToRgTextureDesc()` (the `IPluginRenderPassBuilder_v3`
ABI's own `CreateTexture()` translation) never sets `TextureUsage::Storage` even though its own
pooled outputs are written as compute storage images — a pre-existing gap (the field didn't exist
before this phase), not regressed by this phase, but also not closed (touching the plugin ABI surface
is out of this whole campaign's scope).

**PHASE9 — Scaffolding Idempotency Fix (R6) + Screen Post-Process Convenience API (Decision D3).**
Part A: `src/Editor/ScreenPassAutoWire.cpp`'s idempotency guard now performs TWO independent
"already active?" presence checks (call line, forward declaration) instead of one, via a shared
`AnyNonCommentLineContains()` primitive and a generalized `InsertPendingLines()` helper — closing the
confirmed Finding 10(b) duplicate-forward-declaration bug. A new regression test was confirmed to FAIL
against the original buggy code (temporarily restored) and PASS after the fix was re-applied. Part B:
`Core::AddScreenPostProcessPass()` fixes `stage` to `PostComposite` and auto-assigns a runtime,
process-wide monotonic priority counter when omitted; `Core::RegisterProjectRenderFeature()` itself is
byte-for-byte unchanged. The Editor's scaffold template now generates a call to the new, simpler API
(three pre-existing tests' literal-string assertions updated for the new generated-content shape — a
necessary, mechanical consequence, not a design change). Live-verified end to end: a hand-written
`BlueTintScreenPass.cpp` (bypassing scaffolding entirely, zero `stage`/`priority` argument) produced a
real, live `[PostComposite] BlueTint.ScreenTint` row in the Editor's "Render Graph" panel — kept as a
permanent fixture per the task doc's own stated preference. 8 new tests (1 regression + 4 priority-
counter + 3 live-API, the latter `Skipped` on this machine's pre-existing headless-surface limitation).

**PHASE10 (this report) — Final Verification and Campaign Closeout.** See below.

## Final verification snapshot (PHASE10)

- **Full clean build**: `cmake --build build --clean-first` — 636/636 steps rebuilt from scratch, zero
  errors, zero warnings observed in the full build log (confirmed by direct review — no `warning:` line
  anywhere, including PHASE1's brand-new `spirv_reflect` third-party target). A follow-up no-op
  incremental build confirmed `ninja: no work to do.`
- **Full `ctest -C Debug --output-on-failure`**: **2176 tests total, 100% of executed tests passing, 60
  legitimate environment-gated skips** — up from `editor-core-separation-27`'s own documented baseline
  of **2153 tests/57 skips** (the last campaign to ship before this one started, confirmed via
  `task_manager/editor-core-separation-27/CAMPAIGN_COMPLETION_REPORT.md`). `2176 - 2153 = +23` total
  tests; `60 - 57 = +3` new skips. Fully reconciled against every phase's own reported new-test count:
  PHASE1 (2) + PHASE2 (3) + PHASE3 (3) + PHASE4 (0) + PHASE5 (0) + PHASE6 (0) + PHASE7 (0) + PHASE8 (7)
  + PHASE9 (8) = **23**, exactly matching the observed delta. The +3 new skips are exactly PHASE9's own
  3 new `RegisterProjectRenderFeatureApiTest` cases (`AddScreenPostProcessPassWithNoExplicitPriority...`/
  `...WithExplicitPriority...`/`TwoAutoPriorityCallsInSequence...`), which share this development
  machine's pre-existing, already-documented lack of `VK_EXT_headless_surface` support with every other
  test in that same file — not a new gap this campaign introduces.
- **Live, HTTP-driven Game/Scene View smoke test** against a freshly, fully rebuilt `GreatTamanaEditor.exe`
  (`run_app_background` + `gte_send_request` + `stop_app_background`):
  - `GET /get_swapchain` at startup — a real, correctly rendered frame (salmon-pink background + blue
    radial vignette, the same already-documented `DemoRenderFeatureV2`/`V3` plugin baseline every prior
    phase's own report captured), `GET /get_logs?min_level=Error` → `{"count":0}`.
  - Atmosphere (PHASE5): `GET /get_texture?texture_name=AtmosphereTransmittanceLut` and
    `?texture_name=AtmosphereSkyViewLut_GameView` both returned real, correct gradient images.
  - GPU-driven culling (PHASE4): `POST /spawn_gpu_driven_test_batch` → `visible_count: 6`; after
    `POST /set_entity_trs` moved one instance 5000 units away, `GET /render_graph` correctly reported
    `visible_count: 5` — the migrated culling compute shader is still byte-correct.
  - Blur/GBuffer Validation (PHASE6): both toggled on via `GET /render_graph/set_blur_enabled`/
    `set_gbuffer_enabled`; `GET /get_texture?texture_name=BlurredSceneOutput`/`GBufferAlbedo` both
    returned correct real output, then both toggled back off.
  - Frame Debugger (PHASE6): `open`/`enable`/`capture` → 131 events captured; `select_event`/
    `set_channel=r`/`set_levels` correctly applied a live R-channel-isolated, Levels-stretched preview,
    confirmed visually via `GET /get_swapchain` showing the exact expected tree grouping
    (`"Compute LUT"` heading, `"Compute Dispatches (Post-GameView)"` bucket with `GpuDrivenBatch0`/
    `ProjectAssemblyProbe.FillTexture`/`AtmosphereAerialPerspectiveCompositePass`/
    `RenderFeatureCompositor_Game_SeedCopy` — unchanged from every prior campaign's own documented
    baseline), then disabled again.
  - `GET /get_logs?min_level=Warning` — 58 entries, every one a pre-existing, unrelated warning category
    already documented by PHASE7/8's own baseline (plugin render-feature priority tie-breaks,
    GPU-timing-slot-budget exhaustion) — zero new warning category.
  - `GET /get_logs?min_level=Error` — `{"count":0}` at every checkpoint throughout the whole session.
- **Live reproduction of the Finding 10(b) fix (R6)** against the real
  `Projects/ScreenPassAutoWireProbe/` fixture: `POST /project_assembly/open_project?name=ScreenPassAutoWireProbe`
  made it the active project; `POST /project_assembly/create_asset?kind=screen_post_process_pass&name=Phase10ReloadProbe`
  scaffolded a throwaway pass, auto-wiring both a forward declaration and a call into
  `ScreenPassAutoWireProbeGame.cpp`; the call line was then manually commented out (forward declaration
  left active); the throwaway `.cpp` was deleted so the same production endpoint could be re-triggered
  for the identical name (the endpoint's own `std::filesystem::exists(filePath)` early-return otherwise
  blocks a literal "same name, file still on disk" re-trigger — see "Design decisions resolved" below);
  re-running the exact same `create_asset` request against this now-partially-wired file produced a
  file where the forward declaration appears **exactly once** and a fresh, active call line was
  inserted — the confirmed PHASE9 fix, now proven live against the real fixture that originally
  motivated Finding 2/10(b), not merely the Tier-1 test's synthetic fixture. The throwaway `.cpp` was
  deleted again and `ScreenPassAutoWireProbeGame.cpp` was restored byte-for-byte to its pre-test content
  (`Projects/` is `.gitignore`d; `git status` confirmed zero tracked-file change throughout this whole
  reproduction).

## Design decisions resolved this phase (no `ask_questions` needed)

1. **How to literally "re-trigger the scaffolding tool for that SAME name"** against an already-scaffolded
   `*ScreenPass.cpp` file. Direct reading of `EditorProjectLifecycleCapability::CreateAssetScaffold()`
   confirmed its `ScreenPostProcessPass` branch checks `std::filesystem::exists(filePath)` and returns a
   `400`-mapped failure *before ever calling* `TryAutoWireRegisterCall()` — so literally re-POSTing
   `create_asset` for one of the fixture's three original, permanent `*ScreenPass.cpp` files (or even a
   freshly-scaffolded one, left on disk) can never reach the auto-wire step a second time. Resolved by
   scaffolding a brand-new, explicitly throwaway pass name (`Phase10ReloadProbe` — the task doc's own
   explicitly offered alternative), deleting only its `.cpp` file (never its wiring) before re-triggering,
   so the exists-check passes and `TryAutoWireRegisterCall()` genuinely runs a second time against the
   exact partially-wired state (forward declaration active, call commented out) Finding 10(b) describes —
   a mechanically necessary adaptation of the task doc's own literal wording, not a weaker substitute: the
   real, production HTTP endpoint and the real, unmodified `TryAutoWireRegisterCall()` function are both
   exercised, against the real fixture file, with the real bug's exact precondition reproduced.
2. **Whether to update `readme.md`'s "Status" section in addition to `AGENTS.md`.** The task doc's own
   Step 3 item 5 only names `AGENTS.md`'s "Render Pass System" section — `readme.md` was left untouched,
   matching the task doc's own literal, narrower instruction.

## What remains genuinely open (restated honestly, not silently dropped)

- **Milestone 2 (R3, a real bindless resource system) and R8 (Atmosphere dirty-flag/change-detection
  optimization) remain explicitly, deliberately deferred to a future `better-render-pass-2` campaign** —
  per `PHASE0_MASTER_STRATEGY.md`'s own stated non-goals. This campaign's reflection work (PHASE1/2) makes
  a future bindless migration structurally easier (binding tables are now machine-derived, not
  hand-typed), but building the bindless array itself was never attempted here.
- **PHASE4's GPU Skinning gap**: no animated, GPU-skinned `.pmx` model exists in the current default test
  scene, and no HTTP route exists to import/instantiate one on demand — both real GPU Skinning dispatch
  call sites' migration is therefore verified by build success, structural/reflection-value confirmation,
  and the identical, already-proven-live `CullingPipelines` migration pattern, but never by a live,
  actually-animating skinned mesh. Still true after this phase; closing it would need a heavier,
  out-of-scope asset-import detour.
- **PHASE5's "Inspect Aerial Perspective LUT" button gap**: no generic "click this named ImGui button"
  HTTP route exists, so `CaptureAerialPerspectiveVolumeSliceImmediate()` could not be directly clicked
  this campaign either — mitigated by its byte-for-byte-unchanged body and shared pipeline/layout
  construction with the already-verified per-frame Debug Slice pass.
- **PHASE8's `PluginRenderResourceTranslation.cpp` gap**: the `IPluginRenderPassBuilder_v3` ABI's own
  `CreateTexture()` translation still never sets `TextureUsage::Storage`, even for its own pooled
  compute-storage-image outputs — a pre-existing gap this phase's own `TextureUsage` field made newly
  possible to close, but closing it would touch the plugin ABI surface, explicitly out of this whole
  campaign's scope. Left open, honestly flagged, for a future campaign.
- No other open issues. Every acceptance point in `PHASE0_MASTER_STRATEGY.md` and every one of its ten
  phase files is satisfied.

## Closeout

**`better-render-pass-1` is now CLOSED.** `better-render-pass-2` (Milestone 2 — a real bindless resource
system, R3 — plus the Atmosphere dirty-flag optimization, R8) may begin as its own, later, separate
campaign — nothing further is expected here.
