# COMPUTE_PHASE1_RESOURCE_VOCABULARY_STRATEGY_v2.md

### Child document 1 of 7 — see `COMPUTE_SHADER_MASTER_STRATEGY_v2.md` for the full campaign map.

> **v2 (2nd-iteration review):** this document's Step 1-5 body is IDENTICAL to
> `COMPUTE_PHASE1_RESOURCE_VOCABULARY_STRATEGY_v1.md`. New material was
> appended as **Step 6** below, after re-reading this plan directly against
> the real, currently-shipped `Buffer.cpp/.h`, `RenderTexture.cpp/.h`,
> `Texture2D.cpp/.h`, `RenderGraphTypes.h`, and `RenderGraphResourcePool.cpp/.h`.
> Read Step 6 before implementing this phase — it changes what "done" means
> for the `RWTexture` half in a way v1 doesn't mention at all.

## Step 1: The Goal

Give the engine's existing GPU resource types (`Buffer`, `RenderTexture`,
`Texture2D`) exactly the Vulkan-level plumbing needed to stand in for
Unity's four compute-shader resource kinds — **without** yet building the
compute pipeline, descriptor layout, dispatch, or render-graph pieces that
actually use them (those are Phases 2-6). By the end of this phase:

1. A `Buffer` can be created with `VK_BUFFER_USAGE_STORAGE_BUFFER_BIT`, the
   single Vulkan usage flag behind BOTH `RWStructuredBuffer` and
   `StructuredBuffer` (the read/write-vs-read-only distinction is a GLSL/
   render-graph-level concept, not a different Vulkan object — see below).
2. A `RenderTexture`/`Texture2D` can OPTIONALLY be created with
   `VK_IMAGE_USAGE_STORAGE_BIT`, the flag behind `RWTexture`, with the
   engine correctly checking the target format actually supports storage
   image use before attempting it (not every `VkFormat` does).
3. The existing combined-image-sampler `Texture`/`MaterialTexture` path
   needs **no changes at all** — a compute shader samples a `Texture` via a
   plain `sampler2D`, exactly like a fragment shader already does; this is
   already fully supported by `GpuResourceFactory::CreateMaterialTexture2D()`.

## Step 2: The Situation

- `Buffer` (`src/Renderer/Buffer.h/.cpp`) already accepts an arbitrary
  `VkBufferUsageFlags` parameter — nothing structurally prevents passing
  `VK_BUFFER_USAGE_STORAGE_BUFFER_BIT` today, but no call site does, and
  there is no dedicated, named helper making "I want a structured buffer a
  compute shader can read/write" an obvious, discoverable operation (a
  caller would otherwise have to know the exact Vulkan flag by heart).
- `RenderTexture`/`Texture2D` (`src/Renderer/RenderTexture.h/.cpp`,
  `Texture2D.h/.cpp`) both hardcode their own `VkImageUsageFlags` internally
  (`COLOR_ATTACHMENT_BIT | SAMPLED_BIT` and `TRANSFER_DST_BIT | SAMPLED_BIT`
  respectively) with no way for a caller to add `STORAGE_BIT` at all.
- Vulkan's storage-image usage has a real, driver-dependent constraint that
  none of this engine's existing texture code has ever had to think about:
  not every `VkFormat` supports `VK_IMAGE_USAGE_STORAGE_BIT` as an
  "optimal tiling feature" (query via
  `vkGetPhysicalDeviceFormatProperties()`'s
  `optimalTilingFeatures & VK_FORMAT_FEATURE_STORAGE_IMAGE_BIT`) — this is
  the exact same *kind* of capability check `VulkanDevice::PickDepthFormat()`
  already performs for depth formats, just for a different feature bit.
  `VK_FORMAT_R8G8B8A8_UNORM` (this engine's one texture format,
  `Texture2D.cpp`) and the swapchain's negotiated color format both
  typically DO support storage image use on desktop GPUs, but this must be
  verified via the API, never assumed.
