# ATMOSPHERE_PHASE2_VOLUME_TEXTURE_RENDERGRAPH_SUPPORT_v1.md

### Child document 2 of 9 — see `ATMOSPHERE_PHASE0_MASTER_STRATEGY_v1.md` for the full campaign map.
### Read `ATMOSPHERE_PHASE1_...COMPLETION_REPORT.md` first — this phase depends on Phase 1's `AtmosphereTypes.h` existing (for `AtmosphereFrameUniforms`' future use) but does NOT depend on `AtmosphereMath.h` at all.

> **Pre-implementation technical precheck applied (2026-09-10):** every
> concrete claim in this document was cross-checked directly against the real
> source (`RenderGraphTypes.h/.cpp`, `RenderGraphBuilder.h/.cpp`,
> `RenderGraphCompiler.h/.cpp`, `RenderGraphResourcePool.h/.cpp`,
> `RenderGraphBarrierPlanner.h/.cpp`, `RenderGraph.h/.cpp`,
> `RenderGraphSnapshot.cpp`, `Texture2D.h/.cpp`, `RenderTarget.h`,
> `GpuResourceFactory.cpp`, `GpuMemoryTracker.h`,
> `Vulkan/FormatCapabilities.h`, `ComputeBlurValidation.h/.cpp`,
> `tests/Renderer/RenderGraph/RenderGraphTypesTests.cpp`). One assumption in
> the original Step 2 write-up was **factually wrong and has been corrected
> in place below (search for "CORRECTED")**: this document originally
> assumed `RenderGraphCompiler`/`RenderGraph`'s `kind`-branching code was a
> `Texture`/`Buffer`-only *exhaustive switch* that would fail to compile the
> moment `ResourceKind::VolumeTexture` was added, acting as a free safety
> net. The real code is a plain two-way `if (kind == ResourceKind::Texture)
> {...} else {...}` in three files, which will NOT fail to compile, and
> silently misroutes a volume-texture usage into the Buffer path — a real,
> reachable out-of-bounds vector access (`RenderGraph::EnsureBufferResolved()`
> has no bounds check), not just a wrong dependency edge. Everything else in
> this document was confirmed accurate against the real source and is left
> unchanged except for two smaller naming/signature corrections (also marked
> "CORRECTED") and one clarification each on where the format-capability
> check belongs and how `ImportVolumeTexture()` should be shaped. See
> `ATMOSPHERE_PHASE2_PRECHECK_REPORT.md` for the full write-up of this pass.

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
  `ResourceUsage::ForTexture(...)`/`ForBuffer(...)`. **CONFIRMED** exactly as
  written, field-for-field, against the real `RenderGraphTypes.h`.
- `TextureDesc` carries only `width`/`height`/`format`/`hasDepth` — no
  concept of a third (depth/slice) dimension at all. **CONFIRMED.**
- `TextureHandle`/`BufferHandle`/`PassHandle` are all the same cheap
  `{index, generation}` POD shape (mirroring `gte::Entity`/
  `GpuResourceHandle`) — a new `VolumeTextureHandle` should copy this exact
  shape, as its own distinct struct type (never a shared template — see
  `RenderGraphTypes.h`'s own comment on why three separate handle structs
  exist instead of one generic template; it will become FOUR distinct
  handle structs after this phase, same reasoning). **CONFIRMED.**
- `ResourceAccess` already has generic enumerators this new kind can reuse
  as-is with no new enumerator needed: `ComputeShaderRead`/
  `ComputeShaderWrite` (a compute shader reading/writing an `image3D`) and
  `ShaderRead` (a full-screen pass sampling it as `sampler3D`) — do not add a
  new `ResourceAccess` enumerator just because the resource kind is new; the
  access KIND (what a pass does with it) is orthogonal to the resource kind
  (what shape the resource is), and the existing three values already cover
  every access this campaign needs for a volume texture. **CONFIRMED** —
  `RenderGraphTypes.cpp`'s `IsWriteAccess()`/`ToString()` and
  `RenderGraphBarrierPlanner.cpp`'s `RequiredStateFor()` are genuine,
  deliberately-`default`-less EXHAUSTIVE switches over `ResourceAccess`
  alone, with no `ResourceKind`/dimensionality branch anywhere in any of the
  three — reusing `ComputeShaderRead`/`ComputeShaderWrite`/`ShaderRead`
  genuinely requires zero new `ResourceAccess`-side logic.
