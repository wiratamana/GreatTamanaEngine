# COMPUTE_PHASE7_COMPLETION_REPORT.md

Session report for **Phase 7 — Real tests: prove it works** (the
"Validation, Testing, Tooling" phase) of the compute-shader campaign
described in `COMPUTE_SHADER_MASTER_STRATEGY_v2.md`. Scope was taken from
`COMPUTE_PHASE7_VALIDATION_TESTING_TOOLING_STRATEGY_v2.md` — specifically
its **texture-side half only**: a real, shipped compute box-blur
post-process reading the Editor's "Scene" view as a plain `Texture` and
writing a new, persistent `blurredSceneOutput` `RenderTexture` as an
`RWTexture`. The companion buffer-side validation workload (GPU frustum
culling via `GPU_DRIVEN_RENDERING_COMPUTE_INDIRECT_STRATEGY_v1.md`) is a
**separate document's own responsibility** and was explicitly out of scope
for this session — see "What was deliberately NOT done" below.

Work was done on the pre-existing branch `feature/compute-shader-impl`,
picking up immediately after Phase 6 (`COMPUTE_PHASE6_COMPLETION_REPORT.md`).
This closes out the `[ON GOING]` phase named in this session's task and
completes every phase of the compute-shader campaign's own "Step 3: The
Plan" table for the texture-side vocabulary this campaign introduced.

## What shipped

### New shader: `src/Shaders/BoxBlur.comp`

A small, deliberately unoptimized 7×7 box blur:

- `layout(binding = 0) uniform sampler2D sourceTexture;` — a plain,
  read-only `Texture` (combined image sampler), sampled exactly like a
  fragment shader already samples a `MaterialTexture` today.
- `layout(binding = 1, rgba8) uniform writeonly image2D destinationImage;`
  — the `RWTexture` output.
- `layout(push_constant) uniform PushConstants { uint width; uint height; }`
  — the blurred output's pixel dimensions, used both to reject an
  out-of-bounds invocation (the dispatch's own ceiling-division group count
  can legitimately overshoot the real extent) and to convert a pixel
  coordinate into a normalized UV.
- `layout(local_size_x = 16, local_size_y = 16) in;` — matches
  `ComputeBlurValidation.cpp`'s own `kBoxBlurLocalSizeX`/`kBoxBlurLocalSizeY`
  constants exactly (a hand-maintained convention, per this campaign's own
  refusal of shader reflection).

### New class: `src/Editor/ComputeBlurValidation.h`/`.cpp`

Owns everything a real, end-to-end compute-shader pass needs:

