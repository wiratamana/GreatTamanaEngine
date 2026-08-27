# COMPUTE_PHASE3_DESCRIPTOR_BINDING_MODEL_STRATEGY_v1.md

### Child document 3 of 7 — see `COMPUTE_SHADER_MASTER_STRATEGY_v1.md` for the full campaign map.
### Corresponds to the user's requested **"Module 2, Key Implementation Detail A: Creating Descriptor Layout & Pipeline"**.

## Step 1: The Goal

Give every future compute shader a **reusable, general way to build a
descriptor set layout** out of an arbitrary combination of the four
resource kinds from Phase 1 (`RWStructuredBuffer`/`StructuredBuffer` →
`VK_DESCRIPTOR_TYPE_STORAGE_BUFFER`, `RWTexture` →
`VK_DESCRIPTOR_TYPE_STORAGE_IMAGE`, `Texture` →
`VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER`), plus a matching descriptor
pool sized for compute-shaped descriptor types — **without hardcoding a
single fixed layout the way `GpuResourceFactory::MaterialDescriptorSetLayout()`
does for materials.** A future compute shader with, say, two input buffers
and one output image should be able to describe that combination in a few
lines, not by hand-writing a new `VkDescriptorSetLayoutCreateInfo` from
scratch every time.

## Step 2: The Situation

- `GpuResourceFactory`'s constructor already builds exactly ONE fixed
  descriptor set layout (`m_materialSetLayout`: one combined-image-sampler,
  fragment stage, binding 0) plus one matching descriptor pool
  (`m_materialDescriptorPool`, sized for `VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER`
  only, `kMaxMaterialTextures = 4096`). This is a real, working precedent
  for "build a layout once, allocate many sets against it from one pool,
  never free individual sets" — but it is single-purpose: nothing today
  can build a layout with, e.g., two storage-buffer bindings and one
  storage-image binding, because every binding is hand-written directly
  in `GpuResourceFactory`'s constructor body.
- No descriptor pool in this engine has ever requested
  `VK_DESCRIPTOR_TYPE_STORAGE_BUFFER` or `VK_DESCRIPTOR_TYPE_STORAGE_IMAGE`
  — `m_materialDescriptorPool`'s single `VkDescriptorPoolSize` entry only
  covers combined-image-samplers. A compute shader's descriptor set(s) need
  their own pool (or an extended one), sized for these two new descriptor
  types.
- There is no binding-number convention documented anywhere for compute
  shaders (unlike the material set, which has exactly one binding and
  therefore never needed a stated convention). Multiple resources in one
  set need a consistent, memorable ordering rule so the GLSL and C++ sides
  never silently disagree about which binding is which — and since this
  campaign explicitly refuses shader reflection (see the master document),
  this convention is enforced entirely by code review/comments, never
  tooling.
- `RenderGraphResourcePool` (`src/Renderer/RenderGraph/RenderGraphResourcePool.h/.cpp`)
  may hand back a DIFFERENT underlying `VkBuffer`/`VkImageView` for the same
  logical `BufferHandle`/`TextureHandle` across different frames (pooled/
  reused physical resources) — a compute pass's descriptor set, once
  allocated, is NOT automatically kept in sync with this; whoever wrote the
  descriptor set must re-issue `vkUpdateDescriptorSets()` whenever the
  underlying resource identity changes. This is a genuinely new problem
  `MaterialTexture` never had to solve, since a `MaterialTexture`'s
  `Texture2D` is immutable for its whole lifetime (see `Texture2D.h`'s own
  class comment: "never resized/rewritten after construction").

## Step 3: The Plan

- **New `src/Renderer/Vulkan/DescriptorSetLayoutBuilder.h/.cpp`** — a small,
  fluent helper class:
  ```
  DescriptorSetLayoutBuilder builder(device);
  builder.AddStorageBuffer(/*binding=*/0, VK_SHADER_STAGE_COMPUTE_BIT)
         .AddStorageBuffer(/*binding=*/1, VK_SHADER_STAGE_COMPUTE_BIT)
         .AddStorageImage(/*binding=*/2, VK_SHADER_STAGE_COMPUTE_BIT);
  VkDescriptorSetLayout layout = builder.Build();
  ```
  Internally just accumulates `VkDescriptorSetLayoutBinding` entries and
  calls `vkCreateDescriptorSetLayout()` once in `Build()` — genuinely
  small, no cleverness, purely to avoid every future compute-shader author
  hand-writing the same `VkDescriptorSetLayoutCreateInfo` boilerplate
  `GpuResourceFactory`'s constructor already writes once for the material
  set. Add `AddCombinedImageSampler(binding, stage)` too, for a `Texture`
  binding alongside storage resources in the same set (e.g. a compute
  shader sampling one input texture while writing an output image).