- `RenderGraphBuilder`'s real public API (confirm exact signatures directly
  from `RenderGraphBuilder.h`) has `CreateTexture(name, TextureDesc)`/
  `ImportTexture(name, ...)` and `PassBuilder::ReadTexture()`/
  `WriteTexture()`/`ReadBuffer()`/`WriteBuffer()` — a new
  `CreateVolumeTexture(name, VolumeTextureDesc)`/`ImportVolumeTexture(name,
  VolumeTexture&, initialAccessOrLayout)` plus `PassBuilder::
  ReadVolumeTexture()`/`WriteVolumeTexture()` need to be added following the
  EXACT same shape/naming convention. **CONFIRMED, with one signature
  correction:** `ImportTexture()`'s REAL signature is
  `ImportTexture(const char* name, const RenderTarget& externalTarget,
  VkImageLayout currentLayout)` — it takes a plain, non-owning `RenderTarget`
  struct (`RenderTarget.h`: just `VkImage`/`VkImageView`/`VkExtent2D`/
  `VkFormat` Vulkan handles, no ownership), never the owning
  `RenderTexture`/`Texture2D` object itself. `RenderGraphBuilder.h`
  deliberately `#include`s only `RenderTarget.h`, never `RenderTexture.h` —
  this is a real, load-bearing layering choice, not an accident.
  `ComputeBlurValidation::AddPass()` proves this convention in production
  code today: `builder.ImportTexture("BlurredSceneOutput",
  m_blurredOutput->Target(), VK_IMAGE_LAYOUT_UNDEFINED)` passes
  `m_blurredOutput->Target()` (a `RenderTarget`), never `*m_blurredOutput`
  itself. **`ImportVolumeTexture()` must follow this SAME convention, not
  take a `VolumeTexture&` directly** — see the corrected 3.3 below.
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
  4). **CONFIRMED against the real `RenderGraphResourcePool.h/.cpp`:** its
  `AcquireTexture()`/`AcquireBuffer()`/`BeginFrame()` shape is exactly as
  described, and an imported resource genuinely never touches this pool at
  all (see `RenderGraphBuilder.h`'s own `TextureImportInfo`/`BufferImportInfo`
  comment) — the "zero changes needed if import-only" reasoning holds.
