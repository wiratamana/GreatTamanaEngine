# COMPUTE_PHASE1_COMPLETION_REPORT.md

Session report for **Phase 1 — Make data usable by compute shaders** (the
"Resource Vocabulary" phase) of the compute-shader campaign described in
`COMPUTE_SHADER_MASTER_STRATEGY_v2.md`. Scope was taken directly from
`COMPUTE_PHASE1_RESOURCE_VOCABULARY_STRATEGY_v2.md` — including its v2 "Step
6" amendments (the `extraUsage` parameter on the structured-buffer factory,
and the explicit "no transient/pooled `RWTexture` yet" scope boundary).
Nothing beyond that document's own "Step 3: The Plan" was implemented, per
its own "Step 4: What We Will NOT Do".

Work was done on the pre-existing branch `feature/compute-shader-impl`.

## What shipped

Every change is purely additive to already-shipped Buffer/RenderTexture/
Texture2D/GpuResourceFactory/Renderer code — no existing call site's
behavior changed (every new parameter has a default value that reproduces
the prior behavior exactly), and no compute pipeline/descriptor/dispatch/
render-graph work was touched at all (that's Phases 2-6).

### New file: storage-image format capability query

- **`src/Renderer/Vulkan/FormatCapabilities.h`/`.cpp`** — a single new free
  function, `bool SupportsStorageImageUsage(VkPhysicalDevice, VkFormat)`,
  mirroring `VulkanDevice::PickDepthFormat()`'s own "ask the device, never
  hardcode" discipline (see `AGENTS.md`, "Render Target Format Matching")
  for a different feature bit: it queries
  `vkGetPhysicalDeviceFormatProperties()` and checks whether
  `optimalTilingFeatures` includes `VK_FORMAT_FEATURE_STORAGE_IMAGE_BIT`.
  This is the one and only place in the engine that answers "can a storage
  image (an `RWTexture`) actually be created at this format on this
  device/driver" — every other new code path below calls through it rather
  than assuming.

### `RWStructuredBuffer` / `StructuredBuffer` — buffers

- **`GpuResourceFactory::CreateStructuredBuffer()`** (new) — a thin wrapper
  over the existing `CreateBuffer()`: `size = elementStride * elementCount`,
  always ORs in `VK_BUFFER_USAGE_STORAGE_BUFFER_BIT` (+
  `VK_BUFFER_USAGE_TRANSFER_DST_BIT` when `memoryUsage ==
  BufferMemoryUsage::GpuOnly`, mirroring `CreateDeviceLocalBuffer()`'s own
  convention), plus an `extraUsage` parameter (default `0`) so a future
  caller (e.g. the companion GPU-driven-rendering document's indirect-draw
  buffer) can OR in additional flags like
  `VK_BUFFER_USAGE_INDIRECT_COMMAND_BIT` without needing a second,
  near-duplicate factory method — this is the v2 "Step 6" amendment,
  implemented from day one rather than retrofitted later.
- **`Renderer::CreateStructuredBuffer()`** — a plain one-line forward, same
  shape as every other `Renderer` → `GpuResourceFactory` passthrough.
- **No new C++ type** — `Buffer` itself is completely untouched. The
  `RWStructuredBuffer`-vs-`StructuredBuffer` (read-write vs. read-only)
  distinction is *documented*, not enforced in C++: it's a GLSL-side
  (`readonly buffer` vs. plain `buffer`) and, later, render-graph-side
  (`ComputeShaderRead` vs. `ComputeShaderWrite`, Phase 5's job) distinction
  applied to the exact same Vulkan buffer/descriptor type
  (`VK_DESCRIPTOR_TYPE_STORAGE_BUFFER`).

### `RWTexture` — storage images

- **`RenderTexture`** (`RenderTexture.h`/`.cpp`) — gained a new
  `allowStorageImageAccess` constructor parameter (default `false`, so
  every existing constructor call is unaffected), stored as a new
  `m_allowStorageImageAccess` member (participates correctly in the move
  constructor/move-assignment, and is re-applied automatically on every
  `Resize()` since `Create()` reads the member, not a constructor-local
  value). When `true`, `Create()` additionally ORs
  `VK_IMAGE_USAGE_STORAGE_BIT` into the color image's `VkImageCreateInfo`.
  A new `AllowsStorageImageAccess()` accessor was added for a future
  descriptor-set builder (Phase 3) to query. **The capability CHECK itself
  lives one layer up, in `GpuResourceFactory::CreateRenderTexture()`** (see
  below) — `RenderTexture`'s own constructor unconditionally trusts that
  the check already happened, exactly as the strategy document specifies.
- **`Texture2D`** (`Texture2D.h`/`.cpp`) — the identical treatment: a new
  `allowStorageImageAccess` constructor parameter (default `false`), a new
  `m_allowStorageImageAccess` member (also carried through move ctor/
  assignment), `VK_IMAGE_USAGE_STORAGE_BIT` conditionally OR'd into its
  fixed `VK_FORMAT_R8G8B8A8_UNORM` image, and a matching
  `AllowsStorageImageAccess()` accessor.
- **`GpuResourceFactory::CreateRenderTexture()`/`CreateTexture2D()`** — both
  gained the same new `allowStorageImageAccess` parameter (default
  `false`). When `true`, each method calls `SupportsStorageImageUsage()`
  against the target format (the caller-supplied `format` for
  `CreateRenderTexture()`; the fixed `VK_FORMAT_R8G8B8A8_UNORM` for
  `CreateTexture2D()`) and **throws `std::runtime_error` loudly** if the
  device doesn't actually support it, rather than silently creating a
  texture a compute shader could never actually bind as a storage image.
  This is why `GpuResourceFactory` now also stores a `VkPhysicalDevice`
  (new constructor parameter, threaded from `Renderer`'s own
  `m_device.Physical()`) purely for this one capability check — nothing
  else in `GpuResourceFactory` needed it before.
- **`Renderer::CreateRenderTexture()`/`CreateTexture2D()`** — both gained
  the matching new parameter and forward it straight through.
- **Explicit scope boundary honored (v2 "Step 6")**: nothing here touches
  `rg::TextureDesc`/`RenderGraphResourcePool` — a storage-capable
  `RenderTexture` can only be created directly (via
  `Renderer::CreateRenderTexture()`), never as a transient,
  render-graph-pooled resource requested through
  `RenderGraphBuilder::CreateTexture()`. This is deliberate, documented in
  every relevant comment added this phase, and matches the master
  document's own "What We Will NOT Do" list.

### `Texture` (plain, read-only sampled texture) — no changes

Confirmed (not modified) that `GpuResourceFactory::CreateMaterialTexture2D()`'s
existing `VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER` descriptor is already
exactly what a compute shader needs for a read-only `Texture` binding — a
`sampler2D` in a future `.comp` file is bound/sampled identically to one in
a `.frag` file today. Nothing to build here, as the strategy document
itself states.

## Build system changes

- Root `CMakeLists.txt`: added `src/Renderer/Vulkan/FormatCapabilities.h`/
  `.cpp` to `gte_core`'s source list, right after
  `Vulkan/VulkanQueryPool.h`/`.cpp`.
- No test-file changes this phase — the new capability-query function and
  every new parameter are Tier-2 (GPU-touching, `VkPhysicalDevice`/
  `VkDevice`-dependent) by nature, falling into the same accepted "no
  automated coverage yet, manually verified" bucket as `Buffer`/
  `RenderTexture`/`Pipeline`/`GpuResourceFactory` themselves (see
  `TESTING.md`'s own note on this). No new Tier-1-testable pure logic was
  introduced this phase (unlike, say, Phase 4's dispatch-math, which is
  explicitly pure and will get real unit tests).

## Verification performed

- Reconfigured with CMake (Ninja generator, reusing the existing `build/`
  directory) — no network access needed, everything was already fetched.
- Built `gte_core` from a clean-of-these-changes incremental build —
  compiled with zero warnings/errors introduced by the new/changed files.
- Built the full project (`GreatTamanaEngine.exe` **and**
  `GreatTamanaEngineTests.exe`) — both link successfully; shaders staged
  correctly next to the executable.
- Ran the **entire** existing test suite: **625 of 626 tests passed**, **1
  skipped** (`PmxLoaderRealModelSmokeTest.LoadsAnMmdModelIfPresentOnThisMachine`,
  a pre-existing machine-gated smoke test unrelated to this change — the
  referenced real MMD model file isn't present on this machine; expected
  and documented in `TESTING.md`). **Zero regressions.**
- Launched the real `GreatTamanaEngine.exe` (which constructs a real
  `Renderer`/`GpuResourceFactory`/live `VkPhysicalDevice` on startup,
  exercising the new constructor signature end-to-end) and confirmed it
  stayed running (no crash/exception at startup) before stopping it —
  since `GpuResourceFactory`'s constructor signature and
  `Renderer`'s member-initializer-list order both changed, this was worth
  confirming directly rather than trusting the test suite alone (the test
  suite never constructs a live `Renderer`/`GpuResourceFactory` — see
  `TESTING.md`'s "Tier 2" note).

## Acceptance criteria check (against the strategy doc's own "Step 3: The Plan")

- ✅ `Buffer` gained no new fields/constructor overload — `CreateStructuredBuffer()`
  is a pure factory-level convenience, exactly as specified.
- ✅ `RenderTexture`/`Texture2D` both gained the storage-image opt-in with
  a default that leaves every existing call site byte-for-byte unaffected.
- ✅ The capability check (`SupportsStorageImageUsage()`) lives at the
  `GpuResourceFactory` layer, not inside `RenderTexture`/`Texture2D`
  themselves — matching the document's explicit "the CALLER... is
  responsible" wording.
- ✅ `CreateStructuredBuffer()` accepts the v2-mandated `extraUsage`
  parameter (Step 6, Finding 5) so the companion GPU-driven-rendering
  document's own indirect-draw-buffer workload can reuse this same method.
- ✅ No transient/render-graph-pooled `RWTexture` support was added (Step
  6, Finding 1) — `RenderGraphBuilder`/`RenderGraphResourcePool`/
  `rg::TextureDesc` are all completely untouched this phase.
- ✅ `Texture`/`MaterialTexture` path confirmed already sufficient, zero
  changes made.

## What was deliberately NOT done (per the strategy doc's own "Step 4" and its v2 "Step 6")

- No `ComputePipeline`, shader loading, or `.comp` compilation — Phase 2's
  job.
- No descriptor-set-layout builder — Phase 3's job.
- No dispatch math/`Renderer::Dispatch()` — Phase 4's job.
- No `ResourceAccess` enum additions (`ComputeShaderRead`/`ComputeShaderWrite`)
  — Phase 5's job (this phase only supplies the resource *kinds* those
  future access values will apply to).
- No `RenderGraphBuilder`/`PassBuilder` changes (`WriteTexture()`, etc.) —
  Phase 6's job.
- No typed/templated buffer wrapper (`Buffer<T>`) — `elementStride`/
  `elementCount` on `CreateStructuredBuffer()` are plain bookkeeping only.
- No 3D/array/cubemap storage images — `RWTexture` here means plain 2D
  only, matching `RenderTexture`/`Texture2D`'s own existing scope.
- No transient (render-graph-pooled) `RWTexture` — `rg::TextureDesc` has no
  usage-flags field, and extending it is explicitly deferred, documented
  follow-up work belonging to a later phase/document, not this one (see
  Step 6, Finding 1 in the strategy doc).

## Handoff notes for whoever picks up Phase 2

- Phase 2 (`COMPUTE_PHASE2_PIPELINE_INFRASTRUCTURE_STRATEGY_v1.md`) is the
  next unit of work — building `ComputePipeline`, a shared SPIR-V/
  `VkShaderModule` loader extracted out of `Pipeline.cpp`, and confirming
  `.comp` support in `cmake/CompileShaders.cmake`'s `gte_add_shader()`.
- Every `RWTexture` created from here on must be an externally-owned,
  persistent `RenderTexture`/`Texture2D` (via `Renderer::CreateRenderTexture(
  ..., allowStorageImageAccess=true)`/`CreateTexture2D(...,
  allowStorageImageAccess=true)`), then brought into a render graph pass
  via `RenderGraphBuilder::ImportTexture()` once Phase 6 lands — never
  requested through `RenderGraphBuilder::CreateTexture()`. This constraint
  is now baked into every doc comment this phase added; do not forget it
  when Phase 7's blur validation workload is built.
- When choosing a format for a NEW storage-capable texture that has no
  other format constraint (i.e. it's never bound to the same `Pipeline` as
  the swapchain/Game/Scene views), prefer `VK_FORMAT_R8G8B8A8_UNORM`
  (already `Texture2D`'s own proven format) over
  `Renderer::ColorFormat()`/`VK_FORMAT_UNDEFINED` — the latter's negotiated
  swapchain format (commonly `VK_FORMAT_B8G8R8A8_UNORM`) is NOT guaranteed
  to support `VK_FORMAT_FEATURE_STORAGE_IMAGE_BIT` on every driver/GPU. A
  `SupportsStorageImageUsage()` failure for `Renderer::ColorFormat()` on
  some GPU is an expected, legitimate outcome, not a bug — see Phase 1's
  own Step 6, Finding 2 and Phase 7's Step 6.
- `GpuResourceFactory` now carries a `VkPhysicalDevice` member purely for
  the storage-image capability check — if a future phase needs the
  physical device for something else too, reuse this member rather than
  threading a second copy through.