- GLSL's own storage-image declarations require an explicit format
  qualifier (e.g. `layout(binding = 1, rgba8) uniform image2D`) that must
  match the image's real `VkFormat` — a mismatch is undefined behavior per
  the Vulkan spec, not something the driver validates for you at pipeline-
  creation time in every case. This is a *documentation and convention*
  problem this phase must call out explicitly, not a code problem this
  phase can programmatically prevent (no shader reflection exists in this
  engine — see the master document's "What We Will NOT Do").
- `AGENTS.md`'s "GPU Resource Memory Tracking" rules apply unchanged here:
  every new call path must still register with `GpuMemoryTracker` via the
  existing `Track()`/`Untrack()` calls already inside `Buffer`/`RenderTexture`/
  `Texture2D` — a storage buffer/image is memory-tracked exactly like any
  other buffer/image, with no special case needed (VMA/`GpuMemoryTracker`
  don't distinguish by usage flag, only by size/location).

## Step 3: The Plan

### `RWStructuredBuffer` / `StructuredBuffer` — one Vulkan buffer, two GLSL/graph-level roles

- Add `GpuResourceFactory::CreateStructuredBuffer(VkDeviceSize elementStride,
  std::uint32_t elementCount, BufferMemoryUsage memoryUsage, const char*
  debugName)` — a thin, self-documenting wrapper over the existing
  `CreateBuffer()`, computing `size = elementStride * elementCount` and
  always OR-ing in `VK_BUFFER_USAGE_STORAGE_BUFFER_BIT` (plus
  `VK_BUFFER_USAGE_TRANSFER_DST_BIT` when `memoryUsage ==
  BufferMemoryUsage::GpuOnly`, mirroring `CreateDeviceLocalBuffer()`'s own
  existing convention, so a GPU-only structured buffer can still be
  initialized once via a staging upload if the caller wants that).
  `Renderer::CreateStructuredBuffer()` forwards to it exactly like every
  other `GpuResourceFactory` method Renderer already forwards.
- `elementStride`/`elementCount` are **bookkeeping only** — `Buffer` itself
  stays exactly what it is today (an untyped `VkBuffer` + byte size),
  matching Vulkan's own untyped-buffer model. They exist purely so a
  debug name / a future Memory-panel row / a future validation assert can
  say "128 elements of 32 bytes" instead of just "4096 bytes" — do not
  build a typed/templated `Buffer<T>` wrapper class; this project's
  existing philosophy (see `Buffer.h`'s own class comment) is a single,
  general-purpose untyped buffer type used for everything, and this phase
  should not deviate from that.
- **The `RWStructuredBuffer` vs. `StructuredBuffer` distinction is enforced
  in exactly two places, neither of which is the Vulkan buffer object
  itself:**
  1. The GLSL shader source: a `buffer` block a shader only reads from is
     declared `readonly buffer InputBuffer { ... }`; one it writes to omits
     `readonly`. This is a hand-authored discipline per shader, not
     something C++ enforces.
  2. The render graph's own declared `ResourceAccess` for that handle
     (`ComputeShaderRead` for a `StructuredBuffer`, `ComputeShaderWrite` for
     an `RWStructuredBuffer`) — see Phase 5 for the enum values themselves,
     and Phase 6 for how a pass declares them. This is what makes the
     *barrier planner* (not the GPU) treat the two differently — a
     `StructuredBuffer` never triggers a write-hazard barrier against a
     LATER read of the same handle, exactly as an `IsWriteAccess() ==
     false` `ResourceAccess` already behaves today for `ShaderRead` on a
     texture.
- Document this split explicitly (in this file, and again as a comment at
  the eventual `CreateStructuredBuffer()` call site) so a future engineer
  never goes looking for a "read-only storage buffer" Vulkan flag that does
  not exist.

### `RWTexture` — storage image opt-in for `RenderTexture`/`Texture2D`

- Add an `allowStorageImageAccess` bool parameter (default `false`,
  preserving every existing call site's behavior unchanged) to
  `RenderTexture`'s constructor/`Resize()` and to `GpuResourceFactory::
  CreateRenderTexture()`/`Renderer::CreateRenderTexture()`. When `true`,
  `RenderTexture::Create()` OR's `VK_IMAGE_USAGE_STORAGE_BIT` into its
  existing `imageInfo.usage` — but only after confirming the target format
  actually supports it (see below); throw the same kind of
  `std::runtime_error` `RenderTexture`'s constructor already throws for a
  failed `vmaCreateImage()` if the format doesn't support storage-image use
  and the caller asked for it anyway — a silent fallback would be a subtler
  bug than a loud failure.
- Add the exact same opt-in to `Texture2D`'s constructor/`GpuResourceFactory::
  CreateTexture2D()`, for the (less common but real) case of a plain,
  CPU-authored texture a compute shader also needs to write into (e.g. a
  procedurally-populated lookup table).
- New free function, `bool SupportsStorageImageUsage(VkPhysicalDevice,
  VkFormat)` (natural home: alongside `VulkanDevice::PickDepthFormat()`'s
  own format-capability-query style, e.g. a small addition to
  `VulkanDevice.h/.cpp` or a new tiny `Vulkan/FormatCapabilities.h/.cpp` —
  decide based on whether more format queries accumulate later) — queries
  `vkGetPhysicalDeviceFormatProperties()` and checks
  `optimalTilingFeatures & VK_FORMAT_FEATURE_STORAGE_IMAGE_BIT`, mirroring
  `PickDepthFormat()`'s own "ask the device, never hardcode" discipline
  exactly (see `AGENTS.md`, "Render Target Format Matching").
- Document, at every `RWTexture`-capable call site, the required GLSL
  format qualifier convention: the image's real `VkFormat` must be named in
  a comment immediately next to wherever its `layout(..., <qualifier>)
  uniform image2D` is declared in the corresponding `.comp` file, so the
  two never drift apart silently. `VK_FORMAT_R8G8B8A8_UNORM` ↔ `rgba8`,
  and `Renderer::ColorFormat()`'s negotiated swapchain format (commonly
  `VK_FORMAT_B8G8R8A8_UNORM`, but never hardcoded — see `AGENTS.md`) needs
  its GLSL qualifier resolved the same way `ColorFormat()` itself is
  resolved at runtime, not assumed at shader-authoring time; a compute
  shader meant to write directly into a Game/Scene `RenderTexture` should
  target a format known to be storage-compatible regardless of what the
  swapchain negotiated, or use a dedicated intermediate texture instead
  (see Phase 7's blur validation workload, which does exactly this via a
  ping-pong pair rather than writing into the swapchain-matched texture
  directly).

### `Texture` — no changes needed

- Confirm (do not modify) that `GpuResourceFactory::CreateMaterialTexture2D()`'s
  existing combined-image-sampler descriptor (`VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER`,
  `VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL`) is already exactly what a
  compute shader needs for a read-only `Texture` binding — a `sampler2D`
  declared in a `.comp` file's descriptor set is bound and sampled
  identically to one in a `.frag` file. Record this explicitly as "already
  done, nothing to build" so Phase 3 doesn't accidentally re-invent it.

## Step 4: What We Will NOT Do

- No typed/templated buffer wrapper (`Buffer<T>`) — `elementStride`/
  `elementCount` are plain bookkeeping fields on the factory call, never a
  new C++ type.
- No 3D textures, texture arrays, or cubemap storage images — `RWTexture`
  here means a plain 2D `RenderTexture`/`Texture2D` only.
- No automatic GLSL-format-qualifier-vs-`VkFormat` validation — this is a
  documented human convention (see Phase 3/6 for where binding numbers get
  the same treatment), not a compile-time or runtime check, consistent with
  this engine's explicit "no shader reflection" scope decision.
- No change to `MaterialTexture`/`GpuResourceFactory::CreateMaterialTexture2D()`
  — the plain `Texture` role is already fully served by existing code.
- No change to `Buffer`'s public API shape beyond the new factory
  convenience method — `Buffer` itself gains no new fields, no new
  constructor overload.

## Step 5: Their Role

- Land the `Buffer` (`CreateStructuredBuffer()`) half first — it is the
  simpler of the two (no format-capability query, no GLSL-qualifier
  concern) and is what the GPU-driven-rendering companion document's own
  Phase A/culling workload needs immediately.
- Land the `RenderTexture`/`Texture2D` storage-image opt-in second, and
  validate it manually (Tier 2, per `AGENTS.md`/`TESTING.md`'s existing
  accepted bucket for GPU-touching code) with validation layers enabled —
  specifically confirm `SupportsStorageImageUsage()` correctly reports
  `true` for `VK_FORMAT_R8G8B8A8_UNORM` and for whatever this engine's
  `Renderer::ColorFormat()` negotiates at runtime on your actual
  development GPU, since this is the first place this engine has ever
  needed to ask a physical device about `VK_FORMAT_FEATURE_STORAGE_IMAGE_BIT`
  specifically.
- Hand this phase's output to Phase 2 (`ComputePipeline`) and Phase 3
  (descriptor binding) — neither can meaningfully be validated until real
  storage buffers/images exist to bind to something.

---

## Step 6: V2 Revision Notes (2nd-Iteration Review)

Checked directly against the real, currently-shipped
`src/Renderer/RenderGraph/RenderGraphTypes.h` and
`src/Renderer/RenderGraph/RenderGraphResourcePool.cpp/.h` — three amendments:

1. **New, explicit scope boundary: no TRANSIENT (render-graph-pooled)
   `RWTexture` support in this phase, or anywhere in this campaign, unless a
   later phase deliberately extends `rg::TextureDesc`.** `rg::TextureDesc`
   (already shipped, part of the original Render Graph campaign) is exactly
   `{ width, height, format, hasDepth }` — no usage-flags field of any kind.
   `RenderGraphResourcePool::AcquireTexture(const TextureDesc& desc, const
   char* debugName)` always creates a fresh pool entry (when no matching,
   unclaimed entry exists) via
   `m_renderer->CreateRenderTexture(width, height, desc.format, debugName,
   nullptr)` — there is no path, anywhere in this call chain, for a
   `RenderGraphBuilder::CreateTexture()`-declared (i.e. transient,
   pool-managed) texture to ever request `allowStorageImageAccess = true`.
   **Consequence: every `RWTexture` this campaign ever creates MUST be an
   externally-owned, persistent `RenderTexture`/`Texture2D`, created
   directly via `Renderer::CreateRenderTexture()`/`CreateTexture2D()` with
   `allowStorageImageAccess = true`, and brought into a pass via
   `RenderGraphBuilder::ImportTexture()` — never via
   `RenderGraphBuilder::CreateTexture()`.** This happens to already be true
   of Phase 7's own `blurredSceneOutput` design (a dedicated, persistent
   `RenderTexture`, imported every frame, exactly like the Game/Scene
   views), but v1 never said this was REQUIRED or explained why — a future
   engineer reading only Phase 1/6 could reasonably assume
   `builder.CreateTexture("Foo", desc)` already supports a storage-capable
   transient resource, since `BufferDesc` (the buffer sibling) already
   carries a full `VkBufferUsageFlags usage` field and DOES support an
   arbitrary storage-buffer-shaped transient `BufferHandle` today. Add this
   exact asymmetry (`BufferDesc` is usage-flag-generic; `TextureDesc` is
   not) as an explicit callout in this phase's own "What We Will NOT Do"
   list: *"No transient (render-graph-pooled) `RWTexture` — `rg::TextureDesc`
   has no usage-flags field, and extending it (plus threading a matching
   opt-in through `RenderGraphResourcePool::AcquireTexture()`) is real,
   deferrable follow-up work belonging to a future phase/document, not this
   one."*
2. **Format-choice guidance for any `RWTexture` a compute shader must
   WRITE.** Do not default to `VK_FORMAT_UNDEFINED`/`Renderer::ColorFormat()`
   for a texture that needs to be a write target — `Renderer::ColorFormat()`
   reflects whatever the swapchain actually negotiated at runtime (commonly
   `VK_FORMAT_B8G8R8A8_UNORM` per `VulkanSwapchain.cpp`), and
   `VK_FORMAT_FEATURE_STORAGE_IMAGE_BIT` support for BGRA8 formats is
   genuinely NOT guaranteed across all Vulkan drivers/GPUs — some report it,
   some don't. `VK_FORMAT_R8G8B8A8_UNORM` (already this engine's own proven,
   hardcoded `Texture2D` format) is a much safer default for any NEW,
   dedicated storage-capable texture that has no other reason to match
   `ColorFormat()` (i.e. it is never bound to the SAME `Pipeline` as the
   swapchain/Game-Scene views — see `AGENTS.md`, "Render Target Format
   Matching," for why format-matching only matters when a pipeline is
   actually shared). A `SupportsStorageImageUsage()` check that returns
   `false` for `Renderer::ColorFormat()` on some GPU is therefore an
   EXPECTED, legitimate possible outcome, not a bug to chase — Step 5's
   manual-verification bullet should not be read as implying that call must
   succeed.
3. **`CreateStructuredBuffer()` should accept an optional extra usage-flags
   parameter.** Add `VkBufferUsageFlags extraUsage = 0` to
   `GpuResourceFactory::CreateStructuredBuffer(...)`/
   `Renderer::CreateStructuredBuffer(...)`, OR'd in alongside the always-on
   `VK_BUFFER_USAGE_STORAGE_BUFFER_BIT` (+ the existing `GpuOnly`-only
   `TRANSFER_DST_BIT`). Rationale: the companion
   `GPU_DRIVEN_RENDERING_COMPUTE_INDIRECT_STRATEGY_v1.md` document's own
   culling workload produces an INDIRECT DRAW buffer, which also needs
   `VK_BUFFER_USAGE_INDIRECT_COMMAND_BIT` set on the very same
   `VkBuffer` a compute shader writes as a plain `RWStructuredBuffer` —
   without this parameter, that document would need a second, separate,
   near-duplicate structured-buffer factory method instead of reusing this
   one, directly contradicting this campaign's own stated goal ("this
   campaign generalizes ... infrastructure any future compute shader can
   use... build it once, reference it from both documents"). This is a
   zero-risk, backward-compatible addition (default `0` preserves every
   other call site's exact original behavior).