- `RenderGraphCompiler`'s dependency-resolution/culling walk operates
  generically over `ResourceUsage` values by `kind` — confirm it does NOT
  have any `Texture`/`Buffer`-only exhaustive `switch` (without a
  `VolumeTexture` case, an exhaustive switch would FAIL TO COMPILE the
  moment the new enumerator is added, which is actually the safety net that
  catches every place needing an update — treat any such compile error as
  the compiler telling you exactly where to add the new case, not as a
  problem to work around).

  **CORRECTED — this assumption is FACTUALLY WRONG, confirmed by reading the
  real `RenderGraphCompiler.cpp`/`RenderGraph.cpp`/`RenderGraphSnapshot.cpp`
  source directly.** There is NO exhaustive `switch(ResourceKind)` anywhere
  in this codebase today. Every place that branches on `ResourceUsage::kind`
  is a plain, TWO-WAY `if (usage.kind == ResourceKind::Texture) { ... }
  else { ... /* treated as Buffer, unconditionally */ ... }` — a binary
  branch, not a `switch`, and it will compile perfectly cleanly the moment a
  third `ResourceKind::VolumeTexture` value exists, silently routing every
  volume-texture usage down the Buffer path instead. Confirmed exact
  locations (via `search_in_dir` for `ResourceKind::Texture` across
  `src/Renderer/RenderGraph/` — re-run this search yourself before trusting
  this list, since the source may have moved on since this audit):
  - `RenderGraphCompiler.cpp`, FOUR separate `if/else` sites in one
    function: the RAW-edge writer lookup (`usage.kind == ResourceKind::Texture
    ? lastTextureWriter[...] : lastBufferWriter[...]`), the WAW-edge/
    last-writer update (same shape, for `pass.writes`), the backward-
    reachability root-marking scan (`usage.kind == ResourceKind::Texture &&
    ContainsTextureHandle(finalOutputs, usage.texture)` — note a
    `VolumeTextureHandle` can therefore never be marked as a root here
    either, matching the existing rule that a `BufferHandle` can't be one
    now), and the resource-lifetime `touch()` lambda (which picks
    `result.textureLifetimes` vs. `result.bufferLifetimes` and
    `usage.texture.index` vs. `usage.buffer.index` via the SAME ternary).
  - `RenderGraph.cpp`: `ApplyUsageBarrierIfNeeded()`'s top-level
    `if (usage.kind == ResourceKind::Texture) {...} else { EnsureBufferResolved(usage.buffer.index, ...); ... }`,
    and `ExecuteCompiledGraph()`'s color/depth-attachment-write scan
    (`if (usage.kind != ResourceKind::Texture) { continue; }`).
  - `RenderGraphSnapshot.cpp`'s `ResourceUsageName()` (Editor "Render Graph"
    panel display only — lower risk, bounds-checked, degrades to an empty
    string rather than crashing; still an `if/else`, still silently wrong
    for a volume-texture usage, and still explicitly accepted as an
    out-of-scope gap for THIS phase per Step 4 below).

  **Why this is a real correctness bug, not just a cosmetic one:** per this
  phase's own plan (3.2), `ResourceUsage::ForVolumeTexture(handle, access)`
  will leave `ResourceUsage::buffer` at its default-constructed
  `BufferHandle{}` (`index == kInvalidIndex == 0xFFFFFFFF`). If any of the
  `else` branches above are left unfixed, a volume-texture usage silently
  falls into the Buffer path with that huge, invalid index. In
  `RenderGraphCompiler.cpp` this happens to be harmless (every lookup there
  is bounds-checked, e.g. `if (usage.buffer.index < lastBufferWriter.size())`)
  — the practical effect is simply that a volume-texture read/write gets NO
  dependency edge and NO lifetime tracking at all, and can never be reached
  as a root, so a volume-texture-only pass is silently, permanently CULLED.
  But in `RenderGraph.cpp`, `EnsureBufferResolved(std::uint32_t index, ...)`
  indexes `physicalBuffers[index]` directly, with **NO bounds check
  whatsoever** — this is a genuine, reachable out-of-bounds `std::vector`
  access (undefined behavior, not a controlled failure) the instant a real
  pass declares a volume-texture read/write and this file hasn't been fixed
  first. **Every one of the sites listed above MUST be changed to a real
  three-way branch — either an explicit `if/else if/else`, or (preferred,
  since it gives every FUTURE fourth resource kind the same protection this
  one never had) an exhaustive `switch (usage.kind)` with deliberately no
  `default:` case, mirroring `IsWriteAccess()`'s own convention exactly —
  BEFORE `ResourceKind::VolumeTexture` is added to the enum, never after.**
  Do this audit-and-fix pass FIRST, confirm (e.g. by temporarily reverting
  the `else`-collapsing and checking every one) that no site was missed,
  THEN add the enumerator and the new `VolumeTexture`-side branches for
  real.
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
  should be a small, mechanical addition either way). **CONFIRMED — this one
  IS accurate, and stands in useful contrast to the corrected finding
  above:** `RequiredStateFor()`'s real body (`RenderGraphBarrierPlanner.cpp`)
  is a genuine, deliberately-`default`-less exhaustive `switch (access)`
  with zero `ResourceKind`/`isDepthResource`-driven branch for
  `ComputeShaderRead`/`ComputeShaderWrite`/`ShaderRead` at all (it returns
  the identical `ResourceState` — `VK_IMAGE_LAYOUT_GENERAL` +
  `VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT` + the matching read/write access
  mask — regardless of whether the caller is a texture or a buffer). A
  volume texture reusing these three `ResourceAccess` values genuinely
  needs ZERO new logic in this file.
- `PassContext` (used inside a pass's `execute` callback, e.g.
  `resolveTexture(TextureHandle)`/`resolveBuffer(BufferHandle)`) needs a new
  `resolveVolumeTexture(VolumeTextureHandle) -> VolumeTexture&`-shaped method,
  confirmed against its real current definition (it is only forward-declared
  in `RenderGraphTypes.h`; its full shape lives in `RenderGraph.h`/`.cpp` —
  read that directly). **CONFIRMED** — `PassContext` (fully defined in
  `RenderGraph.h`) carries `resolveReadTexture`/`resolveTexture` (both
  `std::function<ResolvedTexture(TextureHandle)>`) and `resolveBuffer`
  (`std::function<VkBuffer(BufferHandle)>`) as plain `std::function` fields,
  wired up fresh inside `RenderGraph::ExecuteCompiledGraph()`'s per-pass
  loop — a `resolveVolumeTexture` field follows the exact same shape.
