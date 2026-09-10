# Atmosphere Phase 3 — Completion Report

**Phase:** `ATMOSPHERE_PHASE3_TRANSMITTANCE_LUT_v1.md`
**Branch:** `feature/atmosphere-scattering-impl`
**Status:** Complete — `gte_core`, `GreatTamanaEngineTests`, and `GreatTamanaEngine` all build cleanly (targeted incremental build only, per this campaign's own workflow rule); the engine was actually run (Editor build, Vulkan validation layers enabled by default in this configuration) and the Transmittance LUT was captured live via `GET /get_texture?texture_name=AtmosphereTransmittanceLut&format=png` — visually confirmed as a plausible transmittance gradient (see below).

## What changed

### 1. `src/Shaders/AtmosphereCommon.glsl` — populated for the first time

Was an empty placeholder since Phase 1; now contains:

- `struct AtmosphereParametersGpu` — a GLSL mirror of
  `src/Renderer/Atmosphere/AtmosphereTypes.h`'s C++ struct, field-for-field,
  same order (relies on the fact that std140 and std430 produce identical
  padding for this flat, array-free, vec3+float-grouped struct — already
  noted by Phase 1's own file comment).
- `RayleighDensityAtHeight()` / `MieDensityAtHeight()` / `OzoneDensityAtHeight()`
  / `ComputeExtinctionCoefficientAtHeight()` / `AtmosphereRadiusKm()` —
  line-for-line GLSL transcriptions of `AtmosphereMath.cpp`'s C++ functions
  of the exact same name (the CPU oracle, unchanged).
- `ComputeOpticalDepthToTopOfAtmosphere()` / `ComputeTransmittanceToTopOfAtmosphere()`
  — same names, same 40-sample "advance-then-sample" numerical method as
  their CPU counterparts (verified line-by-line against
  `AtmosphereMath.cpp`), so Phase 9's GPU-vs-CPU parity check compares two
  genuinely equivalent implementations.
- Two NEW sphere-intersection helpers with **no CPU-oracle counterpart** (by
  design — ground occlusion is explicitly out of `AtmosphereMath.h`'s own
  scope, per its own doc comment): `DistanceToAtmosphereOuterBoundary()`
  (the far-root helper the optical-depth integral needs internally, mirrors
  `AtmosphereMath.cpp`'s anonymous-namespace `DistanceToSphereBoundary()`)
  and `RayIntersectsSphereNearest()` (a genuinely new near-root helper,
  transcribed from the cloned reference's own
  `pl_ray_intersect_sphere_nearest()`, used by the ground-hit check below).
- `RayleighPhaseFunction()` / `CornetteShanksMiePhaseFunction()` — added now
  (not strictly needed by the Transmittance LUT itself) since they are
  genuinely shared, unconditional math with no LUT-specific shape.
- `HeightZenithToTransmittanceLutUv()` (the inverse — for a future sampler,
  Phase 4 onward) and `TransmittanceLutUvToHeightZenith()` (what this
  phase's own `.comp` file calls) — the Transmittance LUT's specific
  UV↔(height, zenith) parameterization, transcribed from
  `_reference/pl-sky/shaders/sky_transmission_lut.comp`'s own texel decode:
  a plain **linear** height/angle grid (`x = texel/(width-1)`,
  `y = texel/(height-1)`, `upDot = y*2-1` clamped away from exactly `-1`) —
  confirmed NOT the Bruneton asin-based remap, per
  `ATMOSPHERE_REFERENCE_NOTES.md`, Section 3.

### 2. `src/Shaders/AtmosphereTransmittanceLut.comp` — new

`#include "AtmosphereCommon.glsl"`; binding 0 = `AtmosphereParametersGpu` as
a `readonly buffer` (STORAGE buffer, per the corrected guidance), binding 1
= the output `image2D` (`rgba8`). `local_size_x/y = 16` (mirrors
`BoxBlur.comp`'s established convention — 256 is evenly divisible by 16, so
no reason to deviate). Per-texel: decodes `(heightKm, upDot)` via the shared
parameterization, builds a view direction, does the ground-occlusion check
via `RayIntersectsSphereNearest()` against the planet radius (forcing
`vec3(0)` on a hit, mirroring the reference's own
`tIntersection.bHitEarth ? vec3(0) : tTransmittance` branch — this check
deliberately lives HERE, in the LUT-specific shader, not in the shared
`ComputeOpticalDepthToTopOfAtmosphere()`, exactly as Phase 1's own
completion report flagged as an open question for this phase to resolve),
then calls the shared `ComputeTransmittanceToTopOfAtmosphere()` with a fixed
40-sample count (`kTransmittanceLutSampleCount`, matching the reference's
own `PL_SAMPLE_COUNT`) and stores the result with alpha = 1.0.

### 3. `src/Renderer/Atmosphere/AtmosphereLutRenderer.h/.cpp` — new

Modeled directly on `src/Editor/ComputeBlurValidation.h/.cpp`'s shape: owns
a lazily-initialized `ComputePipeline` + `VkDescriptorSetLayout` +
`ComputeDescriptorSet`, a read-only STORAGE `Buffer` for
`AtmosphereParametersGpu` (96 bytes, `BufferMemoryUsage::CpuToGpu`, so
`Upload()` can write into it directly every call — no dirty-flag check, per
Step 4), and a persistent `Texture2D` (256×256,
`allowStorageImageAccess = true`) registered under the literal name
`"AtmosphereTransmittanceLut"`. One public method,
`AddTransmittanceLutPass(RenderGraphBuilder&, Renderer&, const
AtmosphereParametersGpu&) -> TextureHandle` — declared as the single home
every later LUT phase (4/5/6) will extend with one more method, per the
strategy document's own instruction.

**Deviation (an implementation detail, not a design change):**
`RenderGraphBuilder::ImportTexture()` takes a `const RenderTarget&`, but
`Texture2D` (unlike `RenderTexture`) has no `Target()` accessor of its own.
`AddTransmittanceLutPass()` builds a `RenderTarget` by hand from
`Texture2D::Image()`/`View()`/`Width()`/`Height()` plus the class's own
known fixed format, leaving the (irrelevant, since this pass never uses a
depth attachment) depth fields at their default
`VK_NULL_HANDLE`/`VK_FORMAT_UNDEFINED`. This is the first time a
`Texture2D` (rather than a `RenderTexture`) has ever been imported into the
render graph — worth flagging for Phase 4/5's own authors, since they may
hit the same gap.

### 4. Wiring: `src/Application/Application.h/.cpp`

`Application` now owns an `AtmosphereLutRenderer m_atmosphereLutRenderer`
member (declared right after `m_renderGraph`, before `m_editorLayer`, for
the same lifetime reasons `m_renderGraph` itself is placed there). Inside
`Run()`'s existing offscreen-regime build lambda (the same one that already
declares the Game/Scene view passes and GPU-skinning passes), a **temporary
validation call site** now calls
`m_atmosphereLutRenderer.AddTransmittanceLutPass(b, m_renderer,
MakeDefaultEarthAtmosphereParameters())` and pushes the result into that
call's own `outputs` root set — completely independent of whether
Game/Scene are currently visible, so the LUT is recomputed and capturable
every single frame the offscreen regime runs at all. Per the strategy
document's own explicit instruction, this call site carries a
`// TODO(ATMOSPHERE_PHASE7): relocate...` comment and was **deliberately
NOT deleted** after verification (unlike a normal throwaway probe) — Phases
4/5/6 build directly on this pass existing and running every frame.

### 5. `CMakeLists.txt`

- `src/Renderer/Atmosphere/AtmosphereLutRenderer.h/.cpp` added to
  `gte_core`'s unconditional source list (right after `AtmosphereMath.cpp`)
  — **not** gated behind `GTE_ENABLE_EDITOR`, per Locked Design Decision 4
  (atmosphere scattering is an always-compiled core rendering feature, no
  on/off switch of its own — unlike `ComputeBlurValidation`/`BoxBlur.comp`,
  which ARE Editor-gated).
- `gte_add_shader(GreatTamanaEngine src/Shaders/AtmosphereTransmittanceLut.comp
  EXTRA_DEPENDS src/Shaders/AtmosphereCommon.glsl)` — also unconditional,
  for the same reason.

## How the storage-buffer-vs-uniform-buffer question was resolved (for Phase 4/5/6)

Followed the strategy document's own "Revision Notes" exactly, with no
deviation: `AtmosphereParametersGpu` is bound as **binding 0, a read-only
STORAGE buffer** —
`Renderer::CreateBuffer(sizeof(AtmosphereParametersGpu),
VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, BufferMemoryUsage::CpuToGpu, ...)` on
the C++ side, `layout(std430, binding = 0) readonly buffer
AtmosphereParametersBlock { AtmosphereParametersGpu atmosphereParameters; };`
on the GLSL side — **never** `VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER`/GLSL
`uniform`. Confirmed once more, independently, before writing any code: a
fresh grep of `src/` still finds zero `VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER`/
`AddUniformBuffer()`/`ComputeDescriptorWrite::UniformBuffer()` occurrences
anywhere in the engine. **Phase 4/5/6 must follow this exact same
convention for their own per-frame `AtmosphereFrameUniforms` buffer** (Phase
5 onward) — bind it as another read-only storage buffer at whatever binding
number comes next in each shader's own declaration order, never introduce a
real UBO without a fresh, deliberate discussion first (the strategy
document explicitly names this as a valid-but-unbudgeted alternative, not
the default).

One binding-declaration nuance worth flagging for future phases: rather
than declaring the buffer block's members as flat individual fields (the
more common GLSL SSBO style), this phase declares the block as wrapping a
single `AtmosphereParametersGpu`-typed member
(`{ AtmosphereParametersGpu atmosphereParameters; }`). This compiles and
works correctly (confirmed by both a clean `glslc` compile and the live
`/get_texture` capture below) and lets every shared function in
`AtmosphereCommon.glsl` take a plain `AtmosphereParametersGpu` value
parameter, matching the CPU oracle's own function signatures exactly
(`ComputeTransmittanceToTopOfAtmosphere(AtmosphereParametersGpu params,
...)` in GLSL vs. `ComputeTransmittanceToTopOfAtmosphere(const
AtmosphereParametersGpu&, ...)` in C++) — Phase 4/5/6 should reuse this same
one-struct-member block shape for their own bindings of the same buffer,
rather than re-declaring the fields flat and losing that parity.

## Deviations from the plan (and why)

1. **`RenderTarget` built by hand instead of via a `Texture2D::Target()`
   accessor** (Section 3 above) — `Texture2D` genuinely has no such method;
   this is the first call site to import one into the render graph at all.
   Not a design deviation, just an implementation detail worth flagging.
2. **The ground-hit check (`RayIntersectsSphereNearest()`) has no CPU-oracle
   counterpart** — this was flagged as an open question by Phase 1's own
   completion report ("Phase 3 will need to decide exactly how it handles
   the ground-hit case"). Resolved by adding this as a genuinely NEW,
   GLSL-only helper (not mirrored back into `AtmosphereMath.h`, since
   `AtmosphereMath.h`'s own doc comment explicitly scopes ground occlusion
   as "a LUT-shape-specific concern belonging to whichever later phase
   builds the real Transmittance LUT" — i.e., here, not the general-purpose
   CPU oracle). No change to `AtmosphereMath.h`/`AtmosphereMath.cpp` was
   made or needed.
3. **256×256 resolution used, not the "e.g. 256×64" placeholder the
   strategy document's own Step 2 mentioned** — that number was explicitly
   a guess pending the reference notes; `ATMOSPHERE_REFERENCE_NOTES.md`
   (written during Phase 1, i.e. the actual authoritative source for this
   phase) states 256×256 (`tTransmissionLutResolution`), which is what was
   implemented. `local_size_x/y = 16` (matching `BoxBlur.comp`'s existing
   convention) was kept since 256 divides evenly by 16 — no reason to
   deviate to 8×8 (the reference's own local size).
4. No other deviations — every other concrete claim in this phase's own
   strategy document (binding order, `Texture2D`'s fixed format,
   `AtmosphereLutRenderer` being the single home for all LUT passes, no
   dirty-flag/Editor-parameter-editing/GPU-timing work) was followed
   exactly as written.

## What was explicitly NOT done (per Step 4)

- No dirty-flag optimization — the Transmittance LUT (and its 96-byte
  parameters buffer) is recomputed/re-uploaded unconditionally, every
  single frame the offscreen regime runs.
- No Editor parameter editing — `AtmosphereParametersGpu` is still
  `MakeDefaultEarthAtmosphereParameters()`'s fixed output at the one
  temporary call site.
- No GPU timing registration for this pass.
- No new Tier-1 test file — this phase's shader/class work is Tier 2 (no
  live-`VkDevice` test infrastructure exists); `AtmosphereMath.h`'s existing
  25 Tier-1 tests (Phase 1) were not touched and were not re-run as part of
  this specific incremental build (per this campaign's own "fast compile
  check only" workflow rule — the full suite is reserved for Phase 9).

## Build/run verification actually performed

- `cmake --build build --target gte_core` — compiled cleanly (one
  intermediate failure was hit and fixed: `AtmosphereLutRenderer.cpp`
  originally only forward-declared `rg::PassContext` via
  `RenderGraphBuilder.h`/`RenderGraphTypes.h` — needed `#include
  "../RenderGraph/RenderGraph.h"` for the FULL definition, exactly mirroring
  `ComputeBlurValidation.cpp`'s own include list).
- `cmake --build build --target GreatTamanaEngineTests` — compiled cleanly.
- `cmake --build build --target GreatTamanaEngine` — compiled, the new
  `.comp` shader compiled via `glslc` with zero errors, and linked cleanly.
- Ran the engine (`run_app_background`, Editor build, Vulkan validation
  layers enabled by default in this non-`NDEBUG` configuration) and, via the
  embedded HTTP server:
  - `GET /list_textures` confirmed `"AtmosphereTransmittanceLut"`
    (256×256, `R8G8B8A8_UNORM`, regime `"synchronous"`,
    `frames_since_update: 0`) is registered and updating every frame.
  - `GET /get_texture?texture_name=AtmosphereTransmittanceLut&format=png`
    returned a real 256×256 PNG — visually confirmed as a plausible
    transmittance gradient: a black ground-occlusion band (downward-looking
    rays at low altitude correctly hit the planet and report zero
    transmittance), a curved horizon boundary that shifts toward
    more-grazing angles as height increases (physically correct — a higher
    observer can see further below the local horizontal before the ground
    blocks the view), an orange/magenta-tinted grazing-angle band
    (Rayleigh's own wavelength-dependent falloff — blue attenuates faster
    than red at long, near-horizontal path lengths, the same physical
    effect responsible for real-world sunset reddening), and near-white
    (near-1.0-per-channel) transmittance for near-zenith view directions.
  - Stopped the engine (`stop_app_background`) once verification completed.
- Per this campaign's own workflow rule, **no full clean build and no full
  `ctest` regression run** were performed (reserved for Phase 9 only).

## Open questions / notes for Phase 4

- `AtmosphereLutRenderer` is the one class to extend — add a second
  `AddMultiScatteringLutPass(...)` method (or similarly named) alongside
  `AddTransmittanceLutPass()`, reusing this phase's own descriptor-set-
  layout/binding conventions (storage buffer at binding 0, but now ALSO
  reading `AtmosphereTransmittanceLut` as a combined-image-sampler input —
  see `ComputeBlurValidation`'s own `CombinedImageSampler` binding
  precedent for how a compute pass reads another pass's texture output).
- Phase 4/5's own LUTs need an HDR-capable output format —
  `Texture2D`'s fixed `VK_FORMAT_R8G8B8A8_UNORM` will NOT work for them (as
  this phase's own header comments already flag repeatedly). Phase 4 must
  explicitly decide between `RenderTexture` with an explicit float format,
  or extending `Texture2D` with a new `format` parameter, and document
  that choice in its own completion report.
- The "one-struct-member SSBO block" declaration style
  (`{ AtmosphereParametersGpu atmosphereParameters; }`, see above) is worth
  reusing verbatim for `AtmosphereFrameUniforms` once Phase 5 introduces it,
  for the same parity-with-the-CPU-oracle reason.
- The temporary validation call site in `Application.cpp`/`Application.h`
  (`m_atmosphereLutRenderer`, the `TODO(ATMOSPHERE_PHASE7)` comment) is
  still in place and must stay in place through Phases 4/5/6 — only Phase 7
  relocates it into the real, permanent sky background/aerial-perspective
  pass sequence.
