# Atmosphere Phase 2 — Completion Report

**Phase:** `ATMOSPHERE_PHASE2_VOLUME_TEXTURE_RENDERGRAPH_SUPPORT_v1.md`
**Branch:** `feature/atmosphere-scattering-impl`
**Status:** Complete — `gte_core` and `GreatTamanaEngineTests` both build cleanly (targeted incremental build only, per this campaign's own workflow rule); all 149 `RenderGraph*` Tier-1 tests pass (10 new); the engine was actually run (validation layers enabled) with a disposable end-to-end validation pass proving the new `VolumeTexture`/`ResourceKind::VolumeTexture` plumbing, then that disposable code was deleted before writing this report.

## What changed

### 1. `src/Renderer/VolumeTexture.h/.cpp` + `src/Renderer/VolumeTarget.h`

A new RAII 3D-image GPU resource type, `VolumeTexture`, mirroring `Texture2D`'s
shape but `VK_IMAGE_TYPE_3D`/`VK_IMAGE_VIEW_TYPE_3D`, with an explicit
`format` parameter (unlike `Texture2D`'s fixed `VK_FORMAT_R8G8B8A8_UNORM`) and
**always** `VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_SAMPLED_BIT` (no
opt-in flag, unlike `RenderTexture`/`Texture2D`'s `allowStorageImageAccess`).
Single mip level, trilinear/clamp-to-edge sampler in all three axes.
Registers with `GpuMemoryTracker` as `GpuResourceType::Texture` (that enum has
no 2D-vs-3D distinction). The storage-image-format-capability check
(`Vulkan/FormatCapabilities.h`'s `SupportsStorageImageUsage()`) lives in
`GpuResourceFactory::CreateVolumeTexture()`/`Renderer::CreateVolumeTexture()`,
never inside the constructor — matches `Texture2D`'s own "constructor
trusts, factory checks" division of labor exactly.

`VolumeTarget` (new, own header) is the plain, non-owning 3D-image
counterpart of `RenderTarget.h` — `VkImage`/`VkImageView`/`VkExtent3D`/
`VkFormat`, no ownership, no depth-companion concept. `VolumeTexture::Target()`
returns one, exactly mirroring `RenderTexture::Target()`. This is what lets
`RenderGraphBuilder.h` stay decoupled from `VolumeTexture.h`'s own (heavier)
header — it only ever `#include`s `VolumeTarget.h`, never `VolumeTexture.h`,
preserving the exact layering discipline `RenderTarget.h` already established.

### 2. The if/else audit-and-fix pass (done BEFORE adding `ResourceKind::VolumeTexture`)

Per this phase's own precheck callout, every `if (usage.kind ==
ResourceKind::Texture) {...} else {...}` (or equivalent) site across
`RenderGraphCompiler.cpp`/`RenderGraph.cpp` was converted to a real,
exhaustive, `default:`-less three-way `switch (usage.kind)` — mirroring
`IsWriteAccess()`'s own convention — so a *future* fourth resource kind gets
the same compile-time safety net this document's own precheck found this one
never had:

- `RenderGraphCompiler.cpp`: the RAW-edge writer lookup, the WAW-edge/
  last-writer update, and the resource-lifetime `touch()` lambda (three
  sites, converted to real `switch` statements with a new
  `lastVolumeTextureWriter`/`result.volumeTextureLifetimes` tracked
  alongside the texture/buffer equivalents).
- `RenderGraph.cpp`: `ApplyUsageBarrierIfNeeded()`'s top-level branch
  (converted to a real `switch`, with a new `ResourceKind::VolumeTexture`
  case calling the new `EnsureVolumeTextureResolved()`).

Two sites named in the strategy document turned out to need **no code
change at all**, and this report explicitly records why (confirmed by
re-reading them, not merely trusted from the document's own paraphrase):

- `RenderGraphCompiler.cpp`'s backward-reachability root-marking scan
  (`usage.kind == ResourceKind::Texture && ContainsTextureHandle(...)`) —
  this is a plain `if` with **no `else` branch at all**; it already, by
  construction, treats every non-Texture kind identically (never marks a
  pass "kept" via this check) — correct for a `BufferHandle` today and for
  a `VolumeTextureHandle` now, with zero modification needed.
- `RenderGraph.cpp`'s `ExecuteCompiledGraph()` color/depth-attachment-write
  scan (`if (usage.kind != ResourceKind::Texture) { continue; }`) — same
  reasoning: `!=` already excludes every non-Texture kind uniformly, so a
  volume-texture write correctly never triggers a `vkCmdBeginRendering`
  bracket, with zero modification needed either.

`RenderGraphSnapshot.cpp`'s `ResourceUsageName()` (Editor "Render Graph"
panel display only) was deliberately **left unfixed**, per this phase's own
explicit "What We Will NOT Do" scope note — a volume-texture usage there
degrades to displaying an empty/wrong-but-bounds-checked name, never a
crash, and Phase 9's own separate 2D "debug slice" work is where volume
texture debug visibility is planned to land instead.

### 3. `RenderGraphTypes.h` extension

- `VolumeTextureHandle` (same `{index, generation}` POD shape as
  `TextureHandle`/`BufferHandle`).
- `ResourceKind::VolumeTexture` added to the enum (added AFTER the
  if/else audit above, not before).
- `VolumeTextureDesc { width, height, depth, format }` with a purely
  structural `operator==` (no `debugName` field — same rule
  `TextureDesc`/`BufferDesc` already follow).
- `ResourceUsage` gained a `VolumeTextureHandle volumeTexture` field and a
  third static factory, `ResourceUsage::ForVolumeTexture(handle, access)`.
- No new `ResourceAccess` enumerator was added — `ComputeShaderRead`/
  `ComputeShaderWrite`/`ShaderRead` are reused as-is, confirmed already
  generic (keyed purely on `ResourceAccess`, never on `ResourceKind`) in
  both `IsWriteAccess()`/`RequiredStateFor()`.

### 4. `RenderGraphBuilder` extension

- `VolumeTextureImportInfo` (mirrors `TextureImportInfo`'s shape, holding a
  `VolumeTarget`).
- `CompiledGraphInput` gained parallel `volumeTextureDescs`/
  `volumeTextureNames`/`volumeTextureImportInfo` tables.
- `RenderGraphBuilder::ImportVolumeTexture(name, const VolumeTarget&,
  currentLayout)` — takes the plain `VolumeTarget` struct, **never** the
  owning `VolumeTexture&`, exactly mirroring `ImportTexture()`'s own
  `const RenderTarget&` convention.
- `PassBuilder::ReadVolumeTexture()`/`WriteVolumeTexture()` — mirror
  `ReadBuffer()`/`WriteBuffer()`'s exact shape, defaulting to
  `ComputeShaderRead`/`ComputeShaderWrite` respectively.
- **`CreateVolumeTexture()` (a graph-POOLED, transient resource) was
  deliberately NOT implemented** — see "Their Role" answers below.

### 5. `RenderGraphCompiler`/`RenderGraph`/`RenderGraphBarrierPlanner`/`PassContext`

- `RenderGraphCompiler.h`'s `CompiledGraph` gained a parallel
  `volumeTextureLifetimes` vector; `Compile()` now assigns/fills it exactly
  like `textureLifetimes`/`bufferLifetimes`. `Compile()`'s `finalOutputs`
  parameter remains texture-only, unchanged — a `VolumeTextureHandle` can
  never be a root, matching the existing rule for `BufferHandle`.
- `RenderGraphBarrierPlanner::RequiredStateFor()` needed **zero changes** —
  confirmed keyed purely on `ResourceAccess`, verified by re-reading the
  real `.cpp` body directly (a genuine, `default:`-less exhaustive switch
  with no `ResourceKind`/`isDepthResource`-driven branch for
  `ComputeShaderRead`/`ComputeShaderWrite`/`ShaderRead` at all).
- `RenderGraph.h/.cpp` gained: a `PhysicalVolumeTexture` struct (no
  depth-companion state, unlike `PhysicalTexture`'s `colorState`/
  `depthState` split — a volume texture has only one `ResourceState`,
  always targeting `VK_IMAGE_ASPECT_COLOR_BIT`), `EnsureVolumeTextureResolved()`
  (mirrors `EnsureTextureResolved()`'s import branch — every
  `VolumeTextureHandle` today is imported, since `CreateVolumeTexture()`
  doesn't exist yet), and a real third branch inside
  `ApplyUsageBarrierIfNeeded()`'s `switch`.
- `PassContext` gained `resolveVolumeTexture(VolumeTextureHandle) ->
  ResolvedVolumeTexture{ VkImageView view }` — see the deliberate deviation
  from the strategy document's own literal wording, noted below.
- `RenderGraphResourcePool` needed **zero changes** — confirmed: it never
  even sees a volume texture, since every one is imported (never
  `CreateVolumeTexture()`-requested).

### 6. Tier-1 tests (`tests/Renderer/RenderGraph/RenderGraphTypesTests.cpp`)

Added, mirroring the existing `Texture`/`Buffer`-side patterns exactly:
`VolumeTextureHandle` default-invalid/explicit-valid/equality (3 tests),
`VolumeTextureDesc` value-equality (identical + one test per differing
field — width/height/depth/format, 5 tests), and
`ResourceUsage::ForVolumeTexture` sets `kind`/`volumeTexture`/`access`
correctly (1 test) — 10 new tests (RenderGraphTypeTests didn't previously
have a similarly-narrow buffer-vs-volume-texture-only test count to match,
but the shape/count directly mirrors the buffer-side coverage). Same file
already existed — no new test file/CMake registration needed.

### 7. Disposable end-to-end validation (deleted before this report)

A throwaway `src/Shaders/_VolumeProbe.comp` (a `4×4×4 image3D`
`imageStore()` test-pattern writer) plus a temporary block inside
`Application::Run()`'s offscreen-regime `build` lambda: created a real
4×4×4 `VolumeTexture` (`VK_FORMAT_R16G16B16A16_SFLOAT`), imported it via
`ImportVolumeTexture()`, declared a compute pass writing it
(`WriteVolumeTexture(..., ComputeShaderWrite)`, dispatching the probe
shader), and a second compute pass reading it back
(`ReadVolumeTexture(..., ComputeShaderRead)`) that also wrote a tiny
persistent keep-alive `RenderTexture` (added to `outputs`, since a
`VolumeTextureHandle` can never itself be a `finalOutputs` root).

Verified live, with Vulkan validation layers enabled (this build's default —
`Renderer.cpp`'s `kEnableValidation` is `true` whenever `NDEBUG` isn't
defined, true for this configuration):

- Ran the engine (`run_app_background`, stdout/stderr redirected to a log
  file via a throwaway batch wrapper) for ~1500+ frames.
- `GET /list_textures` confirmed `"AtmospherePhase2ProbeKeepAlive"` (4×4,
  `R8G8B8A8_UNORM`) was registered by the render graph's debug-texture
  registry — proof the write pass was NOT culled, the read pass ran, and
  neither threw.
- `GET /get_texture?texture_name=AtmospherePhase2ProbeKeepAlive` (which
  itself calls `Renderer::WaitForGpuIdle()`) succeeded (HTTP 200, a real
  4×4 PNG), exercising a full GPU-idle synchronization point with the
  volume-texture pass's barriers already applied.
- The validation-layer log file was **0 bytes** throughout (the debug
  messenger callback only writes on `WARNING`/`ERROR` severity) — zero
  Vulkan validation errors/warnings, confirming the barrier planner
  correctly synchronizes the `ComputeShaderWrite -> ComputeShaderRead`
  transition on the new resource kind.

`_VolumeProbe.comp`, its `CMakeLists.txt` registration, and the temporary
`Application.cpp` block/includes were all deleted afterward; `gte_core` and
`GreatTamanaEngineTests` were rebuilt clean and the RenderGraph test suite
(149 tests) re-run to confirm the cleanup didn't regress anything. `git
status` confirms no stray files remain (`_reference/`, the build directory,
and the deleted throwaway shader/log are all absent from the diff).

## Deviations from the plan (and why)

1. **`PassContext::resolveVolumeTexture()` returns a plain
   `ResolvedVolumeTexture{ VkImageView view }`, never a `VolumeTexture&`,**
   despite Step 2's own literal wording ("needs a new
   `resolveVolumeTexture(VolumeTextureHandle) -> VolumeTexture&`-shaped
   method"). This is a deliberate, considered correction, not an oversight:
   `RenderGraph` never owns a `VolumeTexture` object at all — every
   `VolumeTextureHandle` today is imported (see Step 3.3's own analysis),
   so `RenderGraph`'s only physical-resource bookkeeping for it is a plain
   `VolumeTarget` (Vulkan handles, no ownership), exactly the same "hand
   back already-resolved Vulkan data, never an owning reference" shape
   `resolveTexture()`/`resolveBuffer()` already use (`ResolvedTexture{view,
   sampler}`/`VkBuffer`, never a `RenderTexture&`/`Buffer&`). A
   `VolumeTexture&` return type would have been impossible to implement
   honestly without `RenderGraph` suddenly owning/aliasing an external
   object it has no lifetime relationship with — the corrected shape keeps
   this file's existing, load-bearing "PassContext hands back plain
   resolved data" convention intact instead of introducing a new,
   inconsistent exception to it.
2. **Two of the four "if/else sites to fix" the precheck listed for
   `RenderGraphCompiler.cpp` needed real switch-ification, but the
   backward-reachability root-marking scan (also named in that same list)
   turned out to need zero code change** — see Section 2 above for the
   full reasoning (it was never an `if/else`, just an `if` with no `else`,
   already correct for any non-Texture kind by construction). Likewise for
   one of the two `RenderGraph.cpp` sites (the color/depth-write scan).
   This is flagged here explicitly per this campaign's own "the real
   source always wins" rule — re-verified directly against the live code,
   not assumed from the strategy document's own count.

No other deviations — every other concrete claim in the strategy document's
own precheck (RenderTarget-only `RenderGraphBuilder.h` dependency,
`RenderGraphResourcePool` needing zero changes, `RenderGraphBarrierPlanner`
needing zero changes, `GpuResourceType` naming) was confirmed accurate
against the real source during implementation.

## What was explicitly NOT done (per Step 4)

- No mip-mapping for `VolumeTexture` — single mip level only.
- No generic "N-dimensional resource" template — `VolumeTexture`/
  `VolumeTextureHandle`/`VolumeTextureDesc` are their own concrete types.
- No `RenderGraphDebugTextureRegistry`/`GET /get_texture` support for
  volume textures — explicitly deferred to Phase 9's own planned 2D
  "debug slice" mirror.
- No `RenderGraphResourcePool` pooling support for volume textures (see
  "Their Role" answer (c) below).
- No changes to `RenderGraphSnapshot.h/.cpp`/the Editor's "Render Graph"
  panel — `ResourceUsageName()`'s existing `if/else` degrades safely
  (empty string, never a crash) for a volume-texture usage; left as an
  accepted, documented gap exactly as the strategy document allowed.

## Build/test verification actually performed

- `cmake -S . -B build` (reconfigure) — succeeded, only the pre-existing,
  unrelated KTX git-describe warning.
- `cmake --build build --target gte_core` — compiled cleanly (both before
  and after the disposable-validation-code cleanup pass).
- `cmake --build build --target GreatTamanaEngineTests` — compiled cleanly
  (both passes).
- `cmake --build build --target GreatTamanaEngine` — compiled and linked
  cleanly (needed to actually run the engine for the live validation step).
- `GreatTamanaEngineTests.exe --gtest_filter=RenderGraph*` — 149/149 passed
  (10 newly added, 139 pre-existing, zero regressions), run again after the
  disposable-code cleanup to confirm nothing broke.
- Live engine run (Editor build, Vulkan validation layers enabled) via
  `run_app_background` + the embedded HTTP server's `/list_textures`/
  `/get_texture` endpoints — see Section 7 above for the full verification
  narrative. Zero validation errors/warnings observed.
- Per this campaign's own workflow rule, **no full clean build and no full
  `ctest` regression run** were performed (reserved for Phase 9 only).

## Step 5 "Their Role" — required answers for Phase 6

**(a) Did `RenderGraphCompiler`'s dependency walk need any changes at all,
and why/why not?**
**Yes.** Confirmed (per the precheck's own corrected finding, re-verified
directly against the live source before writing any code): it is **not**
already generic over `kind` — it used plain `if (usage.kind ==
ResourceKind::Texture) {...} else {...}` two-way branches, never an
exhaustive switch. Three real sites in `RenderGraphCompiler.cpp` (the
RAW-edge writer lookup, the WAW-edge/last-writer update, and the
resource-lifetime `touch()` lambda) were hand-converted to real, exhaustive,
`default:`-less three-way `switch (usage.kind)` statements **before**
`ResourceKind::VolumeTexture` was added to the enum. This was mandatory: had
it been skipped, a volume-texture usage would have silently fallen into the
Buffer path in `RenderGraph.cpp`'s `ApplyUsageBarrierIfNeeded()`, and
`EnsureBufferResolved()` has no bounds check on its own `index` parameter —
a real, reachable out-of-bounds `physicalBuffers` access, not a controlled
failure. (The backward-reachability root-marking scan, also named in the
precheck's four-site list, needed **zero** code change — see Section 2/
Deviation 2 above for why.)

**(b) Did `RenderGraphBarrierPlanner` need any changes beyond "it just
worked because it's keyed on `ResourceAccess` alone"?**
**No — confirmed true, zero changes needed.** `RequiredStateFor()`'s real
body is a genuine, `default:`-less exhaustive `switch (access)` with no
`ResourceKind`/`isDepthResource`-driven branch for `ComputeShaderRead`/
`ComputeShaderWrite`/`ShaderRead` at all — it returns the identical
`ResourceState` (`VK_IMAGE_LAYOUT_GENERAL` + `VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT`
+ the matching read/write access mask) regardless of whether the caller is
a texture, a buffer, or (now) a volume texture. A volume texture reusing
these three `ResourceAccess` values needed zero new logic in this file, and
the live validation run's zero-validation-warnings result is direct
empirical confirmation of this.

**(c) Was `CreateVolumeTexture()` (pooled/transient) implemented or
deliberately deferred?**
**Deliberately deferred — NOT implemented.** Only `ImportVolumeTexture()`
exists. Reasoning: this campaign's one and only volume texture consumer
(Phase 6's aerial-perspective froxel volume) is a FIXED, small, known-at-
startup size that never changes — exactly the same shape
`ComputeBlurValidation`'s own persistent `blurredOutput` `RenderTexture`
already uses (an externally-owned, always-imported resource, never a
graph-pooled/transient one). Building `CreateVolumeTexture()` +
`RenderGraphResourcePool` support for it now would be speculative
infrastructure for a need that may never materialize — per this campaign's
own "don't build it until a real consumer needs it" discipline (see the
strategy document's own Step 4/`AGENTS.md` precedent). **Phase 6 should plan
to create its aerial-perspective volume via `Renderer::CreateVolumeTexture()`
once, persistently (owned by whatever Editor/Application-level object is
appropriate — mirroring `ComputeBlurValidation`'s own ownership shape), and
import it fresh into the render graph every frame via
`RenderGraphBuilder::ImportVolumeTexture()`** — exactly the pattern this
phase's own disposable validation code proved out end-to-end. If a future
phase genuinely needs a pooled/transient volume texture (e.g. more than one
distinct volume texture with varying sizes across frames), extending
`RenderGraphResourcePool`/adding `CreateVolumeTexture()` to
`RenderGraphBuilder` is a well-scoped, isolated follow-up — not something to
build speculatively now.

## Open questions / notes for Phase 3

- Phase 3 (Transmittance LUT) does not depend on anything in this phase at
  all — it is a plain 2D-texture compute pass, following the pre-existing
  `ComputeBlurValidation`/`CreateTexture()` pattern this phase didn't touch.
- Phase 6 (the first REAL consumer of `VolumeTexture`) should read this
  report's "Their Role" answer (c) above before starting — it needs to
  create its own persistent `VolumeTexture` (owned wherever makes sense at
  that point in the campaign, likely a new `AerialPerspectiveVolume`-style
  class mirroring `ComputeBlurValidation`'s shape) and import it fresh each
  frame, never request one via a not-yet-existing `CreateVolumeTexture()`.
- The disposable validation's own `PhysicalVolumeTexture`/
  `EnsureVolumeTextureResolved()` design assumes every `VolumeTextureHandle`
  is imported (`isImported == true`) — its `else` branch (a hypothetical
  transient/pooled volume texture) is defensive-only, untested in practice,
  and will need real exercise/verification the day `CreateVolumeTexture()`
  is finally implemented, if it ever is.
