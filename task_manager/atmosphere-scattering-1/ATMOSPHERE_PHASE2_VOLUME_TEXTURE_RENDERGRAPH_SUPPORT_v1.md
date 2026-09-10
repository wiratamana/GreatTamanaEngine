# ATMOSPHERE_PHASE2_VOLUME_TEXTURE_RENDERGRAPH_SUPPORT_v1.md

### Child document 2 of 9 — see `ATMOSPHERE_PHASE0_MASTER_STRATEGY_v1.md` for the full campaign map.
### Read `ATMOSPHERE_PHASE1_...COMPLETION_REPORT.md` first — this phase depends on Phase 1's `AtmosphereTypes.h` existing (for `AtmosphereFrameUniforms`' future use) but does NOT depend on `AtmosphereMath.h` at all.

**This is the single highest-risk, highest-context phase in the whole
campaign** — it extends a mature, already-shipped, load-bearing engine
subsystem (`gte::rg::RenderGraph`) with a genuinely new resource kind, the
same order of change as the GPU Vertex Skinning campaign's own Phase 3
(`GPU_SKINNING_PHASE3_RENDERGRAPH_SYNCHRONIZATION_STRATEGY_v2.md`, which added
buffer/`ImportBuffer()` support). Take the time to read the REAL current
`RenderGraphTypes.h`/`RenderGraphBuilder.h/.cpp`/`RenderGraphCompiler.cpp`/
`RenderGraphResourcePool.h/.cpp`/`RenderGraphBarrierPlanner.h/.cpp`/
`RenderGraph.h/.cpp` source directly before writing any code — this document
describes the SHAPE of the change against those files as they existed at the
time this document was written; always trust the live source over this
document's own paraphrase of it.

## Step 1: The Goal

Give the engine a real 3D-image GPU resource type (`VolumeTexture`, mirroring
`Texture2D`'s shape but `VK_IMAGE_TYPE_3D`), and teach `gte::rg::RenderGraph`
to treat it as a genuine THIRD resource kind alongside its existing
`Texture`/`Buffer` — declarable, importable, readable, writable, correctly
barrier-synchronized, and resolvable from inside a pass's `execute` callback
— with ZERO workload/atmosphere-specific logic yet. This phase proves the
plumbing with a small, disposable, self-contained validation pass (mirroring
`Passthrough.comp`/the Phase 1 include-probe discipline), exactly the same
way the original compute-shader campaign proved `ComputePipeline` before any
real workload used it.

## Step 2: The Situation

Confirmed directly against the real source (see file paths above):

- `RenderGraphTypes.h`'s `ResourceKind` enum has exactly two values,
  `Texture` and `Buffer`. `ResourceUsage` is a tagged struct carrying BOTH a
  `TextureHandle texture` and a `BufferHandle buffer` field plus a `kind` tag
  saying which one is meaningful, with two static factories,
  `ResourceUsage::ForTexture(...)`/`ForBuffer(...)`.
- `TextureDesc` carries only `width`/`height`/`format`/`hasDepth` — no
  concept of a third (depth/slice) dimension at all.
- `TextureHandle`/`BufferHandle`/`PassHandle` are all the same cheap
  `{index, generation}` POD shape (mirroring `gte::Entity`/
  `GpuResourceHandle`) — a new `VolumeTextureHandle` should copy this exact
  shape, as its own distinct struct type (never a shared template — see
  `RenderGraphTypes.h`'s own comment on why three separate handle structs
  exist instead of one generic template).
- `ResourceAccess` already has generic enumerators this new kind can reuse
  as-is with no new enumerator needed: `ComputeShaderRead`/
  `ComputeShaderWrite` (a compute shader reading/writing an `image3D`) and
  `ShaderRead` (a full-screen pass sampling it as `sampler3D`) — do not add a
  new `ResourceAccess` enumerator just because the resource kind is new; the
  access KIND (what a pass does with it) is orthogonal to the resource kind
  (what shape the resource is), and the existing three values already cover
  every access this campaign needs for a volume texture.
- `RenderGraphBuilder`'s real public API (confirm exact signatures directly
  from `RenderGraphBuilder.h`) has `CreateTexture(name, TextureDesc)`/
  `ImportTexture(name, ...)` and `PassBuilder::ReadTexture()`/
  `WriteTexture()`/`ReadBuffer()`/`WriteBuffer()` — a new
  `CreateVolumeTexture(name, VolumeTextureDesc)`/`ImportVolumeTexture(name,
  VolumeTexture&, initialAccessOrLayout)` plus `PassBuilder::
  ReadVolumeTexture()`/`WriteVolumeTexture()` need to be added following the
  EXACT same shape/naming convention.
