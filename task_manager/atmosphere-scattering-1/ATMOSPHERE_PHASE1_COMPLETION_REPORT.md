# Atmosphere Phase 1 — Completion Report

**Phase:** `ATMOSPHERE_PHASE1_REFERENCE_ANALYSIS_AND_PHYSICAL_MODEL_FOUNDATIONS_v1.md`
**Branch:** `feature/atmosphere-scattering-impl`
**Status:** Complete — `gte_core` and `GreatTamanaEngineTests` both build cleanly (targeted incremental build only, per this campaign's own workflow rule); all 25 new Tier-1 tests pass.

## What changed

1. **Reference clone + read-through (Step 3.1).**
   - Cloned `github.com/hoffstadt/pl-sky` (`--depth 1`) into `_reference/pl-sky`
     (never committed — added `/_reference/` to `.gitignore`; confirmed via
     `git status` that it shows as untouched/ignored).
   - Read every shader/source file that matters for this campaign:
     `shaders/sky_interop.inc`, `sky.inc`, `sky_transmission_lut.comp`,
     `sky_multiscatter_lut.comp`, `sky_lut.comp`, `sky_aerial_lut.comp`,
     `sky.frag`, `postprocess.comp`, and `src/app.c` (for the real default
     numeric constants/LUT resolutions).
   - Wrote `task_manager/atmosphere-scattering-1/ATMOSPHERE_REFERENCE_NOTES.md`
     — physical constants (with exact citations to `app.c` line numbers),
     density-profile formulas, LUT resolutions, UV parameterizations, phase
     functions, sample counts per pass, and the world-unit/atmosphere-space
     convention. This is the shared reference every later phase (3-7) should
     read alongside its own strategy document.

2. **Shared-GLSL-`#include` verification (Step 3.2).**
   - Confirmed via a throwaway probe (`_IncludeProbe.comp` +
     `_IncludeProbeCommon.glsl`, both deleted before finishing this phase)
     that this project's `glslc` resolves `#include "Foo.glsl"` out of the
     box with zero extra flags, exactly as the strategy document's "if it
     just works" branch anticipated.
   - Extended `cmake/CompileShaders.cmake`'s `gte_add_shader()` with an
     optional `EXTRA_DEPENDS <files...>` keyword-argument list (via
     `cmake_parse_arguments`), so a future shader that `#include`s a shared
     header can list it explicitly and get correct incremental-rebuild
     tracking. Verified this actually works: built the probe shader once (no
     `EXTRA_DEPENDS` initially), reconfigured with `EXTRA_DEPENDS` added,
     rebuilt (no-op, confirming nothing broke), then edited ONLY the shared
     include file and rebuilt again — `ninja` correctly re-ran `glslc` for
     the dependent `.comp` file. Every existing `gte_add_shader()` call site
     is unchanged (the new parameter is purely additive/optional).
   - Created `src/Shaders/AtmosphereCommon.glsl` — an intentionally-empty
     placeholder (header comment only) as the real shared-math include target
     Phase 3 onward will populate.
   - Deleted both `_IncludeProbe*` files and the throwaway `CMakeLists.txt`
     line referencing them before finishing this phase, per Step 4's own
     restriction.

3. **`src/Renderer/Atmosphere/` (Steps 3.3-3.5).**
   - `AtmosphereTypes.h` — `AtmosphereParametersGpu` (96 bytes, six 16-byte
     std140/std430-compatible groups) and `AtmosphereFrameUniforms` (48
     bytes, three groups). Vulkan-header-free, mirrors `GpuTiming.h`'s own
     precedent. Uses `Vec3` directly as each group's vec3 member (rather than
     `GpuSkinningTypes.h`'s per-float-field style) since these structs are
     created/read once per frame/startup, not packed per-vertex — a
     `static_assert(sizeof(Vec3) == 12)` makes that layout assumption
     explicit rather than a silent hope.
   - `AtmosphereParameters.h/.cpp` — `MakeDefaultEarthAtmosphereParameters()`
     (every value transcribed verbatim from the cloned reference, citations
     in `ATMOSPHERE_REFERENCE_NOTES.md`), `AtmosphereRadiusKm()`, and
     `WorldPositionToAtmosphereSpaceKm()` plus the documented
     `kWorldUnitsPerKilometer = 1000.0f` constant (1 world unit = 1 meter,
     matching `PrimitiveMeshGenerator`'s own unit-size primitives).
   - `AtmosphereMath.h/.cpp` — the permanent CPU oracle:
     `RayleighDensityAtHeight()`, `MieDensityAtHeight()`,
     `OzoneDensityAtHeight()`, `ComputeExtinctionCoefficientAtHeight()` (an
     addition beyond the strategy document's own enumerated list, but a
     natural, small piece of the same oracle — combines all three density
     profiles into Beer's-law extinction, needed by the optical-depth
     integral below and reusable by Phase 3+'s own GPU-vs-CPU parity
     checks), `ComputeOpticalDepthToTopOfAtmosphere()`,
     `ComputeTransmittanceToTopOfAtmosphere()`, `RayleighPhaseFunction()`,
     and `CornetteShanksMiePhaseFunction()`. All pure, `noexcept`, zero
     Vulkan/Renderer/ECS dependency.
   - `tests/Renderer/Atmosphere/AtmosphereMathTests.cpp` — 25 real
     GoogleTests (added to `tests/CMakeLists.txt`'s `GTE_TEST_SOURCES`,
     unconditionally — this module has no `GTE_ENABLE_*` dependency).
     Covers: density-profile sea-level normalization + monotonic decay,
     Mie's shorter scale height decaying faster than Rayleigh's at the same
     altitude, the ozone tent's peak/symmetry/zero-edges/degenerate-
     half-width behavior, extinction positivity at sea level and near-zero
     far above the atmosphere, optical-depth degenerate-input handling
     (non-positive sample count, zero-length direction), transmittance
     staying within `[0, 1]` per channel across several height/direction
     combinations, the physically-required "sea-level transmittance <
     high-altitude transmittance" ordering, near-full transparency right at
     the atmosphere's outer boundary, both phase functions' hand-computed
     spot values plus a genuine numerical-integration proof that each
     integrates to ~1.0 over the full sphere (the "sanity check" this
     phase's own strategy document specifically asked for), Mie's forward-
     scattering bias for positive `g`, the default Earth parameters matching
     the cited reference values verbatim, and the world-unit<->kilometer
     conversion helper (including its non-positive-scale degrade-to-zero
     path). All 25 pass.

## Deviations from the plan (and why)

Per Phase 0's own "the real source always wins" rule (Step 5), two concrete
details in the strategy document's own Step 3.3 turned out to disagree with
the ACTUAL cloned reference, and the real source was followed instead:

1. **Ozone absorption profile is a simple single tent, not a Bruneton-style
   two-layer piecewise shape.** The strategy document anticipated "two
   density-layer widths/exp-terms/exp-scales"; the real
   `_reference/pl-sky/shaders/sky.inc`'s `pl_height_factor_ozone()` is just
   `max(0, 1 - abs(height - 25) / 15)` — a single symmetric triangle.
   `OzoneDensityAtHeight()` transcribes this simpler formula exactly
   (`ozoneTentCenterKm`/`ozoneTentHalfWidthKm`), not the more complex shape
   the strategy document guessed at.
2. **The struct stores atmosphere THICKNESS, not an absolute RADIUS.** The
   strategy document named a field `atmosphereRadiusKm`; the real reference
   (and every one of its own ray/UV formulas) consistently works in
   height-above-ground/thickness terms (`atmosphereHeight = 100.0f` in
   `app.c`). `AtmosphereParametersGpu::atmosphereThicknessKm` follows the
   real source, with a small `AtmosphereRadiusKm()` helper
   (`planetRadiusKm + atmosphereThicknessKm`) added in
   `AtmosphereParameters.h` for any call site that genuinely needs the
   absolute radius — this closes the gap without contradicting the real
   reference's own convention.

Both deviations are also documented directly in `AtmosphereTypes.h`'s own
file comment (a "DEVIATION FROM THIS PHASE'S OWN STRATEGY DOCUMENT" note) so
a future reader hits the explanation right next to the code, not only here.

One additional, non-conflicting addition: `ComputeExtinctionCoefficientAtHeight()`
was added to `AtmosphereMath.h` even though it wasn't explicitly named in the
strategy document's own bullet list — it's the natural, small "combine all
three density profiles into Beer's-law extinction" step
`ComputeOpticalDepthToTopOfAtmosphere()` needs internally, and having it as
its own named, tested, public function (rather than an anonymous-namespace
implementation detail) means Phase 3's own GPU-vs-CPU parity work has a
ready-made per-height extinction reference to compare against directly,
instead of re-deriving it from the optical-depth integral.

## What was explicitly NOT done (per Step 4)

- No GLSL math beyond the throwaway include probe (deleted) and the empty
  `AtmosphereCommon.glsl` placeholder — real shader math starts Phase 3.
- No `VkDevice`/Renderer/RenderGraph code of any kind.
- No Multi-Scattering LUT CPU oracle (deliberately deferred to whichever
  phase needs it, per the strategy document's own Step 4).
- `_reference/pl-sky`'s clone contents were not committed — confirmed via
  `git status` showing zero references to `_reference/` in either the
  "Changes not staged" or "Untracked files" sections.

## Build/test verification actually performed

- `cmake -S . -B build` (reconfigure) — succeeded, no new warnings beyond the
  pre-existing, unrelated KTX git-describe warning.
- `cmake --build build --target gte_core` — compiled cleanly (only the two
  new `Atmosphere/*.cpp` translation units needed rebuilding).
- `cmake --build build --target GreatTamanaEngineTests` — compiled cleanly.
- `GreatTamanaEngineTests.exe --gtest_filter=AtmosphereMathTest.*:AtmosphereParametersTest.*`
  — all 25 new tests passed. Per this campaign's own workflow rule, NO full
  build/full `ctest` regression run was performed (that is reserved for
  Phase 9 only).

## Open questions / notes for Phase 2

- Phase 2 (Volume Texture + RenderGraph 3rd resource kind) can proceed
  independently of anything else in this phase — nothing here blocks it.
- Phase 3 (Transmittance LUT) should read `ATMOSPHERE_REFERENCE_NOTES.md`
  Sections 1, 2, 3, and 5 before starting, and should use
  `AtmosphereMath.h`'s `ComputeTransmittanceToTopOfAtmosphere()` directly as
  its own GPU-vs-CPU parity oracle (same 40-sample, forward-marching,
  advance-then-sample numerical method — verified to match the reference's
  own per-step Beer's-law composition mathematically, see
  `ComputeOpticalDepthToTopOfAtmosphere()`'s own doc comment).
- Phase 3 will also need to decide exactly how it handles the ground-hit
  case (`ComputeOpticalDepthToTopOfAtmosphere()` deliberately does NOT zero
  out a ray that geometrically passes through the planet first — see that
  function's own doc comment) — this is flagged there as explicitly
  out of scope for this general-purpose integral, left for the LUT-specific
  phase to resolve the same way the reference's own
  `tIntersection.bHitEarth ? vec3(0) : transmittance` branch does.
- The Revision Notes at the top of this phase's own strategy document (about
  the engine having no real UBO plumbing yet, so these structs will actually
  bind as read-only storage buffers) is still accurate and unchanged by this
  phase's own work — `AtmosphereTypes.h`'s std140/std430 dual-compatible
  padding already accounts for this.