- **Document a binding-number convention** directly in this builder's own
  header comment, mirroring how `MaterialDescriptorSetLayout()`'s own
  single binding is documented: bindings are assigned in **declaration
  order**, matching the ORDER a shader's own GLSL bindings are numbered,
  read top-to-bottom as: (1) `StructuredBuffer`/read-only inputs first,
  (2) `RWStructuredBuffer`/read-write buffers next, (3) `Texture`/read-only
  sampled inputs next, (4) `RWTexture`/storage-image outputs last. This is
  a *project convention* (not enforced by any tool), chosen so a future
  reader can predict a shader's binding layout from its resource kind
  alone, without needing to open the `.comp` file. Every future compute
  shader's own file-level comment block should restate its concrete binding
  numbers explicitly (mirroring how `Shaders/TexturedMesh.vert/.frag`
  already documents its own descriptor set 0 / binding 0 convention today).
- **Extend `GpuResourceFactory`'s descriptor pool story**: add a SECOND,
  dedicated descriptor pool, `m_computeDescriptorPool`, sized for
  `VK_DESCRIPTOR_TYPE_STORAGE_BUFFER` and `VK_DESCRIPTOR_TYPE_STORAGE_IMAGE`
  (two `VkDescriptorPoolSize` entries, each generously sized — mirror
  `kMaxMaterialTextures`'s own "generous, never individually freed" sizing
  philosophy, e.g. `kMaxComputeStorageBuffers`/`kMaxComputeStorageImages`
  constants in `GpuResourceFactory.cpp`, sized for the number of DISTINCT
  compute-bound resources this engine is realistically expected to need in
  one process lifetime — a low hundreds, not thousands, since compute
  shaders are far less numerous per-frame than material textures). Never
  share this pool with `m_materialDescriptorPool` — different descriptor
  type requirements, and keeping them separate means a compute-heavy
  feature can never exhaust the material-texture pool's budget or vice
  versa.
- **New `src/Renderer/ComputeDescriptorSet.h`** (a small, explicit value
  type, NOT a RAII owner — descriptor sets from a shared pool are, per
  `MaterialTexture`'s own precedent, never individually freed, only the
  whole pool at once): wraps a single `VkDescriptorSet` plus a small
  `Rewrite(...)`-style method taking the current buffer/image
  view/sampler handles to bind, calling `vkUpdateDescriptorSets()` fresh
  each time it's invoked. A compute pass (Phase 6) calls `Rewrite()` once
  per frame, using whatever physical resource `RenderGraphResourcePool`
  handed back THIS frame for each declared handle — cheap (one
  `vkUpdateDescriptorSets()` call, no allocation) and always correct, at
  the cost of being slightly wasteful when the physical resource happens
  not to have changed frame-to-frame (acceptable: this mirrors the existing
  engine-wide "correctness over micro-optimization until proven necessary"
  discipline, e.g. `FrameGraphData.h`'s deliberately simple flat-list
  design).
- **`GpuResourceFactory::AllocateComputeDescriptorSet(VkDescriptorSetLayout
  layout)`** — allocates one `VkDescriptorSet` from `m_computeDescriptorPool`
  against a caller-supplied layout (built via `DescriptorSetLayoutBuilder`
  above), returned as a plain `VkDescriptorSet` for the caller to wrap in
  `ComputeDescriptorSet` and manage thereafter.

## Step 4: What We Will NOT Do

- No bindless descriptor indexing (`VK_EXT_descriptor_indexing`) — every
  compute shader in this campaign uses a small, fixed number of explicitly-
  bound resources per set, matching `MaterialTexture`'s existing one-
  descriptor-set-per-resource precedent, not an indexed array.
- No automatic re-validation that a shader's GLSL bindings match the C++
  `DescriptorSetLayoutBuilder` call — this is a documented human
  convention, enforced by code review, exactly as stated in Phase 1/this
  campaign's overall refusal of shader reflection.
- No descriptor-set caching/deduplication across different compute passes
  — each pass builds and owns its own layout/set, mirroring how each
  graphics `Pipeline` today builds its own (there is no shared "any two
  identical layouts get deduplicated" cache anywhere in this engine, and
  this phase does not introduce one).
- No support for a descriptor set spanning MULTIPLE frames-in-flight with
  independent double/triple-buffered copies — this engine's existing
  offscreen/pipelined regimes (`ExecuteTimingMode`) are handled by
  `RenderGraphResourcePool`'s own resource pooling underneath a single,
  re-written descriptor set per pass, not by this phase maintaining several
  descriptor set copies itself.

## Step 5: Their Role

- Build `DescriptorSetLayoutBuilder` as pure, isolated infrastructure first
  — it has no dependency on `ComputePipeline` (Phase 2) beyond needing a
  `VkDevice`, so it can be written and manually smoke-tested (Tier 2, per
  `AGENTS.md`) in parallel with Phase 2 if convenient.
- When wiring the throwaway `Passthrough.comp` validation shader from
  Phase 2, use THIS phase's `DescriptorSetLayoutBuilder`/
  `AllocateComputeDescriptorSet()` to build its descriptor set, rather than
  hand-writing one-off Vulkan calls — this doubles as this phase's own
  first real validation.
- Flag the "descriptor set must be re-written whenever `RenderGraphResourcePool`
  hands back a different physical resource" rule loudly to whoever builds
  Phase 6 — it is the single easiest correctness mistake to make in the
  whole campaign (a stale descriptor set silently pointing at a destroyed/
  repurposed buffer is exactly the kind of bug validation layers may not
  catch consistently, depending on the driver).