- `RenderGraphDebugTextureRegistry` (`network-impl-4` campaign) is 2D-texture
  specific by design (`DebugTextureSnapshot`) — this phase does **NOT**
  extend it to understand volume textures at all (see Phase 9's own scope
  note for how volume-texture debug visibility is instead achieved via a
  small separate 2D "debug slice" mirror, later, once there is an actual
  volume texture with real content worth visualizing). **CONFIRMED** —
  `DebugTextureSnapshot` (`RenderGraphDebugTextureRegistry.h`) is built
  directly around a `RenderTarget` (2D-only) field with no 3D concept
  anywhere in the class.

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

  **CLARIFICATION (confirmed against the real source, this is not optional
  wording): the check above belongs in the FACTORY layer, never inside
  `VolumeTexture`'s own constructor.** `Texture2D`'s constructor does NOT
  call `SupportsStorageImageUsage()` itself — its own `.cpp` file says so
  directly ("this constructor unconditionally trusts that check already
  happened"); the actual call site is `GpuResourceFactory::CreateTexture2D()`
  (and, identically, `GpuResourceFactory::CreateRenderTexture()` for
  `RenderTexture`), which throws BEFORE ever constructing the RAII object.
  `VolumeTexture`'s constructor must follow this SAME division of labor —
  put the `SupportsStorageImageUsage()` check (and its thrown
  `std::runtime_error` on failure) in `GpuResourceFactory::CreateVolumeTexture()`
  / `Renderer::CreateVolumeTexture()`, not inside `VolumeTexture::VolumeTexture()`
  itself, even though this class always needs storage access unconditionally
  (unlike `Texture2D`/`RenderTexture`'s optional flag) — staying consistent
  with the existing "constructor trusts, factory checks" convention is more
  valuable here than the minor convenience of special-casing this one class.
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
  `ClassifyGpuMemoryLocation()`/the existing `GpuResourceType` enum should
  already be generic enough to cover this (confirm — it almost certainly
  just needs `GpuResourceType::Texture` reused, since nothing about that enum
  is 2D-vs-3D-specific). **CORRECTED name:** the real enum
  (`src/Renderer/Memory/GpuMemoryTracker.h`) is `GpuResourceType`
  (`Buffer`/`Texture`), not `GpuResourceKind` as an earlier draft of this
  document called it — confirmed it has exactly two values and is indeed
  format/dimensionality-agnostic, so `GpuResourceType::Texture` is the
  correct value to reuse for a `VolumeTexture`'s own `Track()` call.
- `Width()`/`Height()`/`Depth()`/`Image()`/`View()`/`Sampler()`/`Handle()`
  accessors, move-only, RAII destructor — same shape as `Texture2D`.
- `Renderer::CreateVolumeTexture(int width, int height, int depth, VkFormat
  format, const char* debugName = nullptr)` and the matching
  `GpuResourceFactory::CreateVolumeTexture(...)` — same thin-passthrough
  convention every other `Renderer::CreateX()` factory method already
  follows (and see the format-capability-check clarification above for
  exactly what belongs in this factory method specifically).

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
- **Do this BEFORE adding `ResourceKind::VolumeTexture` above, not after —
  see Step 2's corrected finding.** Find and fix EVERY `if (usage.kind ==
  ResourceKind::Texture) {...} else {...}` (or equivalent ternary) site in
  `RenderGraphCompiler.cpp`, `RenderGraph.cpp`, and (lower priority, Editor-
  display only) `RenderGraphSnapshot.cpp` — confirmed, current locations as
  of this audit: `RenderGraphCompiler.cpp`'s RAW-edge lookup, WAW-edge/
  last-writer update, backward-reachability root-marking scan, and
  resource-lifetime `touch()` lambda (four sites); `RenderGraph.cpp`'s
  `ApplyUsageBarrierIfNeeded()` and its color/depth-write scan (two sites);
  `RenderGraphSnapshot.cpp`'s `ResourceUsageName()` (one site). Re-run
  `search_in_dir` for `ResourceKind::Texture`/`ResourceKind::Buffer` across
  `src/Renderer/RenderGraph/` yourself before trusting this list is
  complete, since the source may have shifted since this audit. Convert
  each to a real three-way branch (an exhaustive `switch (usage.kind)` with
  deliberately no `default:`, mirroring `IsWriteAccess()`'s convention, is
  the preferred fix — it gives every future fifth/sixth resource kind the
  same compile-time safety net this document originally, incorrectly,
  assumed already existed here). Only once this is done and re-verified
  should `ResourceKind::VolumeTexture` actually be added and its own
  `Texture`/`Buffer`-sibling branches filled in.
- Add or extend a Tier-1 test in `tests/Renderer/RenderGraph/
  RenderGraphTypesTests.cpp` covering the new handle/desc/usage-factory shape
  (mirrors whatever the existing `Buffer`-side tests already check for
  `BufferHandle`/`BufferDesc`/`ResourceUsage::ForBuffer` — confirmed, the
  real test file already has a directly-mirrorable pattern, e.g.
  `RenderGraphHandleTest.ExplicitlyConstructedBufferHandleIsValid`/
  `RenderGraphDescTest.IdenticalBufferDescsCompareEqual`/
  `RenderGraphResourceUsageTest.ForBufferSetsKindAndBufferFields`).

### 3.3 — `RenderGraphBuilder` extension

- `PassBuilder::ReadVolumeTexture(VolumeTextureHandle, ResourceAccess)`/
  `WriteVolumeTexture(VolumeTextureHandle, ResourceAccess)` — mirrors
  `ReadBuffer()`/`WriteBuffer()`'s exact shape.
- `RenderGraphBuilder::ImportVolumeTexture(const char* name, const
  VolumeTarget& externalVolumeTarget, VkImageLayout currentLayout)` —
  **CORRECTED signature, see Step 2's finding above:** the real
  `ImportTexture()` this is meant to mirror takes a plain, non-owning
  `const RenderTarget&`, never a `RenderTexture&`/`Texture2D&` directly, so
  `RenderGraphBuilder.h` stays decoupled from the owning resource class's
  own (heavier) header — it `#include`s `RenderTarget.h` only, never
  `RenderTexture.h`. Introduce a small, plain, non-owning `VolumeTarget`
  struct mirroring `RenderTarget`'s own shape exactly (e.g. `VkImage image;
  VkImageView imageView; VkExtent3D extent; VkFormat format;` — no
  ownership, no depth-companion fields, since a volume texture has no depth
  counterpart the way a color `RenderTarget` does), plus a
  `VolumeTexture::Target() -> VolumeTarget` accessor (mirroring
  `RenderTexture::Target()`), and pass THAT into `ImportVolumeTexture()` —
  never the `VolumeTexture&` object itself. This keeps
  `RenderGraphBuilder.h` from ever needing to `#include "../VolumeTexture.h"`
  at all, exactly preserving the existing layering discipline
  `RenderTarget.h` already established for `ImportTexture()`. Otherwise,
  this call registers an externally-owned resource under a stable name and
  tracks its current layout for barrier planning going forward, exactly
  like `ImportTexture()` does. Per Step 2's own analysis, decide (and
  document the decision explicitly in the completion report) whether
  `CreateVolumeTexture(name, VolumeTextureDesc)` (a graph-POOLED, transient
  resource) is actually needed by ANY real consumer in this campaign — if
  Phase 6's plan (read that document too before finalizing this decision)
  only ever needs one persistent, imported volume texture, implement
  `ImportVolumeTexture()` only, and leave `CreateVolumeTexture()`
  unimplemented/not-yet-needed (note it as a deliberately deferred,
  not-yet-justified addition, the same "don't speculatively build it"
  discipline `AGENTS.md` already applies elsewhere).