- `RenderGraphResourcePool` is what Phase 4 of the original Render Graph
  campaign built to reuse physical textures/buffers across frames by
  `TextureDesc`/`BufferDesc` value-equality — confirm whether the aerial
  perspective volume actually NEEDS pooling at all. Unlike the Game/Scene
  View's `RenderTexture`s (which resize with the panel), this campaign's one
  and only volume texture (Phase 6) is a FIXED, small, known-at-startup size
  (e.g. 32×32×32) that never changes — it may be simplest and lowest-risk to
  treat it as an IMPORTED, persistently-owned resource (like
  `ComputeBlurValidation`'s own `blurredOutput`), never a
  `CreateVolumeTexture()`-requested transient one. If so, `RenderGraphResourcePool`
  may need ZERO changes, and only `ImportVolumeTexture()` needs to exist —
  confirm this reasoning explicitly in this phase's own completion report
  rather than assuming it; if a future phase's design genuinely needs a
  transient, graph-owned volume texture, extending the pool is a
  well-scoped follow-up, not something to speculatively build now (see Step
  4).
- `RenderGraphCompiler`'s dependency-resolution/culling walk operates
  generically over `ResourceUsage` values by `kind` — confirm it does NOT
  have any `Texture`/`Buffer`-only exhaustive `switch` (without a
  `VolumeTexture` case, an exhaustive switch would FAIL TO COMPILE the
  moment the new enumerator is added, which is actually the safety net that
  catches every place needing an update — treat any such compile error as
  the compiler telling you exactly where to add the new case, not as a
  problem to work around).
- `RenderGraphBarrierPlanner::RequiredStateFor(access, isDepthResource)`
  (or its real equivalent signature) computes a `VkImageLayout`/access-mask/
  stage-mask triple from a `ResourceAccess` value — confirm whether it is
  keyed purely on `ResourceAccess` (in which case a volume texture reusing
  `ComputeShaderRead`/`ComputeShaderWrite`/`ShaderRead` needs literally no
  new logic here) or whether it also branches on `ResourceKind`/image
  dimensionality anywhere (in which case that branch needs a new case for
  `VolumeTexture`, producing `VK_IMAGE_LAYOUT_GENERAL`/
  `VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL` exactly as it would for a 2D
  image — a 3D image's layout enum values are the SAME Vulkan enum, so this
  should be a small, mechanical addition either way).
- `PassContext` (used inside a pass's `execute` callback, e.g.
  `resolveTexture(TextureHandle)`/`resolveBuffer(BufferHandle)`) needs a new
  `resolveVolumeTexture(VolumeTextureHandle) -> VolumeTexture&`-shaped method,
  confirmed against its real current definition (it is only forward-declared
  in `RenderGraphTypes.h`; its full shape lives in `RenderGraph.h`/`.cpp` —
  read that directly).
- `RenderGraphDebugTextureRegistry` (`network-impl-4` campaign) is 2D-texture
  specific by design (`DebugTextureSnapshot`) — this phase does **NOT**
  extend it to understand volume textures at all (see Phase 9's own scope
  note for how volume-texture debug visibility is instead achieved via a
  small separate 2D "debug slice" mirror, later, once there is an actual
  volume texture with real content worth visualizing).

## Step 3: The Plan

### 3.1 — `src/Renderer/VolumeTexture.h/.cpp`

New RAII class, directly modeled on `src/Renderer/Texture2D.h/.cpp`'s exact
shape (read that file in full first — every design note in this section is a
deliberate, named DIFFERENCE from it):

- Constructor: `VolumeTexture(VmaAllocator, std::shared_ptr<GpuMemoryTracker>,
  VkDevice, int width, int height, int depth, VkFormat format, const char*
  debugName = nullptr)`. Unlike `Texture2D` (fixed
  `VK_FORMAT_R8G8B8A8_UNORM`), this class takes an explicit `format` — the
  aerial-perspective volume needs a higher-precision floating-point format
  (e.g. `VK_FORMAT_R16G16B16A16_SFLOAT`, confirmed against what Phase 1's
  reference notes say `pl-sky` itself uses) to store scattering values that
  can exceed `[0, 1]` and need float precision, unlike an 8-bit color
  texture.
- Image creation: `VkImageCreateInfo` with `imageType = VK_IMAGE_TYPE_3D`,
  `extent = {width, height, depth}`, and **always** both
  `VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_STORAGE_BIT` (unlike
  `Texture2D`'s optional `allowStorageImageAccess` flag — a volume texture in
  this campaign is ALWAYS written by a compute pass and ALWAYS later sampled
  by a graphics/compute pass, so there is no "sampled-only" variant to
  support; do not add an unused optional flag). Before creating it, confirm
  (via `Vulkan/FormatCapabilities.h`, the same helper `Texture2D`'s own
  caller already uses to check `VK_FORMAT_FEATURE_STORAGE_IMAGE_BIT`) that
  the chosen format actually supports `VK_FORMAT_FEATURE_STORAGE_IMAGE_BIT`
  on this device/driver — throw loudly (matching this codebase's existing
  "fail loud, never silently degrade" convention for GPU capability gaps) if
  not, rather than silently falling back to a different format.
- Image view: `VkImageViewType = VK_IMAGE_VIEW_TYPE_3D`.
- Sampler: `VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE` in all three axes (U/V/W)
  — a froxel volume must never wrap; clamping at the near/far/edge froxels is
  the physically correct behavior (mirrors `Texture2D`'s own sampler
  creation code closely, just extended to the third axis), `VK_FILTER_LINEAR`
  min/mag/mip (trilinear sampling across froxels is exactly what makes the
  aerial-perspective composite look smooth instead of blocky — this is the
  whole reason a genuine 3D image is worth the engineering cost here instead
  of, say, a 2D texture array).
- `GpuMemoryTracker::Track()`/`Untrack()` exactly like `Texture2D` — same
  handle-based registration, same Editor "Memory" panel visibility for free,
  same rule about tracking the REAL size VMA reports rather than the
  requested size (see `AGENTS.md`, "GPU Resource Memory Tracking").
  `ClassifyGpuMemoryLocation()`/the existing `GpuResourceKind` enum should
  already be generic enough to cover this (confirm — it almost certainly
  just needs `GpuResourceKind::Texture` reused, since nothing about that enum
  is 2D-vs-3D-specific).
- `Width()`/`Height()`/`Depth()`/`Image()`/`View()`/`Sampler()`/`Handle()`
  accessors, move-only, RAII destructor — same shape as `Texture2D`.
- `Renderer::CreateVolumeTexture(int width, int height, int depth, VkFormat
  format, const char* debugName = nullptr)` and the matching
  `GpuResourceFactory::CreateVolumeTexture(...)` — same thin-passthrough
  convention every other `Renderer::CreateX()` factory method already
  follows.

### 3.2 — `RenderGraphTypes.h` extension

- Add `VolumeTextureHandle` (same `{index, generation}` shape as
  `TextureHandle`).
- Add `ResourceKind::VolumeTexture` to the existing enum.
- Add a `VolumeTextureHandle volumeTexture` field to `ResourceUsage`, plus a
  third static factory, `ResourceUsage::ForVolumeTexture(VolumeTextureHandle,
  ResourceAccess)`.
- Add `VolumeTextureDesc { std::uint32_t width, height, depth; VkFormat
  format; }` with `operator==` (mirroring `TextureDesc`/`BufferDesc`'s own
  value-equality convention exactly — see `RenderGraphTypes.h`'s own header
  comment on why `debugName` must never be a field here).
- Fix every exhaustive `switch(ResourceKind)`/`switch` over resource kind
  this change breaks compilation of — treat each resulting compile error as
  the required checklist (see Step 2's own note on `IsWriteAccess()`/
  `RenderGraphCompiler`'s dependency walk — there may be more than one; find
  them all via `search_in_dir` for `ResourceKind::Buffer` and
  `ResourceKind::Texture` across `src/Renderer/RenderGraph/` before
  considering this step done).
- Add or extend a Tier-1 test in `tests/Renderer/RenderGraph/
  RenderGraphTypesTests.cpp` covering the new handle/desc/usage-factory shape
  (mirrors whatever the existing `Buffer`-side tests already check for
  `BufferHandle`/`BufferDesc`/`ResourceUsage::ForBuffer`).

### 3.3 — `RenderGraphBuilder` extension

- `PassBuilder::ReadVolumeTexture(VolumeTextureHandle, ResourceAccess)`/
  `WriteVolumeTexture(VolumeTextureHandle, ResourceAccess)` — mirrors
  `ReadBuffer()`/`WriteBuffer()`'s exact shape.
- `RenderGraphBuilder::ImportVolumeTexture(const char* name, VolumeTexture&
  volumeTexture, VkImageLayout currentLayout)` — mirrors `ImportTexture()`'s
  signature/behavior (registers an externally-owned resource under a stable
  name, tracks its current layout for barrier planning going forward). Per
  Step 2's own analysis, decide (and document the decision explicitly in the
  completion report) whether `CreateVolumeTexture(name, VolumeTextureDesc)`
  (a graph-POOLED, transient resource) is actually needed by ANY real
  consumer in this campaign — if Phase 6's plan (read that document too
  before finalizing this decision) only ever needs one persistent, imported
  volume texture, implement `ImportVolumeTexture()` only, and leave
  `CreateVolumeTexture()` unimplemented/not-yet-needed (note it as a
  deliberately deferred, not-yet-justified addition, the same "don't
  speculatively build it" discipline `AGENTS.md` already applies elsewhere).

### 3.4 — `RenderGraphResourcePool`/`RenderGraphCompiler`/`RenderGraphBarrierPlanner`/`RenderGraph`/`PassContext`

- If 3.3 concluded `CreateVolumeTexture()` (pooled/transient) is not needed
  yet, `RenderGraphResourcePool` needs NO changes at all in this phase — say
  so explicitly in the completion report rather than leaving it ambiguous.
- `RenderGraphCompiler`'s dependency/culling walk: confirm (per Step 2) it
  is generic over `ResourceUsage`/`kind` already; fix any exhaustive switches
  the compiler flags.
- `RenderGraphBarrierPlanner::RequiredStateFor()` (or its real name/
  signature): confirm it is keyed purely on `ResourceAccess`, not on
  `ResourceKind`/image dimensionality; if it happens to branch on
  `isDepthResource`/similar per-resource-kind flags, add whatever minimal
  case is needed for a volume texture (which is never a depth resource,
  simplifying this).
- `RenderGraph`'s internal per-frame bookkeeping (whatever tracks "this
  resource's current `VkImageLayout`/access for barrier purposes between
  passes" — read the exact mechanism from `RenderGraph.cpp`, likely a
  parallel table keyed by handle) needs a THIRD such table for
  `VolumeTextureHandle`, mirroring however it already does this for
  `TextureHandle`/`BufferHandle` separately.
