# ATMOSPHERE_PHASE1_REFERENCE_ANALYSIS_AND_PHYSICAL_MODEL_FOUNDATIONS_v1.md

### Child document 1 of 9 — see `ATMOSPHERE_PHASE0_MASTER_STRATEGY_v1.md` for the full campaign map.

> **Revision Notes (double-check pass, 2026-09-10):** every concrete claim
> in this document was cross-checked against the real source and confirmed
> accurate — `cmake/CompileShaders.cmake`'s `gte_add_shader(TARGET SOURCE)`
> really does list only `SOURCE` in its `DEPENDS` (no include-file tracking
> yet, confirming 3.2's plan is necessary), `src/Renderer/GpuSkinning/`
> really does split into `GpuSkinningTypes.h/.cpp` (pure data) +
> `GpuSkinningPipelines.h/.cpp` (live-`VkDevice` orchestration) exactly as
> cited, and `tests/CMakeLists.txt`'s `GTE_TEST_SOURCES` really is a flat
> list of relative paths, so a new `Renderer/Atmosphere/AtmosphereMathTests.cpp`
> entry mirrors existing entries directly. One clarification worth flagging
> here since it affects this phase's own 3.3: `ATMOSPHERE_PHASE3_TRANSMITTANCE_LUT_v1.md`
> (as corrected by this same double-check pass) found that the engine has
> **no real uniform-buffer (`VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER`) descriptor
> support anywhere today** — `AtmosphereParametersGpu`/`AtmosphereFrameUniforms`
> will actually be bound as read-only STORAGE buffers (GLSL `readonly
> buffer`, std430), not true `uniform` blocks, unless Phase 3 deliberately
> adds new UBO plumbing first. Since neither struct contains an array, this
> does not change this phase's own std140-oriented padding guidance below in
> practice (std140 and std430 produce identical padding for a flat,
> array-free struct) — read "std140-layout-compatible" as "std140-and-
> std430-layout-compatible" given this resolution. See Phase 3's own Step 2
> for the full reasoning.

## Step 1: The Goal

Turn "we want atmosphere scattering" into a concrete, written, engine-specific
technical spec by actually reading the reference implementation, then stand
up the small, pure, Vulkan-free C++ foundation every later phase builds on:
physical atmosphere constants (`AtmosphereParametersGpu`), the GPU-shared
per-frame uniform shape (`AtmosphereFrameUniforms`), and a hand-ported CPU
"oracle" for the transmittance/density-profile math (`AtmosphereMath.h/.cpp`)
that Phase 9's validation tool will check the real GPU shader against. Also
verify (or add) shared-GLSL-include support in the shader build, since every
later `.comp`/`.frag` phase depends on being able to `#include` one common
math file instead of copy-pasting it six times.

## Step 2: The Situation

- Nobody has read `github.com/hoffstadt/pl-sky` yet. It implements Sébastien
  Hillaire's 2020 technique: a **Transmittance LUT**, a **Multi-Scattering
  LUT**, a per-frame **Sky-View LUT**, and a camera-frustum-aligned **aerial
  perspective volume** ("camera volume"), composited in that order. This
  document's own Step 3 below names the exact passes/formulas by their
  standard names from the paper so the actual clone can be mapped onto them
  quickly, but the ACTUAL GLSL source, exact LUT resolutions, and exact
  constant values pl-sky uses must be read directly, not assumed from memory.
- `src/Renderer/` has no `Atmosphere/` subfolder yet. The closest existing
  precedent for "a small, self-contained GPU feature module with its own pure
  math + its own GPU types" is `src/Renderer/GpuSkinning/`
  (`GpuSkinningTypes.h/.cpp`, `GpuSkinningPipelines.h/.cpp`) — mirror that
  file-splitting discipline (pure data/math vs. live-`VkDevice` orchestration
  in separate files) here too.
- `cmake/CompileShaders.cmake`'s `gte_add_shader(TARGET SOURCE)` invokes
  `glslc "${SOURCE_ABSOLUTE}" -o "${COMPILED}"` with a `DEPENDS` list
  containing ONLY that one source file (see its own header comment,
  confirmed directly from the file). `glslc` DOES support `#include`
  directives out of the box for a header sitting in the SAME directory as
  the including file (its built-in file includer resolves relative to the
  including file's own path with no extra flag needed) — but this project
  has never actually exercised this, and the existing `DEPENDS` list means
  CMake would not know to recompile a `.comp` file when only its shared
  `#include`d header changed, silently serving a stale `.spv`. This must be
  fixed as part of this phase, not discovered as a confusing "my shader
  change isn't taking effect" bug three phases from now.
- `src/ECS/Components/` has no `Light`/`DirectionalLight` component yet (that
  is Phase 8's job, not this one) — this phase does not touch ECS at all.
- `tests/CMakeLists.txt` already has a `tests/Renderer/` folder (e.g.
  `tests/Renderer/PrimitiveMeshGeneratorTests.cpp`,
  `tests/Renderer/DrawStatsTests.cpp`) — a new `tests/Renderer/Atmosphere/`
  subfolder should mirror that same registration pattern (check exactly how
  an existing Tier-1 test file is added to `GTE_TEST_SOURCES` and copy it).

## Step 3: The Plan

### 3.1 — Clone and read the reference (produces written notes, not shipped code)

- Create `_reference/` at the repo root and add it to `.gitignore` (a single
  new line, `_reference/`) — this folder is a local study aid only, never
  committed, mirroring how `build/` is already ignored.
- `git clone https://github.com/hoffstadt/pl-sky.git _reference/pl-sky` (this
  needs internet access — use `run_shell`/`cmake` with
  `require_internet_connection=true`, or the environment's normal git clone
  flow; this repo is NOT added as a CMake `FetchContent` dependency anywhere,
  per Locked Design Decision 3 in Phase 0).
- Read through its actual shader/source files (their exact names/locations
  will only be known once cloned) looking specifically for: the physical
  atmosphere constants used (planet radius, atmosphere thickness, Rayleigh/
  Mie scattering + absorption coefficients, the ozone "tent function" density
  profile, Mie phase-function asymmetry `g`, ground albedo), the exact LUT
  resolutions chosen for the Transmittance/Multi-Scattering/Sky-View LUTs and
  the camera volume, the UV-to-angle/height parameterization functions for
  each LUT (these are the trickiest part to get bit-for-bit right and MUST be
  transcribed faithfully, not reinvented), the ray-marching sample counts used
  per pass, and exactly how the camera volume's Z axis maps to view-space
  depth (a non-linear slice distribution, not simple linear depth).
- Write `task_manager/atmosphere-scattering-1/ATMOSPHERE_REFERENCE_NOTES.md` —
  a distilled, engine-specific translation: for each of the four passes, list
  its resolution, its inputs/outputs, its exact formula(s) (write the actual
  math, e.g. the optical-depth integral and the density-profile piecewise
  formula, not just prose), and which engine phase (3/4/5/6/7) will implement
  it. This notes file is the shared reference every later phase's own
  document should be read alongside — Phases 3-7 each explicitly point back
  at the relevant section of it. This file IS allowed to be created fresh in
  this phase (it is not one of the 9 `ATMOSPHERE_PHASEn_*` strategy files
  covered by the "do not create new files" double-check rule — it is a
  working-notes artifact this phase itself produces as part of its own
  deliverable).

### 3.2 — Verify (and if needed, fix) shared-GLSL-include support

- Write a throwaway pair of files: `src/Shaders/_IncludeProbeCommon.glsl`
  (e.g. `const float kProbeValue = 42.0;`) and a throwaway
  `src/Shaders/_IncludeProbe.comp` that does
  `#include "_IncludeProbeCommon.glsl"` and uses `kProbeValue` in a trivial
  way (mirrors the compute-shader campaign's own `Passthrough.comp`
  disposable-verification discipline — see
  `COMPUTE_PHASE2_PIPELINE_INFRASTRUCTURE_STRATEGY_v1.md`, Step 3's
  "Throwaway validation shader"). Add it to the build via `gte_add_shader()`
  and confirm `cmake --build build` actually compiles it successfully with
  `glslc` resolving the `#include` correctly.
- **If it just works:** the only remaining gap is incremental-rebuild
  dependency tracking. Extend `gte_add_shader()` in
  `cmake/CompileShaders.cmake` with an optional trailing variadic parameter,
  e.g. `gte_add_shader(TARGET SOURCE)` becomes
  `gte_add_shader(TARGET SOURCE)` plus a NEW second function
  `gte_add_shader_with_depends(TARGET SOURCE EXTRA_DEPENDS...)` (or simply add
  an optional CMake `ARGN`-based extra-depends list to the existing function
  — either is fine, pick whichever keeps every existing `gte_add_shader()`
  call site in `CMakeLists.txt` unchanged) so a `.comp`/`.frag` that
  `#include`s the shared atmosphere math can list it explicitly in its own
  `DEPENDS`, guaranteeing a correct incremental rebuild when the shared header
  changes.
- **If it does NOT work out of the box** (e.g. this project's specific
  `glslc` version/invocation needs an explicit search path), add
  `-I "${CMAKE_SOURCE_DIR}/src/Shaders"` to the `glslc` invocation inside
  `gte_add_shader()` (this is additive — every existing single-file shader
  compile is completely unaffected by an extra, unused `-I` flag) and re-run
  the same throwaway probe until it succeeds. Either way, delete the
  `_IncludeProbe*` files once verified — never ship them, exactly like
  `Passthrough.comp` before it.
- Create `src/Shaders/AtmosphereCommon.glsl` now (empty except for a header
  comment for now — e.g. "populated starting in Phase 3") so every later
  phase's `.comp` file has a real, already-verified-to-compile include target
  from day one.

### 3.3 — `src/Renderer/Atmosphere/AtmosphereTypes.h`

A plain, Vulkan-header-light (only `<cstdint>`/plain `float`/fixed-size
arrays — no `<volk.h>` needed here, mirroring `GpuTiming.h`'s "zero
Vulkan-header dependency" precedent rather than `RenderGraphTypes.h`'s
"Vulkan-header-present-but-call-free" one, since this struct is pure GPU
buffer LAYOUT, not a Vulkan object descriptor) header defining, as plain
`std140`-layout-compatible structs (explicit `float`/`alignas` padding exactly
where GLSL's `std140` rules require it — document each padding field's
purpose in a comment, mirroring how `GpuSkinningTypes.h` already documents
its own buffer layouts):

- `AtmosphereParametersGpu` — every physical constant Phase 1's own reference
  notes identified: `planetRadiusKm`, `atmosphereRadiusKm`,
  `rayleighScattering` (vec3) + `rayleighDensityExpScale`,
  `mieScattering`/`mieAbsorption` (vec3 each) + `mieDensityExpScale` +
  `miePhaseG`, the ozone "tent function" absorption term's piecewise
  constants (linear/constant terms + two density-layer widths/exp-terms/
  exp-scales, matching the reference's own ozone layer shape), and
  `groundAlbedo` (vec3). Every field gets a doc comment naming its physical
  meaning and units (kilometers vs. per-kilometer scattering coefficients) —
  unit consistency here is the single easiest thing to get subtly wrong.
- `AtmosphereFrameUniforms` — the PER-FRAME values Phase 5 onward needs:
  `cameraPositionAtmosphere` (vec3, the camera's position in the SAME
  atmosphere-space kilometer units as the constants above, i.e. height above
  the planet's center, not the engine's raw world-space `Transform.position`
  — the conversion from engine world units to atmosphere-space kilometers is
  a small, explicit, documented scale/offset decided in this phase and reused
  everywhere), `sunDirection` (vec3, normalized, pointing FROM the scene
  TOWARD the sun), `sunIlluminance` (vec3, the sun's color/intensity).
  Forward-declare (in a comment) that Phase 6 will likely add a couple more
  fields (ray-march sample-count knobs) — do not over-build this struct now
  with speculative fields Phase 5 doesn't actually need yet.

### 3.4 — `src/Renderer/Atmosphere/AtmosphereParameters.h/.cpp`

- `AtmosphereParametersGpu MakeDefaultEarthAtmosphereParameters()` — returns
  the struct above populated with the EXACT default Earth-like constants read
  out of the cloned `pl-sky` reference in 3.1 (do not invent your own
  "plausible-looking" numbers — transcribe the real ones, citing which
  reference file they came from in a code comment).
- A small, explicit unit-conversion helper,
  `Vec3 WorldPositionToAtmosphereSpaceKm(Vec3 worldPosition, float worldUnitsPerKm)`
  (or an equally explicit equivalent) — the engine's own world unit scale is
  not otherwise defined anywhere (no existing convention ties 1 world unit to
  a real-world distance), so this phase must pick and document one sensible
  default (e.g. "1 world unit = 1 meter" is the most natural given existing
  primitive sizes — check `PrimitiveMeshGenerator`'s own default cube/sphere
  sizes to sanity-check this choice) and expose it as a single named constant
  future phases/the Editor (Phase 8) can read, never a magic number
  re-typed at each call site.

### 3.5 — `src/Renderer/Atmosphere/AtmosphereMath.h/.cpp` — the permanent CPU oracle

Pure functions, zero Vulkan/Renderer/ECS dependency, each one a direct,
faithful C++ transcription of the corresponding GLSL formula this campaign's
shaders will ALSO implement (Phases 3-6 write the GLSL side; this is the
CPU side, written first and treated as ground truth per Phase 0's own rule):

- `float RayleighDensityAtHeight(const AtmosphereParametersGpu&, float heightKm)`
  and `float MieDensityAtHeight(...)` — the exponential density-falloff
  profile (`exp(-height / scaleHeight)`).
- `float OzoneDensityAtHeight(const AtmosphereParametersGpu&, float heightKm)`
  — the piecewise "tent function" ozone layer.
- `Vec3 ComputeOpticalDepthToTopOfAtmosphere(const AtmosphereParametersGpu&, Vec3 positionKm, Vec3 direction, int sampleCount)`
  — a straightforward numerical (trapezoidal or midpoint — match whichever
  the reference GLSL uses, per 3.1's notes, so Phase 9's parity check is
  comparing the SAME numerical method, not two different ones that merely
  converge to similar answers) integration of the three density profiles
  above along a ray from `positionKm` to the atmosphere's outer boundary.
- `Vec3 ComputeTransmittanceToTopOfAtmosphere(const AtmosphereParametersGpu&, Vec3 positionKm, Vec3 direction, int sampleCount)`
  — `exp(-opticalDepth)`, per-channel. This is the exact value the
  Transmittance LUT (Phase 3) stores per texel, and is what Phase 9's
  validation tool numerically compares the real compute shader's output
  texture against.
- `float RayleighPhaseFunction(float cosTheta)` and
  `float CornetteShanksMiePhaseFunction(float g, float cosTheta)` — the two
  angular scattering phase functions every later pass needs, transcribed
  exactly from the reference's own formulas.
- Every function above gets a real Tier-1 GoogleTest in
  `tests/Renderer/Atmosphere/AtmosphereMathTests.cpp` (add this new file to
  `tests/CMakeLists.txt`'s `GTE_TEST_SOURCES` the same way any other
  `tests/Renderer/*Tests.cpp` file already is): sanity checks against
  hand-derivable properties (transmittance is always in `[0, 1]` per
  channel; transmittance straight up from sea level is smaller than
  transmittance from a high altitude for the same direction; the phase
  functions integrate to a sane total over a sphere for a hand-picked `g`;
  `RayleighDensityAtHeight`/`MieDensityAtHeight` are monotonically decreasing
  with height) — this is the FIRST piece of this whole campaign covered by
  automated tests, and sets the bar every later phase's own Tier-1 additions
  should match.

## Step 4: What We Will NOT Do

- No GLSL code is written yet in this phase beyond the throwaway include
  probe (deleted before this phase ends) and the empty
  `AtmosphereCommon.glsl` placeholder — the real math lands starting Phase 3.
- No `VkDevice`/Renderer/RenderGraph code at all in this phase — everything
  here is pure, Tier-1-testable C++.
- No attempt to implement the Multi-Scattering LUT's spherical-sampling CPU
  oracle here — that formula is meaningfully more expensive/complex (a
  numerical integration over many sample directions) and is deliberately
  deferred to whichever phase needs it (Phase 4/Phase 9 may add a narrower,
  slower CPU reference just for that one LUT if Phase 9 decides it's worth
  the cost — not required by this phase).
- Do not commit the actual `_reference/pl-sky` clone contents to git — confirm
  `.gitignore`'s new `_reference/` line is in place and `git status` shows
  it as ignored before finishing this phase.

## Step 5: Their Role

- `ATMOSPHERE_REFERENCE_NOTES.md` (3.1) is the single most valuable artifact
  this phase produces for every later phase — write it carefully, with real
  formulas and real resolutions, not vague paraphrase. Phases 3-7 are written
  expecting concrete numbers to already be pinned down there.
- The CPU-oracle discipline established in 3.5 (`AtmosphereMath.h` is
  permanent ground truth) is a campaign-wide rule from `ATMOSPHERE_
  PHASE0_MASTER_STRATEGY_v1.md` — do not treat it as "just this phase's own
  nice-to-have tests."
- If the shared-`#include` verification in 3.2 reveals a genuinely different
  problem than either branch above anticipates (e.g. `glslc` isn't on PATH in
  a way that supports this at all), stop and record the exact finding in this
  phase's completion report — do not silently fall back to duplicating shared
  GLSL math across files without flagging it first, since that changes the
  shape of every remaining phase in this campaign.