### 3.4 — `RenderGraphResourcePool`/`RenderGraphCompiler`/`RenderGraphBarrierPlanner`/`RenderGraph`/`PassContext`

- If 3.3 concluded `CreateVolumeTexture()` (pooled/transient) is not needed
  yet, `RenderGraphResourcePool` needs NO changes at all in this phase — say
  so explicitly in the completion report rather than leaving it ambiguous.
  **Confirmed low-risk**: the real `RenderGraphResourcePool` never touches
  an imported resource at all (see `TextureImportInfo`/`BufferImportInfo`'s
  own doc comments in `RenderGraphBuilder.h`), so an import-only
  `VolumeTexture` genuinely cannot interact with this class either way.
- `RenderGraphCompiler`'s dependency/culling walk: **it is NOT already
  generic over `ResourceUsage`/`kind` — see Step 2's corrected finding.**
  This is a mandatory, hands-on code change (four distinct `if/else` sites
  in this one file, listed in 3.2 above), not something a compile error will
  flag for you. Also note: `Compile(CompiledGraphInput&, std::span<const
  TextureHandle> finalOutputs)`'s root-set parameter is, and must remain,
  TEXTURE-ONLY — a `BufferHandle` can never be a `finalOutputs` root today,
  and a `VolumeTextureHandle` must follow that exact same rule (consistent
  with Phase 6's own plan, which only ever reads the volume texture from a
  later, texture-producing pass — it is never itself a call's own root
  output). Do not add volume-texture support to `finalOutputs` in this
  phase or any later one without a fresh, explicit design discussion.