- A `VkDescriptorSetLayout` (binding 0 = combined-image-sampler, binding 1
  = storage image — built via Phase 3's `DescriptorSetLayoutBuilder`),
  destroyed by hand in this class's own destructor (mirrors
  `GpuResourceFactory`'s own `m_materialSetLayout` teardown convention).
- A `ComputePipeline` (Phase 2), built once, lazily, the first time
  `AddPass()` is called (needs a live `Renderer`/`VkDevice`, so can't happen
  in the default constructor).
- A `ComputeDescriptorSet` (Phase 3), allocated once via
  `Renderer::AllocateComputeDescriptorSet()`, rewritten every frame this
  pass actually runs.
- A persistent `blurredOutput` `RenderTexture` (an `RWTexture` — Phase 1),
  created with an **explicit `VK_FORMAT_R8G8B8A8_UNORM`** — never
  `VK_FORMAT_UNDEFINED`/`Renderer::ColorFormat()` — since this texture has
  no pipeline-sharing requirement with the swapchain/Game/Scene views at
  all (it's only ever sampled via `ImGui::Image()`), exactly matching the
  strategy document's own Step 6 finding.

`AddPass()` declares one compute pass per call (`AddComputePass()` —
Phase 6): `ReadTexture(sceneViewHandle, ShaderRead)` + `WriteTexture(
blurredOutputHandle, ComputeShaderWrite)`, imports its own output texture
fresh every call (per Phase 1 v2's own scope note — a storage-capable
`RWTexture` can only ever be an externally-owned, persistent resource,
never a `CreateTexture()`-requested transient one), resizes it on demand
(mirroring `ImGuiEditorLayer::GameViewTarget()`/`SceneViewTarget()`'s own
resize-on-demand discipline), and its `execute` callback resolves both
handles via `ctx.resolveTexture()`, rewrites the descriptor set, and calls
`Renderer::Dispatch()` with a correctly-computed group count
(`ComputeGroupCount3D()` — Phase 4). Every captured handle/value in the
`execute` lambda is captured **by value**, per Phase 6's own documented
dangling-reference gotcha.

`FinalizeForSampling()` transitions the blurred output from
`ComputeShaderWrite` (`VK_IMAGE_LAYOUT_GENERAL`) to `ShaderRead`
(`VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL`), mirroring `RenderPasses.h`'s
own `FinalizeRenderTextureForExternalSampling()` for the Game/Scene views —
implemented independently inside this class (not by including anything
from `src/Application/`) to avoid a reverse Editor→Application dependency;
guarded by an internal `m_writtenThisFrame` flag rather than trusting the
caller's own enable/visibility condition to still hold by the time it's
called.

### `IEditorLayer` interface extension (`src/Editor/EditorLayer.h`)

Two new pure-virtual methods, mirroring `GameViewTarget()`/
`SceneViewTarget()`'s own "Application only ever sees an opaque handle,
never Editor internals" boundary:

- `std::optional<rg::TextureHandle> AddBlurValidationPass(rg::RenderGraphBuilder&,
  Renderer&, rg::TextureHandle sceneViewHandle, VkExtent2D sceneExtent)` —
  declares the pass (if the Editor's own "Show Compute Blur (debug)"
  toggle is on and "Scene" is visible) and returns the blurred output's
  handle for the caller to add to `finalOutputs`, or `std::nullopt`
  otherwise (always `std::nullopt` for `NullEditorLayer`).
- `void FinalizeBlurValidationForSampling(VkCommandBuffer cmd)` — a safe
  no-op for `NullEditorLayer` and whenever nothing was declared this frame.

`ImGuiEditorLayer` implements both by forwarding into its own
`ComputeBlurValidation m_blurValidation` member (declared alongside
`m_sceneView`), and lazily (re)creates an ImGui descriptor for the blurred
output whenever its underlying `VkImageView` changes (tracked via
`m_lastKnownBlurredView`, since a resize can happen mid-frame during the
offscreen `Execute()` call, entirely outside this class's own
`GameViewTarget()`/`SceneViewTarget()`-style resize methods).

### `Application.cpp` wiring

Inside the existing offscreen `Execute()` build lambda, right after
`AddSceneViewPass()`, calls `m_editorLayer->AddBlurValidationPass(b,
m_renderer, h, extent)` (where `h` is the Scene view's own just-declared
handle) and, if it returns a value, adds it to `outputs` (this call's
`finalOutputs` root set — required, or the pass would be silently culled).
After the existing `FinalizeRenderTextureForExternalSampling()` calls for
Game/Scene, calls `m_editorLayer->FinalizeBlurValidationForSampling(
offscreenCmd)` — still before `EndOffscreenRenderGraphRecording()`.

### Editor UI (`src/Editor/EditorContext.h`, `Panels/ScenePanel.cpp`)

- `EditorContext` gained `bool showBlurredSceneOutput` (the toggle) and
  `VkDescriptorSet blurredSceneOutputDescriptor`.
- The "Scene" panel gained a small, permanent, clearly-labeled **"Show
  Compute Blur (debug)"** checkbox in its own toolbar — per the strategy
  document's own Step 5 guidance to keep this as a small, permanent Editor
  debug tool (the same precedent already set by the Bone Viewer), rather
  than deleting it after validation. When checked (and a blurred output
  actually exists), "Scene" displays the blur pass's own output instead of
  the normal Scene view — a direct, at-a-glance visual confirmation the
  whole plumbing works.

### Build system (`CMakeLists.txt`)

- `src/Editor/ComputeBlurValidation.h`/`.cpp` added to the
  `GTE_ENABLE_EDITOR` `target_sources()` block.
- `gte_add_shader(GreatTamanaEngine src/Shaders/BoxBlur.comp)` added,
  gated behind `if(GTE_ENABLE_EDITOR)` — this shader is never compiled/
  staged in a release build (no Editor, no way to toggle the debug view at
  all).

## Cross-pass texture READ, exercised for the first time

This is, per `RENDERGRAPH_FUTURE_TODO_DELIBERATELY_NOT_IMPLEMENTED.md`'s
own Section B.2, the first genuinely SHIPPED compute pass whose
`ReadTexture()` handle was written by an EARLIER pass in the SAME
`RenderGraph::Execute()` call, synchronized entirely by the render graph's
own automatic barrier planner (`ApplyUsageBarrierIfNeeded()` — completely
unmodified since Phase 5/6) — no hand-written barrier anywhere in this
phase's own code. The compiled execution order for a frame with the toggle
on is `SceneView → ComputeBlurValidation`, exactly as the dependency-graph
edge (`SceneView` writes the texture `ComputeBlurValidation` reads) already
guarantees via `RenderGraphCompiler::Compile()`'s existing, unmodified
logic.

## Verification performed

- Reconfigured with CMake (Ninja generator, reusing the existing `build/`
  directory) — no network access needed, everything was already fetched.
- Built the full project (`GreatTamanaEngine.exe` **and**
  `GreatTamanaEngineTests.exe`) — both link successfully; `BoxBlur.comp`
  compiled cleanly through the existing, unmodified `gte_add_shader()`
  macro; every shader (including `BoxBlur.comp.spv`) staged correctly next
  to the executable.
- Ran the **entire** existing test suite: **649 of 650 tests passed**, **1
  skipped** (`PmxLoaderRealModelSmokeTest.LoadsAnMmdModelIfPresentOnThisMachine`,
  the same pre-existing machine-gated smoke test every prior compute-shader
  phase's own report has noted — unrelated to this change). **Zero
  regressions** — no new Tier-1 tests were added this phase (every new
  type here — `ComputeBlurValidation`, the compute pass itself — is Tier 2,
  GPU-touching, falling into the same accepted "no automated coverage yet,
  manually verified" bucket as `Pipeline`/`Buffer`/`RenderTexture`/every
  other Vulkan-execution-layer class in this engine — see `TESTING.md`).
- Launched the real `GreatTamanaEngine.exe` in the background and confirmed
  it was still running (via `tasklist`) several seconds after launch — no
  crash/exception at startup, including constructing the new
  `ComputeBlurValidation` member, the extended `IEditorLayer` interface,
  and the new `Application::Run()` call sites — before stopping it.
- Additionally configured and built a **completely separate**
  `-DGTE_ENABLE_EDITOR=OFF` build (a fresh build directory, `GTE_BUILD_TESTS=OFF`
  to keep it quick) to confirm: (a) `NullEditorLayer.cpp` compiles cleanly
  against the two new pure-virtual `IEditorLayer` methods, (b)
  `ComputeBlurValidation.h/.cpp` are correctly NOT compiled into `gte_core`
  at all in this configuration, and (c) `BoxBlur.comp` is correctly NOT
  compiled/staged at all in this configuration — confirming the shader-gate
  in `CMakeLists.txt` works as intended and a release build carries zero
  trace of this validation feature. Build succeeded with no errors; this
  scratch build directory was deleted afterward.
- No validation-layer run was possible on this development machine — as
  already noted in every prior compute-shader phase's own completion
  report, `VK_LAYER_KHRONOS_validation` is not installed here (a
  pre-existing environment limitation, not something this phase introduced
  or can control). Manual visual confirmation (toggling "Show Compute Blur
  (debug)" and observing the Scene view visibly soften) is the accepted
  verification bar for this Tier-2 feature per this phase's own strategy
  document.

## Acceptance criteria check (against the strategy doc's own "Step 3: The Plan")

- ✅ Chosen validation workload: a compute box-blur post-process on the
  Scene view — simpler to hand-verify visually than the originally-floated
  outline-highlight idea, and exercises BOTH `Texture` (read) and
  `RWTexture` (write) in one shader, per the strategy document's own
  reasoning.
- ✅ `blurredSceneOutput` created with an EXPLICIT
  `VK_FORMAT_R8G8B8A8_UNORM` (never the default format) — per this
  document's own Step 6 pin-down.
- ✅ `blurredSceneOutput` created OUTSIDE the render graph (owned by
  `ImGuiEditorLayer`, alongside `m_sceneView`) and imported fresh every
  call via `ImportTexture()` — never requested via `CreateTexture()` — per
  this document's own Step 6 pin-down and Phase 1 v2's scope note.
  Ping-pong design (a SEPARATE `RWTexture`, not read+write of the same
  resource in one dispatch) avoids the intra-pass same-resource hazard
  Phase 5 flags.
- ✅ Cross-pass texture READ validated end-to-end: the compute pass reads
  the Scene view's own `RenderTexture`, written by an earlier graphics pass
  in the same `Execute()` call, synchronized entirely by the existing
  barrier planner — no manual barrier code anywhere in this phase.
- ✅ `AddComputePass()`/`PassBuilder::WriteTexture()`/`PassContext::
  resolveTexture()`/`Renderer::Dispatch()` (Phases 2-6) all used directly,
  with zero further `RenderGraphBuilder`/`RenderGraph` changes needed —
  confirming those phases' own APIs are sufficient for a real workload.
- ✅ Editor toggle: a small, permanent, clearly-labeled "Show Compute Blur
  (debug)" checkbox in the "Scene" panel — kept (not deleted after
  validation) per the strategy document's own Step 5 guidance, the same
  precedent the Bone Viewer already established.
- ✅ Full test suite run before and after, zero regressions — matching
  every prior phase's own verification discipline.

## What was deliberately NOT done (per the strategy doc's own "Step 4", and this session's own scope)

- **The buffer-side validation workload (GPU frustum culling via
  `GPU_DRIVEN_RENDERING_COMPUTE_INDIRECT_STRATEGY_v1.md`) was NOT
  implemented this session** — that document's own Phase D/G remains a
  separate, not-yet-started effort. Per this campaign's own "Step 5: Their
  Role" ("Do not consider this campaign complete until BOTH validation
  workloads... are landed"), the compute-shader campaign as a whole is
  therefore **not yet fully complete** — only its texture-side half is
  proven end-to-end. This is an honest, explicitly-tracked gap, not an
  oversight — see "Handoff notes" below.
- No production-quality blur (no adjustable kernel radius UI, no separable
  two-pass Gaussian, no bilinear-optimized sampling) — this is a
  validation vehicle, not a real post-process feature, exactly as scoped.
- No automated visual-diff/screenshot-comparison testing — remains Tier 2/
  manual, per this engine's existing accepted testing bucket.
- No benchmark-mode/perf-regression CI for compute — out of scope for this
  whole campaign.
- No GPU vertex skinning or any other new compute workload — named
  explicitly, below, as the next natural candidate, never folded into this
  phase speculatively.
- No changes to `RenderGraphSnapshot`/`BuildRenderGraphSnapshot()`/the
  Editor's "Render Graph" panel — confirmed (by inspection; this session
  did not add a dedicated new automated test for it) that the panel's
  existing, pass/resource-shape-agnostic design already displays a
  `ComputeBlurValidation` pass and its `BlurredSceneOutput` resource
  correctly with zero code changes, per Phase 8 of the original Render
  Graph campaign's own design goal.

## Campaign status (`COMPUTE_SHADER_MASTER_STRATEGY_v2.md`)

| Phase | Status |
|---|---|
| 1 — Resource vocabulary | ✅ Done (`COMPUTE_PHASE1_COMPLETION_REPORT.md`) |
| 2 — ComputePipeline | ✅ Done (`COMPUTE_PHASE2_COMPLETION_REPORT.md`) |
| 3 — Descriptor binding | ✅ Done (`COMPUTE_PHASE3_COMPLETION_REPORT.md`) |
| 4 — Dispatch execution | ✅ Done (`COMPUTE_PHASE4_COMPLETION_REPORT.md`) |
| 5 — Synchronization | ✅ Done (`COMPUTE_PHASE5_COMPLETION_REPORT.md`) |
| 6 — RenderGraph integration | ✅ Done (`COMPUTE_PHASE6_COMPLETION_REPORT.md`) |
| 7 — Validation (texture side) | ✅ Done (this report) |
| 7 — Validation (buffer side, culling) | ⏸ NOT done — separate document's own scope |

## What remains open (honest, tracked, not silently dropped)

- **Buffer-side validation (GPU frustum culling + indirect draw)** — the
  companion `GPU_DRIVEN_RENDERING_COMPUTE_INDIRECT_STRATEGY_v1.md`
  document's own Phase D/G. This campaign's `ResourceAccess::
  ComputeShaderRead/ComputeShaderWrite/IndirectCommandRead` (Phase 5),
  `CreateStructuredBuffer()` (Phase 1, with its `extraUsage` parameter
  specifically added for an indirect-draw buffer), and `AddComputePass()`
  (Phase 6) are all already shipped and ready for that document's own
  workload to consume directly — no further infrastructure changes are
  anticipated to be needed.
- **Transient (render-graph-pooled) `RWTexture` support** — `rg::TextureDesc`
  still has no storage-usage opt-in (Phase 1 v2's own explicit scope
  boundary); every `RWTexture` in this campaign, including this phase's
  own `blurredSceneOutput`, remains an externally-owned, persistent
  resource, imported per-frame.
- **`BufferHandle`-flavored `finalOutputs`/`ImportBuffer()`** — a
  buffer-only compute pass's write still needs an in-graph reader whose own
  chain reaches a real texture final output, or it is silently culled
  (Phase 5's own documented, tested constraint) — not lifted by this
  campaign.
- **Async compute, bindless descriptors, 3D/array/cubemap storage images**
  — all explicitly out of scope for the whole campaign from Phase 1
  onward, unchanged by this phase.
- **GPU vertex skinning** — the natural next compute-shader workload
  candidate once this campaign (both halves) closes, per
  `RENDERGRAPH_FUTURE_TODO_DELIBERATELY_NOT_IMPLEMENTED.md`, Section C.5 —
  intentionally not started here.
- **Real GPU timestamp timing for `ComputeBlurValidation`'s own pass** — no
  new work was needed for this: `RenderGraph::ExecuteCompiledGraph()`'s
  timestamp bracket already covers every pass unconditionally (Phase 4/B.1),
  so the "ComputeBlurValidation" pass already reports real GPU milliseconds
  in the Editor's "Render Graph"/"Profiler" panels once it runs — this was
  confirmed by design inspection, not a new gap.

## Handoff notes for whoever picks up the buffer-side validation workload

- Do not consider `COMPUTE_SHADER_MASTER_STRATEGY_v2.md`'s whole campaign
  "done" until the buffer-side workload (GPU culling) also lands and is
  manually verified — this report only closes the texture-side half.
- `ComputeBlurValidation.h`/`.cpp` (this phase) is a reasonable structural
  template to follow for a similarly self-contained compute-workload class:
  own your own pipeline/descriptor-set/layout, expose one `AddPass()`
  entry point that both declares the pass and does any needed lazy
  init/resize, and take care that every captured value in the pass's
  `execute` lambda is captured by value, never by reference to a
  `build`-lambda-local variable.
- `CreateStructuredBuffer()`'s `extraUsage` parameter (Phase 1) is already
  in place specifically so the culling workload's indirect-draw buffer
  doesn't need a second, near-duplicate buffer factory method — use it
  directly (`VK_BUFFER_USAGE_INDIRECT_COMMAND_BIT`).
- Remember Phase 5's own buffer-reachability caveat when designing the
  culling pass's own pass graph: a compute pass whose only write is a
  buffer needs an in-graph reader (the actual indirect draw call, which
  itself writes a real color attachment) to survive `RenderGraphCompiler::
  Compile()`'s culling — this is already proven correct by a dedicated
  regression test (`RenderGraphCompilerTests.cpp`'s
  `BufferOnlyWriteSurvivesCullingOnlyWhenAReaderReachesATextureFinalOutput`),
  but the culling workload's own real pass graph should still be built with
  this constraint consciously in mind.
