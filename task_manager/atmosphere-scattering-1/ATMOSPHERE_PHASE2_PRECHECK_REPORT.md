# ATMOSPHERE_PHASE2 Pre-Implementation Technical Precheck Report

**Scope:** a focused, documentation-only cross-check of
`ATMOSPHERE_PHASE2_VOLUME_TEXTURE_RENDERGRAPH_SUPPORT_v1.md` against the
ACTUAL current engine source (not the document's own paraphrase of it),
before this phase goes into implementation. No engine source code was
modified. Branch stayed on `feature/atmosphere-scattering-impl` throughout,
per instructions.

## What was read

- `README.md`, `AGENTS.md` (full).
- `ATMOSPHERE_PHASE0_MASTER_STRATEGY_v1.md` (full, orchestrator document).
- `ATMOSPHERE_PHASE2_VOLUME_TEXTURE_RENDERGRAPH_SUPPORT_v1.md` (full, the
  document under review).
- Real source, in full: `src/Renderer/Texture2D.h/.cpp`,
  `src/Renderer/RenderTarget.h`,
  `src/Renderer/RenderGraph/RenderGraphTypes.h/.cpp`,
  `src/Renderer/RenderGraph/RenderGraphBuilder.h/.cpp`,
  `src/Renderer/RenderGraph/RenderGraphCompiler.h/.cpp`,
  `src/Renderer/RenderGraph/RenderGraphResourcePool.h/.cpp`,
  `src/Renderer/RenderGraph/RenderGraphBarrierPlanner.h/.cpp`,
  `src/Renderer/RenderGraph/RenderGraph.h/.cpp`,
  `src/Renderer/RenderGraph/RenderGraphDebugTextureRegistry.h`,
  `src/Renderer/RenderGraph/RenderGraphSnapshot.cpp` (partial, the relevant
  function), `src/Editor/ComputeBlurValidation.h/.cpp`,
  `src/Shaders/BoxBlur.comp`, `src/Renderer/Vulkan/FormatCapabilities.h`,
  `src/Renderer/Memory/GpuMemoryTracker.h`, `src/Renderer/GpuResourceFactory.cpp`
  (relevant sections), `src/Renderer/RenderTexture.cpp` (relevant section),
  `tests/Renderer/RenderGraph/RenderGraphTypesTests.cpp`.
- `search_in_dir` sweeps for `ResourceKind::` and `ResourceKind` across
  `src/Renderer/RenderGraph/` and `src/` to find every real branch point,
  and for `SupportsStorageImageUsage`/`VK_IMAGE_TYPE` to check for hidden
  3D-image obstacles (none found).

## Verdict

The document was **mostly accurate** — nearly every concrete claim about
`ResourceKind`/`ResourceUsage`/`TextureDesc`/handle shapes/the compute-pass
precedent/the resource-pool behavior/the barrier planner's genericity
checked out exactly as written. However, one specific, load-bearing
technical assumption was **factually wrong** and has been corrected in
place in the document (search for "CORRECTED"/"CORRECTION"/"CLARIFICATION"
markers), plus two smaller naming/signature fixes. Nothing required
touching any other `ATMOSPHERE_PHASEn_*.md` file.

## Findings and corrections made

### 1. (Major, corrected) `RenderGraphCompiler`/`RenderGraph` are NOT generic over `ResourceKind` — no exhaustive-switch safety net exists

The original document's Step 2 assumed `RenderGraphCompiler`'s (and, by
implication, `RenderGraph`'s) `kind`-branching logic was an *exhaustive
`switch(ResourceKind)`* that would **fail to compile** the instant
`ResourceKind::VolumeTexture` was added — framed as a free, automatic
safety net that "tells you exactly where to add the new case."

Reading the real source disproves this. Every place that branches on
`ResourceUsage::kind` today is a plain, **two-way `if (usage.kind ==
ResourceKind::Texture) { ... } else { ... }`** — never a `switch` at all:

- `RenderGraphCompiler.cpp`: the RAW-edge writer lookup, the WAW-edge/
  last-writer update, the backward-reachability root-marking scan (`usage.kind
  == ResourceKind::Texture && ContainsTextureHandle(finalOutputs,
  usage.texture)`), and the resource-lifetime `touch()` lambda — four
  distinct `if/else`/ternary sites in one file.
- `RenderGraph.cpp`: `ApplyUsageBarrierIfNeeded()`'s top-level branch, and
  `ExecuteCompiledGraph()`'s color/depth-attachment-write scan
  (`if (usage.kind != ResourceKind::Texture) { continue; }`).
- `RenderGraphSnapshot.cpp`'s `ResourceUsageName()` (Editor "Render Graph"
  panel display only — bounds-checked, degrades to an empty string, lower
  risk, and already an accepted out-of-scope gap per the document's own
  Step 4).

This means adding `ResourceKind::VolumeTexture` to the enum will compile
**perfectly cleanly** everywhere — with **zero** compiler errors pointing
at any of these sites. Left unfixed, every one of them silently treats a
volume-texture usage as a *buffer* usage instead (the `else` branch catches
"anything that isn't `Texture`"). Per the document's own plan,
`ResourceUsage::ForVolumeTexture(...)` leaves `ResourceUsage::buffer` at its
default `BufferHandle{}` (`index == kInvalidIndex == 0xFFFFFFFF`). In
`RenderGraphCompiler.cpp` every affected lookup happens to be bounds-checked,
so the practical effect there is merely "a volume-texture pass silently gets
no dependency edges and is permanently culled" (bad, but not a crash). In
`RenderGraph.cpp`, however, `EnsureBufferResolved(std::uint32_t index, ...)`
indexes `physicalBuffers[index]` **with no bounds check at all** — this is a
real, reachable **out-of-bounds `std::vector` access** (undefined behavior)
the moment a real pass declares a volume-texture read/write against this
unfixed code.

**Fix applied to the document:** Step 2's write-up now states the correct
finding (if/else, not switch; no compile-time safety net; concrete crash
risk quoted), Step 3.2 now lists the exact confirmed file/site locations and
mandates fixing them to a genuine three-way branch (or, preferably, an
exhaustive `switch` with no `default:`, matching `IsWriteAccess()`'s own
convention so a future fourth kind gets real protection) **before** the new
enumerator is added, Step 3.4 repeats the corrected guidance at the
`RenderGraphCompiler`/`RenderGraph` bullets, and Step 5's "must explicitly
answer in writing" callout now states the corrected answer for question (a)
directly rather than leaving it open.

### 2. (Minor, corrected) Wrong enum name: `GpuResourceKind` → `GpuResourceType`

The document referred to `GpuResourceKind::Texture` in 3.1 when discussing
`GpuMemoryTracker` registration. The real enum
(`src/Renderer/Memory/GpuMemoryTracker.h`) is `GpuResourceType` (values
`Buffer`/`Texture`) — `GpuResourceKind` does not exist anywhere in this
codebase. Corrected both occurrences; confirmed the enum genuinely has no
2D-vs-3D-specific behavior, so reusing `GpuResourceType::Texture` for a
`VolumeTexture`'s own `Track()` call is still the right choice.

### 3. (Real gap, corrected) `ImportVolumeTexture()`'s proposed signature didn't match the real `ImportTexture()` convention

The document proposed `ImportVolumeTexture(const char* name, VolumeTexture&
volumeTexture, VkImageLayout currentLayout)`, describing it as mirroring
`ImportTexture()`. The REAL `ImportTexture()` signature is
`ImportTexture(const char* name, const RenderTarget& externalTarget,
VkImageLayout currentLayout)` — it takes a small, plain, non-owning
`RenderTarget` struct (just Vulkan handles: image/view/extent/format, see
`RenderTarget.h`), **never** the owning `RenderTexture`/`Texture2D` object
directly. This is a deliberate layering choice: `RenderGraphBuilder.h`
`#include`s only `RenderTarget.h`, never `RenderTexture.h`.
`ComputeBlurValidation::AddPass()` confirms this in production code —
`builder.ImportTexture("BlurredSceneOutput", m_blurredOutput->Target(), ...)`
passes `.Target()`, not `*m_blurredOutput`.

Had the document's original wording been implemented literally,
`RenderGraphBuilder.h` would have needed to `#include "../VolumeTexture.h"`
(pulling in `GpuMemoryTracker.h`/`VulkanAllocator.h` transitively) — a real,
avoidable architectural regression a future implementer would likely not
have noticed until it was already done. **Fix applied:** Step 2 and Step 3.3
now specify a new, plain `VolumeTarget` struct (mirroring `RenderTarget`'s
own shape) plus a `VolumeTexture::Target()` accessor, with
`ImportVolumeTexture()` taking `const VolumeTarget&`, exactly preserving the
existing layering discipline.

### 4. (Clarification added) Where the storage-format capability check belongs

The document's 3.1 said the `VolumeTexture` class should "confirm... via
`FormatCapabilities.h`... throw loudly if not" without being fully explicit
about which layer (constructor vs. factory) performs this. Checked directly:
`Texture2D`'s constructor explicitly does NOT perform this check itself
(its own `.cpp` comment says "this constructor unconditionally trusts that
check already happened") — the real call site is
`GpuResourceFactory::CreateTexture2D()`/`CreateRenderTexture()`, which
throws BEFORE constructing the RAII object. Added an explicit clarification
that `VolumeTexture`'s constructor must follow this same "constructor
trusts, factory checks" convention (the check belongs in
`GpuResourceFactory::CreateVolumeTexture()`), even though — unlike
`Texture2D`/`RenderTexture`'s *optional* storage flag — a `VolumeTexture`
always needs storage access unconditionally.

## Items verified as accurate, no change needed

- Every claim about `ResourceKind`'s two current values, `ResourceUsage`'s
  tagged-struct shape and its `ForTexture()`/`ForBuffer()` factories,
  `TextureDesc`'s exact fields (no 3rd-dimension concept), and the
  `TextureHandle`/`BufferHandle`/`PassHandle` `{index, generation}` shape —
  all confirmed field-for-field against `RenderGraphTypes.h`.
- `ResourceAccess`'s `IsWriteAccess()`/`ToString()` (`RenderGraphTypes.cpp`)
  and `RenderGraphBarrierPlanner::RequiredStateFor()` are genuinely
  exhaustive, deliberately-`default`-less `switch(ResourceAccess)`
  functions with **zero** `ResourceKind`/dimensionality branching — reusing
  `ComputeShaderRead`/`ComputeShaderWrite`/`ShaderRead` for a volume texture
  needs no new `ResourceAccess` enumerator and no barrier-planner changes at
  all. This is the one place the document's "exhaustive switch = safety net"
  framing was actually correct, which is exactly why the corrected document
  now draws an explicit contrast between this file (genuinely generic) and
  `RenderGraphCompiler.cpp`/`RenderGraph.cpp` (not generic at all).
- `RenderGraphResourcePool`'s `AcquireTexture()`/`AcquireBuffer()`/
  `BeginFrame()` behavior, and the claim that an imported resource never
  touches the pool at all — confirmed exactly as described.
- `RenderGraphDebugTextureRegistry`/`DebugTextureSnapshot` being genuinely
  2D/`RenderTarget`-only with no 3D concept — confirmed.
- `PassContext`'s `resolveTexture`/`resolveBuffer` being plain
  `std::function` fields (not virtual methods), fully defined in
  `RenderGraph.h`, wired up per-pass inside `ExecuteCompiledGraph()` —
  confirmed; a `resolveVolumeTexture` field follows the identical shape.
- `ComputeBlurValidation`/`BoxBlur.comp`'s shape as the proven end-to-end
  compute-pass precedent — confirmed accurate in every particular checked
  (lazily-initialized pipeline/descriptor set/output texture, `AddPass()`
  returning a handle, `FinalizeForSampling()` for external-sampling
  transitions, binding-convention comments matching the real `.comp` file).
- `Texture2D`'s constructor/format-capability pattern generalizing cleanly
  to `VK_IMAGE_TYPE_3D`/`VK_IMAGE_VIEW_TYPE_3D` — confirmed no obstacle in
  `Texture2D.cpp` or the Vulkan wrapper layer (`search_in_dir` for
  `VK_IMAGE_TYPE` across `src/Renderer/Vulkan/` found no hardcoded
  2D-only assumption to work around); `SupportsStorageImageUsage()`'s own
  signature (`VkPhysicalDevice`, `VkFormat`) is dimensionality-agnostic,
  since `VkFormatProperties` are format-wide, not per-image-type.
- `tests/Renderer/RenderGraph/RenderGraphTypesTests.cpp`'s existing
  `Buffer`-side test pattern (handle validity/equality, desc value-equality,
  `ForBuffer()` factory shape) is directly mirrorable for the new
  `VolumeTexture` types, exactly as the document assumed.

## What changed in the repository

- `task_manager/atmosphere-scattering-1/ATMOSPHERE_PHASE2_VOLUME_TEXTURE_RENDERGRAPH_SUPPORT_v1.md`
  — overwritten in place (same filename, no `_v2`) with the corrections
  above integrated inline (marked "CORRECTED"/"CORRECTION"/"CLARIFICATION"),
  keeping its original Step 1–5 structure and level of technical detail. A
  short callout was added right under the document header summarizing the
  precheck and pointing at this report.
- `task_manager/atmosphere-scattering-1/ATMOSPHERE_PHASE2_PRECHECK_REPORT.md`
  — this new report.
- No other `ATMOSPHERE_PHASEn_*.md` file was touched.
- No file under `src/` or `tests/` was modified — this was a
  documentation-only review pass, per the task's instructions.

## Recommendation for the implementing session

Treat item 1 above as a hard prerequisite, exactly as the corrected document
now states in its Step 5: fix the three files' `if/else` sites to a real
three-way branch FIRST, confirm no site was missed (e.g. temporarily grep
for every `ResourceKind::Texture`/`ResourceKind::Buffer` occurrence again
after the fix and manually check each), and only then add
`ResourceKind::VolumeTexture` to the enum and build out the rest of this
phase's plan. This ordering is what prevents the reachable out-of-bounds
`std::vector` access in `RenderGraph::EnsureBufferResolved()` identified
above from ever becoming live, even transiently, during implementation.