- `PassContext::resolveVolumeTexture(VolumeTextureHandle) -> VolumeTexture&`
  — mirrors `resolveTexture()`/`resolveBuffer()`'s exact resolution logic
  (look up the imported resource by handle; throw/assert on an unresolved
  handle exactly like the existing two do).

### 3.5 — Disposable end-to-end validation (mirrors `Passthrough.comp`)

- A tiny, throwaway compute shader, `src/Shaders/_VolumeProbe.comp` — binds a
  small (e.g. 4×4×4) `image3D` (`layout(binding = 0, rgba16f) uniform
  writeonly image3D destinationVolume;`) and writes a simple, position-derived
  test pattern into every voxel via `imageStore()`.
- A throwaway C++ call site (NOT a permanent class — a temporary block of
  code in, e.g., `Application.cpp`'s own render-graph-building block, clearly
  commented `// TEMPORARY - ATMOSPHERE_PHASE2 VALIDATION - DELETE BEFORE
  MERGE`) that creates a real `VolumeTexture`, imports it via
  `ImportVolumeTexture()`, declares a pass writing it via
  `WriteVolumeTexture(..., ResourceAccess::ComputeShaderWrite)`, dispatches
  the probe shader, and reads a couple of known voxels back (e.g. via a
  one-off `vkCmdCopyImageToBuffer` + a mapped staging buffer, OR simply
  confirm via RenderDoc/Vulkan validation layers that the dispatch/barriers
  are clean with zero validation errors — either is acceptable proof).
