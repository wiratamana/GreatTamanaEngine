# COMPUTE_PHASE7_VALIDATION_TESTING_TOOLING_STRATEGY_v2.md

### Child document 7 of 7 — see `COMPUTE_SHADER_MASTER_STRATEGY_v2.md` for the full campaign map.

> **v2 (2nd-iteration review):** this document's Step 1-5 body is IDENTICAL to
> `COMPUTE_PHASE7_VALIDATION_TESTING_TOOLING_STRATEGY_v1.md`. New material was
> appended as **Step 6** below, after re-reading the blur workload's plan
> directly against the real, currently-shipped `Renderer::CreateRenderTexture()`
> default-format behavior and `RenderGraphBuilder`/`RenderGraphResourcePool`.
> Read Step 6 before building `blurredSceneOutput` — it pins down two things
> v1 leaves ambiguous (exact format, exact creation path) that materially
> affect whether this workload even runs on some GPUs.

## Step 1: The Goal

Prove, end-to-end, that compute shaders are genuinely first-class
`RenderGraph` citizens for **both** resource families this campaign built —
buffers (`RWStructuredBuffer`/`StructuredBuffer`, validated via the
companion GPU-driven-rendering document's own frustum-culling workload) and
images (`RWTexture`/`Texture`, validated via a NEW, small, concrete
workload this phase introduces: a compute box-blur post-process) — and
extend the Editor's existing "Render Graph" panel/testing discipline to
cover compute passes cleanly.

## Step 2: The Situation

- `GPU_DRIVEN_RENDERING_COMPUTE_INDIRECT_STRATEGY_v1.md`'s own Phase G
  already plans a full validation pass for the BUFFER side of this
  campaign (GPU frustum culling, an indirect draw buffer, visually and
  numerically confirmed via the Editor's "Render Graph"/"Profiler"
  panels). This phase does not repeat that work — it is a prerequisite
  this phase depends on, not something to redo.
- Nothing in either this campaign or the companion document ever exercises
  an `RWTexture` — the culling workload is buffers-only by design (see
  that document's own "What We Will NOT Do"). Without a second, dedicated
  validation workload, "compute shaders are first-class citizens" would
  only be proven true for HALF of the four resource kinds this whole
  campaign set out to deliver (per the user's own original ask:
  `RWStructuredBuffer`/`StructuredBuffer`/`RWTexture`/`Texture`).
- `RENDERGRAPH_FUTURE_TODO_DELIBERATELY_NOT_IMPLEMENTED.md`'s own Section
  B.2 flags that this engine's render graph has NEVER exercised a genuine
  cross-pass READ of a texture written by an earlier pass in the same
  frame — Dear ImGui currently samples the Game/Scene `RenderTexture`s
  entirely OUTSIDE the graph's own resource model
  (`FinalizeRenderTextureForExternalSampling()` in `RenderPasses.cpp` is a
  manual workaround for exactly this gap). A compute pass that WRITES an
  `RWTexture`, followed by a graphics (or compute) pass that READS it as a
  plain `Texture`, is a second, independent, and arguably more direct proof
  of this exact capability than the originally-suggested outline-highlight
  post-process — this phase's chosen validation feature closes that gap as
  a side effect.
- `RenderGraphSnapshot`/`BuildRenderGraphSnapshot()` (`src/Renderer/RenderGraph/RenderGraphSnapshot.h/.cpp`,
  the data model behind the Editor's "Render Graph" panel) was deliberately
  built pass/resource-shape-agnostic from day one (Phase 8 of the original
  campaign) — it should require zero changes to correctly display a
  compute pass and its buffer/image resources, but this must be CONFIRMED
  by actually looking at the panel with a real compute pass declared, not
  assumed from the doc comment alone.

## Step 3: The Plan

### The chosen validation workload: a compute box-blur post-process on the Scene view

- **Why blur, not the originally-floated outline-highlight idea:** a blur
  is simpler to hand-verify visually (an obviously softer image vs. a
  crisp one, no picking/collider system needed — unlike an outline
  highlight, which per `TODO.md`'s own "Editor / Debug UI" section still
  needs a whole click-to-select raycasting system this engine doesn't have
  yet), and it naturally demonstrates BOTH `Texture` (read) and
  `RWTexture` (write) in a single, small shader — exactly the two
  resource kinds this campaign's own Phase 1 introduced and nothing else
  in this whole campaign exercises together.
- **Shape of the workload** (a ping-pong design, deliberately avoiding the
  same-resource read+write intra-pass hazard flagged in Phase 5's own
  caveat):
  1. A NEW, dedicated `RenderTexture` (or plain storage-capable
     `Texture2D`), `blurredSceneOutput`, sized to match the Scene view's
     own `RenderTexture` — created with `allowStorageImageAccess = true`
     (Phase 1). **[v2] See Step 6 for the exact format/creation-path this
     must use — do not use the default format.**
  2. `Shaders/BoxBlur.comp` — `layout(binding = 0) uniform sampler2D
     sourceTexture;` (a plain `Texture` read of the Scene view's own
     already-rendered `RenderTexture`, sampled normally, NOT as a storage
     image — the Scene `RenderTexture` itself needs no `STORAGE_BIT` at
     all, since it's only ever READ here) and
     `layout(binding = 1, rgba8) uniform writeonly image2D
     destinationImage;` (the `RWTexture`, `blurredSceneOutput`). One
     thread per output pixel, `imageStore`s a simple NxN box-average of
     `texelFetch`/`texture()` samples around the corresponding source
     pixel. Deliberately simple — this is a validation vehicle, not a
     production-quality blur (no separable two-pass optimization, no
     bilinear-sampling tricks) — see "What We Will NOT Do" below.
  3. Declared as a compute pass (Phase 6's `AddComputePass()`/`AddPass()`):
     `pass.ReadTexture(sceneViewHandle, ShaderRead)` (a NORMAL sampled
     read — not `ComputeShaderRead`, since this binding is a combined-
     image-sampler, exactly like a fragment shader's read of any other
     `Texture` today) and `pass.WriteTexture(blurredOutputHandle,
     ComputeShaderWrite)`.
  4. Dispatched via `Renderer::Dispatch()` (Phase 4) with `groupCountX/Y =
     ComputeGroupCount(width/height, localSizeXY)` (Phase 4's own tested
     math), using a descriptor set built via `DescriptorSetLayoutBuilder`
     (Phase 3) with one combined-image-sampler binding + one storage-image
     binding.
  5. Displayed via a temporary Editor toggle (e.g. a checkbox in the
     Scene panel's toolbar, or a debug-only key binding) that swaps which
     `RenderTexture`/`Texture2D` the Scene panel's `ImGui::Image()` call
     samples from — `blurredSceneOutput` instead of the normal Scene
     `RenderTexture` — purely so the effect is visually confirmable by a
     human without needing a pixel-diffing tool. This toggle may be
     removed once validation is complete, or kept as a small, permanent,
     clearly-labeled "debug: compute blur test" Editor feature — decide
     based on whether it's judged useful enough to keep around as living
     documentation of the capability (a real precedent for this kind of
     "kept because it's a good living demo" decision would need sign-off,
     but is not unreasonable given this engine's Editor already carries
     several debug-only tools, e.g. the Bone Viewer).
- **Cross-pass READ validation, explicitly called out**: this workload's
  compute pass reading the Scene view's OWN `RenderTexture` (written by an
  earlier `SceneView` graphics pass in the SAME synchronous offscreen
  regime — see `RenderPasses.cpp`'s `AddSceneViewPass()`) is, itself, the
  first genuine "pass B declares a `ReadTexture()` of pass A's declared
  output, and the barrier planner alone makes it correct" exercise this
  engine's render graph has ever had — confirm, via the Editor's "Render
  Graph" panel, that this dependency shows up correctly as a real edge
  (the compute pass ordered strictly after `SceneView`, never culled), and
  that validation layers report zero warnings for the
  `ColorAttachmentWrite → ShaderRead` transition on the Scene texture.
- **Testing tiering** (mirrors `TESTING.md`'s existing split exactly):
  - Tier 1 (pure, no live device): `ComputeGroupCount()` (Phase 4,
    already covered), any pure blur-KERNEL-weight math if the shader ever
    grows beyond a flat box average (not needed for this validation
    workload's simple box blur).
  - Tier 2 (manual, live device, validation layers): `ComputePipeline`
    creation, descriptor set correctness, the actual dispatch and its
    visual output — same accepted bucket `Buffer`/`RenderTexture`/`Pipeline`
    already occupy, per `TESTING.md`'s own "Tier 2" note. Do not treat the
    absence of automated Tier-2 coverage as a blocker, per `AGENTS.md`'s
    explicit "Testability & Regression Safety" guidance on this exact
    point.
  - Full `ctest` run before and after every phase in this whole campaign,
    exactly like every prior Render Graph phase's own "Verification
    performed" section.
- **Editor tooling**: confirm (do not modify unless a real gap is found)
  that `RenderGraphSnapshot`/`BuildRenderGraphSnapshot()`/the "Render
  Graph" panel already correctly displays: the new compute pass by name,
  its declared reads/writes as resource-name chips (including the NEW
  `blurredSceneOutput` buffer/texture resource and its computed lifetime),
  and (once GPU timing is wired for the graph — see
  `RENDERGRAPH_FUTURE_TODO_DELIBERATELY_NOT_IMPLEMENTED.md`, Section B.1,
  already closed per that document's own notes) a real per-pass GPU
  millisecond figure for the dispatch itself, letting a developer directly
  compare the blur pass's cost against the `SceneView`/`Present` passes it
  sits alongside.

## Step 4: What We Will NOT Do

- No production-quality blur (no separable two-pass Gaussian, no adjustable
  kernel radius UI, no bilinear-optimized sampling) — this is a validation
  vehicle for the compute-shader/RenderGraph plumbing, not a real visual
  feature request. If a real blur/bloom/post-process feature is wanted
  later, it should be scoped as its own, separate strategy document, reusing
  this campaign's infrastructure rather than growing out of this validation
  shader in place.
- No automated visual-diff/screenshot-comparison testing — this remains
  Tier 2/manual, per this engine's existing accepted testing bucket.
- No benchmark-mode/perf-regression CI for compute — out of scope for this
  campaign entirely (see the master document's campaign-wide refusals).
- No folding of GPU vertex skinning (the actual, real, already-identified
  next compute workload — see `RENDERGRAPH_FUTURE_TODO_DELIBERATELY_NOT_IMPLEMENTED.md`,
  Section C.5) into this validation phase — name it explicitly as the next
  candidate in the eventual campaign completion report, never attempt it
  here.

## Step 5: Their Role

- Do not consider `COMPUTE_SHADER_MASTER_STRATEGY_v2.md`'s whole campaign
  complete until BOTH validation workloads — the companion document's own
  buffer-side culling (its own Phase G) and this phase's texture-side blur
  — are landed, manually verified with validation layers clean, and
  visually confirmed correct. Passing only one is proof of half the
  original ask, not the whole thing.
- Once both are done, write `COMPUTE_SHADER_CAMPAIGN_COMPLETION_REPORT.md`
  (as instructed by the master document), explicitly listing GPU vertex
  skinning as the next natural compute workload candidate, and explicitly
  listing async compute/bindless descriptors/3D-or-array-texture support,
  AND transient-`RWTexture`-pooling/buffer-final-outputs [v2] as
  knowingly-deferred, not forgotten.
- Remove any throwaway debug toggles/shaders introduced purely for
  validation (per Phase 2's own instruction on `Passthrough.comp`) unless a
  deliberate, documented decision is made to keep one as a permanent,
  clearly-labeled Editor debug tool — never leave an unlabeled, unexplained
  leftover code path behind.

---

## Step 6: V2 Revision Notes (2nd-Iteration Review)

Checked directly against the real, currently-shipped
`Renderer::CreateRenderTexture()` (`Renderer.h/.cpp`), `RenderTexture.cpp`
(default format `VK_FORMAT_B8G8R8A8_UNORM`), `Texture2D.cpp` (fixed format
`VK_FORMAT_R8G8B8A8_UNORM`), and `RenderGraphBuilder::ImportTexture()`
(`RenderGraphBuilder.cpp`) — two concrete pin-downs this phase needs before
`blurredSceneOutput` is built, both flagged at the master-document level
(Step 0, Finding 1 and Finding 4):

1. **`blurredSceneOutput` MUST be created with an explicit, known
   storage-compatible format — never the default.** v1's plan says only
   "created with `allowStorageImageAccess = true`," leaving the `format`
   argument unstated. `Renderer::CreateRenderTexture()`'s `format`
   parameter defaults to `VK_FORMAT_UNDEFINED`, which resolves internally
   to `Renderer::ColorFormat()` — whatever the swapchain actually
   negotiated at runtime (commonly `VK_FORMAT_B8G8R8A8_UNORM`, per
   `VulkanSwapchain.cpp`'s `ChooseSurfaceFormat`, but never guaranteed).
   `VK_FORMAT_FEATURE_STORAGE_IMAGE_BIT` support for `B8G8R8A8_UNORM`
   specifically is genuinely inconsistent across real Vulkan
   drivers/GPUs — some report it, some don't. If this workload is built
   using the default format, it may throw `std::runtime_error` at
   startup on some (but not all) development machines, purely because of
   which GPU happened to be used, which is a confusing, hard-to-reproduce
   failure mode for a validation feature whose entire point is to be
   simple to verify.
   - **Fix: create `blurredSceneOutput` with an EXPLICIT format —
     `VK_FORMAT_R8G8B8A8_UNORM`** (this engine's own already-proven,
     hardcoded `Texture2D` format — see `Texture2D.cpp`), not
     `VK_FORMAT_UNDEFINED`. There is no reason for `blurredSceneOutput` to
     match `Renderer::ColorFormat()` at all: per `AGENTS.md`'s "Render
     Target Format Matching" section, format-matching only matters when a
     `Pipeline` is shared between two targets — `blurredSceneOutput` is
     never bound to the SAME graphics `Pipeline` as the swapchain/Game/
     Scene views (it's written by a dedicated `ComputePipeline` and
     displayed via `ImGui::Image()`, which has no format-matching
     requirement of its own). Its matching GLSL qualifier is therefore
     always `rgba8`, unconditionally, with no runtime-dependent resolution
     needed at all — simpler than the general `RWTexture`-writing-into-
     Game/Scene-view case Phase 1 warns about, and this workload should
     say so explicitly rather than leave a reader to wonder whether it
     needs the same runtime-format-resolution dance.
   - A `SupportsStorageImageUsage()` check failing for `Renderer::
     ColorFormat()` specifically (Phase 1's own Step 5 manual-verification
     bullet) is therefore an EXPECTED, acceptable outcome on some GPUs —
     not a blocker for this phase, precisely BECAUSE this phase never
     actually needs that format to support storage images at all.
2. **`blurredSceneOutput` is created OUTSIDE the render graph and
   IMPORTED every frame — never requested via
   `RenderGraphBuilder::CreateTexture()`.** This follows directly from
   Phase 1 v2's own scope note (Step 0, Finding 1 /
   `COMPUTE_PHASE1_RESOURCE_VOCABULARY_STRATEGY_v2.md`'s Step 6): `rg::
   TextureDesc` has no storage-usage field, so `CreateTexture()` cannot
   produce a storage-capable transient resource today. v1's own plan
   already, IMPLICITLY, does the right thing here (describing
   `blurredSceneOutput` as "a NEW, dedicated `RenderTexture`" the same way
   `ImGuiEditorLayer`'s own `m_gameView`/`m_sceneView` are — persistent,
   owned outside the graph, resized alongside the Scene view) — this note
   simply makes that REQUIRED and EXPLICIT rather than an unstated
   assumption a reader could get wrong: create `blurredSceneOutput` once
   (e.g. owned by `ImGuiEditorLayer` right alongside `m_sceneView`, resized
   in lockstep with it), and bring it into each frame's offscreen
   `RenderGraph::Execute()` call via
   `builder.ImportTexture("BlurredSceneOutput", blurredSceneOutput.Target(),
   VK_IMAGE_LAYOUT_UNDEFINED)` (or whatever layout it was actually left in
   last frame — mirror `GameView`/`SceneView`'s own existing
   `ImportTexture()` call sites in `Application.cpp` exactly, including
   their `currentLayout` bookkeeping).