- `RenderGraphBarrierPlanner::RequiredStateFor()` (or its real name/
  signature): confirmed keyed purely on `ResourceAccess`, not on
  `ResourceKind`/image dimensionality at all — see Step 2's confirmed
  finding above. No change needed here; reusing `ComputeShaderRead`/
  `ComputeShaderWrite`/`ShaderRead` for a volume texture needs zero new
  logic in this file.
- `RenderGraph`'s internal per-frame bookkeeping (whatever tracks "this
  resource's current `VkImageLayout`/access for barrier purposes between
  passes" — read the exact mechanism from `RenderGraph.cpp`, likely a
  parallel table keyed by handle) needs a THIRD such table for
  `VolumeTextureHandle`, mirroring however it already does this for
  `TextureHandle`/`BufferHandle` separately. **Confirmed**: the real
  mechanism is `RenderGraph`'s private `PhysicalTexture`/`PhysicalBuffer`
  structs plus `std::vector<PhysicalTexture> physicalTextures`/
  `std::vector<PhysicalBuffer> physicalBuffers` locals inside
  `ExecuteCompiledGraph()`, resolved lazily via `EnsureTextureResolved()`/
  `EnsureBufferResolved()` — a volume texture needs its own
  `PhysicalVolumeTexture` struct, its own `std::vector<PhysicalVolumeTexture>
  physicalVolumeTextures` local, and its own `EnsureVolumeTextureResolved()`,
  wired into a genuinely three-way (not `if/else`) `ApplyUsageBarrierIfNeeded()`
  — see 3.2's corrected fix above, which this depends on directly.
- `PassContext::resolveVolumeTexture(VolumeTextureHandle) -> VolumeTexture&`
  — mirrors `resolveTexture()`/`resolveBuffer()`'s exact resolution logic
  (look up the imported resource by handle; throw/assert on an unresolved
  handle exactly like the existing two do). Confirmed shape: both existing
  resolvers are plain `std::function` fields on `PassContext` (fully defined
  in `RenderGraph.h`), wired up freshly inside `ExecuteCompiledGraph()`'s
  per-pass loop — `resolveVolumeTexture` should be a third field of the
  same kind.

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
  needed purely to keep it alive for this test — **this matters doubly now**:
  per Step 2/3.2's corrected finding, a volume-texture-only pass has NO path
  to `finalOutputs` at all unless something else also reads it, since
  `VolumeTextureHandle` can never itself be a root, exactly like
  `BufferHandle` today), the barrier planner emits a correct transition
  before the compute shader writes it, and no validation error/warning
  appears anywhere in the sequence.
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
  `RenderGraphTypes.h`'s own comment on why three (now four) separate handle
  structs exist instead of one template).
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
  (Confirmed low-risk even unfixed: `ResourceUsageName()`'s own `if/else`
  is bounds-checked and degrades to an empty display string, never a crash
  — see Step 2's corrected finding for exactly why this one file is lower
  priority than the two that must be fixed.)

## Step 5: Their Role

- This phase's completion report MUST explicitly answer, in writing: (a)
  whether `RenderGraphCompiler`'s dependency walk needed any changes at all
  and why/why not — **the corrected answer, confirmed by this precheck, is
  YES: it is NOT already generic over `kind`, it uses plain `if/else`
  (never an exhaustive switch), and every one of the sites listed in 3.2
  must be hand-fixed to a real three-way branch BEFORE `ResourceKind::
  VolumeTexture` is added, or a real out-of-bounds vector access becomes
  reachable via `RenderGraph::EnsureBufferResolved()`** — (b) whether
  `RenderGraphBarrierPlanner` needed any changes beyond "it just worked
  because it's keyed on `ResourceAccess` alone" — **confirmed true, zero
  changes needed there** — and (c) the final decision on whether
  `CreateVolumeTexture()` (pooled) was implemented or deliberately deferred.
  Phase 6 depends on knowing these answers precisely before it can be
  implemented correctly.
- If the disposable validation in 3.5 reveals the barrier planner does NOT
  correctly synchronize a volume-texture write (e.g. a validation-layer
  warning appears), **do not proceed to Phase 3-9 until this is fixed** —
  every later phase in this campaign is only safe to build once this
  foundation is proven correct, exactly the same non-negotiable prerequisite
  the GPU Vertex Skinning campaign's own Phase 3 WAW-hazard fix represented
  for ITS later phases.