- Confirm, with validation layers enabled, that: the pass survives
  `RenderGraphCompiler`'s culling (it must be reachable from a root — a
  throwaway "read it back" pass, or `builder.AddRootOutput()`/equivalent, is
  needed purely to keep it alive for this test), the barrier planner emits a
  correct transition before the compute shader writes it, and no validation
  error/warning appears anywhere in the sequence.
- **Delete every one of `_VolumeProbe.comp` and its throwaway call site
  before this phase's completion report is written** — same non-negotiable
  cleanup discipline as `Passthrough.comp`/Phase 1's `_IncludeProbe*` files.
  Leaving disposable validation code in the shipped tree is exactly the kind
  of debt `AGENTS.md`/`RENDERGRAPH_FUTURE_TODO_DELIBERATELY_NOT_IMPLEMENTED.md`
  already warn against.

## Step 4: What We Will NOT Do

- No mip-mapping for `VolumeTexture` — a single mip level only, exactly what
  this campaign's froxel volume needs.
- No generic "N-dimensional resource" abstraction — `VolumeTexture` is its
  own concrete class, `VolumeTextureHandle`/`VolumeTextureDesc` are their own
  concrete types, mirroring this codebase's consistent preference for
  explicit, named types over generic/templated ones (see
  `RenderGraphTypes.h`'s own comment on why three separate handle structs
  exist instead of one template).
