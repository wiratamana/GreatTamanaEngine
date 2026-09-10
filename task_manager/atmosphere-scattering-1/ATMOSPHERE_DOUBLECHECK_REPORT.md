# Atmosphere Scattering Campaign — Double-Check Report (2026-09-10)

**Scope:** a second-iteration documentation review of all 10 strategy files
in `task_manager/atmosphere-scattering-1/` (Phase 0 orchestrator + Phases
1-9), cross-checking every concrete technical claim against the REAL current
engine source (never the documents' own paraphrase). No engine code was
implemented or modified in this task — this was a review/correction pass
only, exactly as instructed. Branch stayed on
`feature/atmosphere-scattering-impl` throughout.

## What was read in full before reviewing

- `README.md`, `AGENTS.md` (project root), both in full.
- All 10 `ATMOSPHERE_PHASEn_*.md` files, in full.
- `ATMOSPHERE_PHASE2_PRECHECK_REPORT.md` (an earlier, narrower review pass
  that already fixed up Phase 2 specifically) — read first, per the task's
  own instruction, so this pass built on it instead of duplicating it.
- Real engine source, in full or in the relevant part, including:
  `src/Editor/ComputeBlurValidation.h/.cpp`, `src/Shaders/BoxBlur.comp`,
  `cmake/CompileShaders.cmake`, `src/Renderer/Texture2D.h/.cpp`,
  `src/Renderer/RenderTexture.h`, `src/Renderer/Buffer.h`,
  `src/Renderer/DepthBuffer.h/.cpp`, `src/Renderer/Vulkan/
  DescriptorSetLayoutBuilder.h`, `src/Renderer/ComputeDescriptorSet.h`,
  `src/Renderer/RenderGraph/RenderGraphTypes.h`, `src/Renderer/RenderGraph/
  RenderGraphBarrierPlanner.h`, `src/Renderer/RenderGraph/RenderGraph.h`,
  `src/Renderer/GpuSkinning/GpuSkinningTypes.h`, `src/ECS/Components/
  Camera.h`, `src/Game/RenderSystem.h/.cpp`, `src/Editor/Panels/
  HierarchyPanel.cpp`, `src/Editor/GpuSkinningValidation.h`,
  `src/Editor/SceneGridRenderer.h`, `src/Application/RenderPasses.h/.cpp`,
  `src/Application/Application.cpp` (relevant sections), `tests/CMakeLists.txt`.
- `search_in_dir` sweeps for `VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER`,
  `BufferMemoryUsage`, `RenderPasses`, `AddComputePass`, `hasDepth`,
  `finalOutputs`, `Create 3D Object`, `ResolveActiveCameraViewProjection`,
  `clearValue.depthStencil` to independently confirm every non-obvious claim
  before trusting it.

## Overall verdict

The campaign's documents were already in good shape (Phase 2 had already
been through a rigorous precheck pass). This second pass found the plan's
**high-level architecture and phase breakdown were sound**, but uncovered
**three concrete, load-bearing technical gaps** that would have caused a
real implementer to hit a wall (or introduce a hidden correctness bug)
partway through Phase 3 and Phase 7 specifically. All three are now fixed
in place, with corrections propagated to every downstream phase that
inherits the same assumption.

## Findings and fixes, by file

### `ATMOSPHERE_PHASE0_MASTER_STRATEGY_v1.md` — reviewed, left unchanged