- No debug-texture-registry (`GET /get_texture`) support for volume textures
  in this phase — explicitly deferred to Phase 9 (a separate, small,
  2D-mirror mechanism), not attempted here.
- No `RenderGraphResourcePool` pooling support unless 3.3/3.4's own analysis
  concludes a real consumer needs it (see those sections) — do not build
  speculative infrastructure "just in case" ahead of Phase 6 actually needing
  it.
- No changes to `RenderGraphSnapshot.h/.cpp`/the Editor's "Render Graph"
  panel in this phase — that panel showing a volume-texture resource
  correctly (vs. just not crashing on one) is a nice-to-have, not required
  for this campaign; note it as an acceptable, documented gap if the panel's
  resource-table rendering happens to choke on the new `ResourceKind` value,
  and fix only what's needed to avoid an actual crash/incorrect display.

## Step 5: Their Role

- This phase's completion report MUST explicitly answer, in writing: (a)
  whether `RenderGraphCompiler`'s dependency walk needed any changes at all
  and why/why not, (b) whether `RenderGraphBarrierPlanner` needed any changes
  beyond "it just worked because it's keyed on `ResourceAccess` alone," and
  (c) the final decision on whether `CreateVolumeTexture()` (pooled) was
  implemented or deliberately deferred. Phase 6 depends on knowing these
  answers precisely before it can be implemented correctly.
- If the disposable validation in 3.5 reveals the barrier planner does NOT
  correctly synchronize a volume-texture write (e.g. a validation-layer
  warning appears), **do not proceed to Phase 3-9 until this is fixed** —
  every later phase in this campaign is only safe to build once this
  foundation is proven correct, exactly the same non-negotiable prerequisite
  the GPU Vertex Skinning campaign's own Phase 3 WAW-hazard fix represented
  for ITS later phases.