Re-read in full. Its claims (no atmosphere/sky/fog today, the Render Graph's
2D-texture-only vocabulary, the compute-shader stack precedent via
`ComputeBlurValidation`/`BoxBlur.comp`, the `GET /get_texture` auto-capture
behavior, the Locked Design Decisions, the document map, the cross-cutting
rules) were all re-confirmed against the same real source used to review
its children. No incorrect or insufficient claim was found. Left completely
unchanged — this is the orchestrator document and none of the corrections
below require changing its own text (they were resolved at the child-phase
level, per its own "if a phase's plan disagrees with reality, note the
discrepancy in that phase's own report" rule).

### `ATMOSPHERE_PHASE1_REFERENCE_ANALYSIS_AND_PHYSICAL_MODEL_FOUNDATIONS_v1.md` — Revision Notes added

Confirmed accurate: `cmake/CompileShaders.cmake`'s `gte_add_shader(TARGET
SOURCE)` really does pass only `SOURCE` in its `DEPENDS` list (no
include-file tracking), confirming 3.2's plan to fix this is genuinely
necessary; `src/Renderer/GpuSkinning/` really does split into
`GpuSkinningTypes.h/.cpp` (pure data) + `GpuSkinningPipelines.h/.cpp`
(live-`VkDevice` orchestration) exactly as cited, a good model for
`src/Renderer/Atmosphere/`; `tests/CMakeLists.txt`'s `GTE_TEST_SOURCES` is
confirmed to be a flat list of relative paths, matching 3.5's plan for a new
`tests/Renderer/Atmosphere/AtmosphereMathTests.cpp` entry.

**Added:** a Revision Notes section flagging, for continuity, the real gap
found and fixed in Phase 3 (below) that touches this phase's own 3.3: the
engine has no real `VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER` support anywhere
today, so `AtmosphereParametersGpu`/`AtmosphereFrameUniforms` will actually
bind as read-only STORAGE buffers, not true `uniform` blocks — this does not
change 3.3's own std140-oriented padding guidance in practice (std140 and
std430 produce identical padding for a flat, array-free struct), so no
further edit to 3.3's own body text was needed beyond this pointer.

### `ATMOSPHERE_PHASE2_VOLUME_TEXTURE_RENDERGRAPH_SUPPORT_v1.md` — reviewed again, left unchanged

This file was already corrected by the earlier, narrower precheck pass
(`ATMOSPHERE_PHASE2_PRECHECK_REPORT.md`) — its major finding (the
`if`/`else`, not exhaustive-switch, `ResourceKind` branching in
`RenderGraphCompiler.cpp`/`RenderGraph.cpp`, and the real out-of-bounds
`EnsureBufferResolved()` risk) and two smaller signature/naming fixes were
already applied in place. This pass re-read the corrected document plus the
same real source files it cites (`RenderGraphTypes.h`, `RenderGraphBuilder.h`,
`RenderGraphBarrierPlanner.h`, `RenderTarget.h`, `Texture2D.h/.cpp`,
`GpuMemoryTracker.h`) and found nothing further to fix — every corrected
claim still holds, and no new gap was found. **Left completely unchanged**,
per the task's own "if a file is already good enough as-is, leave it
unchanged" rule.

### `ATMOSPHERE_PHASE3_TRANSMITTANCE_LUT_v1.md` — two real gaps found and fixed

This was the phase with the most substantive corrections, because it is the
FIRST phase that actually has to bind `AtmosphereParametersGpu` to a
descriptor set and create the Transmittance LUT's own output texture — every
later LUT phase (4, 5, 6) inherits whatever convention this phase
establishes.

1. **(Major) No real uniform-buffer descriptor support exists in the engine
   today.** The original document's Step 2/Step 3 repeatedly describe
   binding `AtmosphereParametersGpu` as a GLSL `uniform` block via a
   descriptor set binding. A direct search of `src/` for
   `VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER` returned **zero** hits anywhere in
   this codebase. `src/Renderer/Vulkan/DescriptorSetLayoutBuilder.h` only
   has `AddStorageBuffer()`/`AddStorageImage()`/`AddCombinedImageSampler()`
   (no `AddUniformBuffer()`), and `src/Renderer/ComputeDescriptorSet.h`'s
   `ComputeDescriptorWrite` only has `StorageBuffer()`/`StorageImage()`/
   `CombinedImageSampler()` factories (no `UniformBuffer()`). An
   implementer following the document literally would have had no actual
   API to call. **Fixed:** the document now explicitly resolves this by
   directing the implementer to bind `AtmosphereParametersGpu` as a
   read-only STORAGE buffer instead (`AddStorageBuffer()`/
   `ComputeDescriptorWrite::StorageBuffer()`, GLSL `readonly buffer`,
   `VK_BUFFER_USAGE_STORAGE_BUFFER_BIT`) — this exactly mirrors the GPU
   Vertex Skinning campaign's own proven bone-matrix-buffer precedent and
   needs zero new engine plumbing. An alternative (adding genuine
   `AddUniformBuffer()` support) is also noted as valid but not the
   recommended default. This same resolution is now cross-referenced from
   Phases 4, 5, and 6 (each of which also binds `AtmosphereParametersGpu`
   and/or `AtmosphereFrameUniforms`) rather than re-litigated per phase.
2. **(Real gap) `Texture2D` cannot actually support the format flexibility
   the plan implicitly assumed.** The original document said to "prefer
   `Texture2D` with `allowStorageImageAccess=true` over `RenderTexture`"
   for the LUT's output texture, without mentioning that `Texture2D`
   (`src/Renderer/Texture2D.h/.cpp`) is **hard-locked to
   `VK_FORMAT_R8G8B8A8_UNORM`** — it has no `format` constructor parameter
   at all, unlike `RenderTexture`, which does take one (at the cost of an
   always-created, here-unused companion `DepthBuffer`). **Fixed:**
   confirmed this specific choice is still fine for THIS LUT (a
   transmittance value is always in `[0, 1]` by definition), but the
   document now explicitly warns that Phase 4/5's own LUTs store HDR
   (unbounded) values and must NOT copy this same choice — they need
   `RenderTexture` with an explicit float format instead, and must record
   that decision explicitly in their own completion reports. Phases 4 and 5
   now each make this decision explicitly in their own text (see below).

A short "Revision Notes" section was added near the top of the file
explaining both corrections, mirroring `GPU_SKINNING_PHASE0_MASTER_STRATEGY_v2.md`'s
own "Why v2 exists" style, while keeping the original filename unchanged.

### `ATMOSPHERE_PHASE4_MULTISCATTERING_LUT_v1.md` — corrections propagated

Fixed the same "uniform buffer" wording to "read-only storage buffer" in
Step 3's binding description, and added an explicit, concrete instruction
that this LUT's own output texture must use `RenderTexture` with an
explicit HDR float format (e.g. `VK_FORMAT_R16G16B16A16_SFLOAT`, matching
Phase 2's own `VolumeTexture` format choice) rather than `Texture2D` —
since a multi-scattering "response" value can legitimately exceed `1.0`.
Added a short Revision Notes pointer at the top cross-referencing Phase 3's
own corrected reasoning rather than duplicating it. Everything else in this
document (the two-sample-count math description, the render-graph
read-dependency ordering reasoning) was confirmed accurate.

### `ATMOSPHERE_PHASE5_SKYVIEW_LUT_v1.md` — corrections propagated

Same two fixes as Phase 4: both `AtmosphereParametersGpu` and this phase's
own new `AtmosphereFrameUniforms` are now described as read-only storage
buffers, and the Sky-View LUT's own output texture is now explicitly
required to use `RenderTexture` with an HDR float format (never `Texture2D`)
— the sky's own near-sun radiance routinely exceeds `1.0`, and clamping it
to 8-bit UNORM would visibly clip/band the one LUT the Sky Background pass
(Phase 7) samples directly for on-screen color. Confirmed accurate and
unchanged: `RenderSystem::ResolveActiveCameraViewProjection()`'s real
signature/behavior, and the Game-View-vs-Scene-View dual-camera situation
this phase's per-view LUT decision is built around.

### `ATMOSPHERE_PHASE6_AERIAL_PERSPECTIVE_FROXEL_VOLUME_v1.md` — one correction propagated

Fixed the same buffer-binding wording (both `AtmosphereParametersGpu` and
`AtmosphereFrameUniforms` bind as read-only storage buffers, never
`uniform` blocks) in Step 3's binding description. This phase's own
`VolumeTexture`/`rgba16f` format choice was already correct and unaffected
(Phase 2 already gives `VolumeTexture` an explicit float format parameter —
it was never subject to the `Texture2D` hard-lock issue at all). Everything
else (the column-major intra-thread-Z-loop design, the disposable
CPU-readback validation plan) was confirmed accurate against Phase 2's own
corrected document and the real `RenderGraphBuilder`/`VolumeTexture` API
shape it depends on.

### `ATMOSPHERE_PHASE7_SKY_BACKGROUND_AND_COMPOSITE_PASSES_v1.md` — two hedges resolved into concrete, confirmed answers

This phase's original document already correctly IDENTIFIED two areas of
uncertainty and explicitly told the implementer to "confirm" them against
real source before proceeding — this pass did that confirmation work now,
so the implementing session doesn't have to discover either fact for
itself under time pressure:

1. **Depth convention for the Sky Background pass.** Confirmed directly
   from `src/Renderer/FrameRecorder.cpp`
   (`depthAttachment.clearValue.depthStencil = { 1.0f, 0 }`) and
   `src/Editor/SceneGridRenderer.h`'s own comment ("`VK_COMPARE_OP_LESS`,
   matching every other pipeline in this engine"): depth is cleared to
   `1.0` (the far plane) every frame, and every real pipeline in this
   engine depth-tests with `VK_COMPARE_OP_LESS`. The document now gives a
   concrete instruction instead of a hedge: draw the Sky Background pass's
   full-screen triangle at a fixed NDC depth of exactly `1.0`, with the
   pipeline's own depth-compare op set to `VK_COMPARE_OP_EQUAL`.
2. **(Real, confirmed gap) The view's depth buffer cannot actually be
   sampled today.** The original document already flagged uncertainty here
   ("confirm the engine's depth attachment can be bound as a sampled
   input... check `DepthBuffer.h`'s real capabilities before assuming
   direct sampling works"). Reading `src/Renderer/DepthBuffer.h/.cpp`
   directly confirms the answer is **no**: its `VkImage` is created with
   ONLY `VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT` (no
   `VK_IMAGE_USAGE_SAMPLED_BIT`), and it never creates a `VkSampler` at all
   — its own class comment says so outright. The Aerial Perspective
   Composite pass's planned `sampler2D sourceDepth` binding will not
   compile/work as originally written. **Fixed:** the document now gives a
   concrete, recommended fix — add an `allowSampledAccess` constructor
   parameter to `DepthBuffer` (default `false`, every existing call site
   unaffected), mirroring `Texture2D`/`RenderTexture`'s own existing
   `allowStorageImageAccess` opt-in convention exactly, that additionally
   ORs in `VK_IMAGE_USAGE_SAMPLED_BIT` and creates a real (nearest-filter)
   `VkSampler` — plus confirmation that
   `RenderGraphBarrierPlanner::RequiredStateFor()` already treats a depth
   `ShaderRead` identically to a color one, so zero barrier-planner changes
   are needed beyond this.

A Revision Notes section was added near the top summarizing both, and the
corresponding Step 2/3.2/3.3 body text was updated in place with the
concrete answers. Everything else in this document (the
`RenderPasses.h/.cpp` precedent, `NotifyDebugTextureStateOverride()`'s four
existing call sites, the blur-toggle descriptor-swap pattern in
`ImGuiEditorLayer.cpp`/`Panels/GamePanel.cpp`/`ScenePanel.cpp`) was confirmed
accurate against the real source.

### `ATMOSPHERE_PHASE8_SUN_ECS_AND_EDITOR_CONTROLS_v1.md` — reviewed, left unchanged (Revision Notes added confirming this)

Confirmed accurate: `Camera` (`src/ECS/Components/Camera.h`) really is the
plain-data-plus-two-pure-helper-methods shape the document asks to mirror
for `DirectionalLight`; `RenderSystem::
ResolveActiveCameraViewProjection(Registry&, float)`'s real signature and
"first active, `ComponentStorage` order, fall back to a sane default"
behavior matches exactly what 3.2's own resolution-helper plan assumes;
`Panels/HierarchyPanel.cpp`'s "Create 3D Object" is confirmed to be a
top-level `ImGui::BeginMenu()` entry inside the right-click context menu
(`BeginPopupContextWindow()`), confirming a parallel "Create Directional
Light" entry (or sibling top-level entry) fits the existing structure
naturally, exactly as the document already hedged ("whichever reads more
naturally... check it directly"). No incorrect, insufficient, or missing
claim was found. A short Revision Notes note was added recording this
confirmation; no other body text was changed.

### `ATMOSPHERE_PHASE9_VALIDATION_DEBUG_TOOLING_AND_DOCS_v1.md` — reviewed, left unchanged (Revision Notes added confirming this)

Confirmed accurate: `src/Editor/GpuSkinningValidation.h/.cpp` really is the
exact Editor-only, self-contained-`Renderer::ImmediateSubmit()`,
no-`RenderGraph`-dependency shape this phase's own 3.1 asks to mirror for
`AtmosphereTransmittanceLutValidation` — a good precedent to copy
faithfully. This phase's own readback approach (a plain
`vkCmdCopyImageToBuffer` image copy, not a descriptor-bound read) is
unaffected by the binding-type/depth-sampling corrections made to Phases
3/7 above — those are simply prerequisites this phase can assume are
already landed and working by the time it starts, per the normal
one-phase-at-a-time campaign order. No incorrect, insufficient, or missing
claim was found beyond that cross-reference note. Left otherwise unchanged.

## Summary table

| File | Outcome |
|---|---|
| Phase 0 (Master Strategy) | Reviewed, no issues found, **left unchanged** |
| Phase 1 (Reference Analysis) | Reviewed, accurate; **Revision Notes added** (cross-reference to Phase 3's fix) |
| Phase 2 (Volume Texture) | Already corrected by prior precheck; re-reviewed, **left unchanged** |
| Phase 3 (Transmittance LUT) | **Two real gaps found and fixed** (no UBO support; `Texture2D` format lock) — substantive edits + Revision Notes |
| Phase 4 (Multi-Scattering LUT) | Corrections propagated (buffer type + HDR format decision) + Revision Notes |
| Phase 5 (Sky-View LUT) | Corrections propagated (buffer type + HDR format decision) + Revision Notes |
| Phase 6 (Aerial Perspective Volume) | Correction propagated (buffer type only — format was already correct) + Revision Notes |
| Phase 7 (Sky Background + Composite) | **Two hedges resolved into confirmed, concrete answers** (depth compare-op; depth-buffer sampling gap + fix) — substantive edits + Revision Notes |
| Phase 8 (Sun ECS + Editor Controls) | Reviewed, accurate, **left unchanged** (Revision Notes confirms this) |
| Phase 9 (Validation, Debug Tooling, Docs) | Reviewed, accurate, **left unchanged** (Revision Notes confirms this) |

No new `.md` strategy files were created. Every edit was made in place to
one of the 10 existing files (filenames unchanged), plus this one new report
file, per the task's own rules.

## Recommendation for the implementing sessions

Phase 3's implementer should read its corrected Step 2/Step 3 carefully
before writing any descriptor-set code — the storage-buffer-vs-uniform-buffer
decision made there is now the fixed convention every later phase (4-8)
assumes without re-deriving it. Phase 7's implementer should treat the
`DepthBuffer::allowSampledAccess` addition as a small, required, early
sub-step of that phase (not something to discover mid-phase) since the
Aerial Perspective Composite pass structurally depends on it.
