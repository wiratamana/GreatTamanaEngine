# ATMOSPHERE_PHASE4_MULTISCATTERING_LUT_v1.md

### Child document 4 of 9 — see `ATMOSPHERE_PHASE0_MASTER_STRATEGY_v1.md` for the full campaign map.
### Depends on: Phase 3's `AtmosphereLutRenderer` + `"AtmosphereTransmittanceLut"` texture existing and being visually correct.

## Step 1: The Goal

Add the second permanent atmosphere compute pass: the **Multi-Scattering
LUT** — a small 2D texture (per `ATMOSPHERE_REFERENCE_NOTES.md`, typically
much smaller than the Transmittance LUT, e.g. 32×32) approximating the
contribution of light that scatters MORE THAN ONCE before reaching the eye —
single-scattering alone (what the Transmittance LUT alone would give you)
noticeably under-brightens a real sky, especially near the horizon and on
the night/dark side of objects. This is the LUT the Sky-View LUT (Phase 5)
and the aerial-perspective volume (Phase 6) both add on top of their own
single-scattering integration.

## Step 2: The Situation

- This is meaningfully more mathematically involved than the Transmittance
  LUT: the standard technique (confirm the EXACT approach `pl-sky` itself
  uses, per Phase 1's reference notes) numerically estimates multiple
  scattering by shooting a fixed number of sample directions (e.g. 64) over a
  sphere around a point at a given height/sun-zenith-angle, ray-marching each
  one while accumulating single-scattered radiance AND transmittance
  (sampling the Transmittance LUT — Phase 3's output — instead of
  re-integrating optical depth from scratch, which is exactly why Phase 3
  had to exist first), then solving a geometric-series closed form
  (`L_total = L_2ndOrderScattering / (1 - f_ms)`, where `f_ms` is the mean
  scattered fraction) to approximate the infinite sum of all higher-order
  bounces from just the 2nd-order estimate.
- This pass is the first one in the campaign that SAMPLES another pass's
  texture output from within a compute shader (`sampler2D` bound read-only,
  exactly like `BoxBlur.comp`'s own `sourceTexture` binding) — the render
  graph must therefore declare a real read-dependency on
  `"AtmosphereTransmittanceLut"` (`ReadTexture(transmittanceLutHandle,
  ResourceAccess::ComputeShaderRead)` or `ShaderRead` — confirm from
  `RenderGraphTypes.h` which access value a compute shader's `sampler2D`
  (as opposed to `image2D`) read should use; `BoxBlur.comp`'s own
  `sourceTexture` binding is the exact same case and already answers this),
  so `RenderGraphCompiler`'s dependency ordering runs this pass strictly
  after Phase 3's.
- Like the Transmittance LUT, this LUT only depends on
  `AtmosphereParametersGpu` (not on camera/sun direction) — it, too, can
  safely recompute unconditionally every frame for this campaign's scope
  (see Phase 3's own "What We Will NOT Do" on dirty-flag optimization,
  unchanged here).

## Step 3: The Plan

- Extend `src/Shaders/AtmosphereCommon.glsl` with the shared spherical-sample
  helper(s) this LUT needs (e.g. a fixed, deterministic direction-sampling
  pattern over a sphere/hemisphere — transcribe the exact sample count and
  pattern `pl-sky` uses, per the reference notes, rather than inventing a
  different one) and the Multi-Scattering LUT's own UV<->(height,
  sunZenithAngle) parameterization functions (simpler than the Transmittance
  LUT's — usually near-linear, per most public references, but confirm
  against the actual cloned source).
- `src/Shaders/AtmosphereMultiScatteringLut.comp` — binding 0:
  `AtmosphereParametersGpu` uniform buffer, binding 1: `sampler2D
  transmittanceLut` (read-only), binding 2: `image2D destinationImage`
  (write-only) — following `DescriptorSetLayoutBuilder`'s documented
  ordering convention exactly (uniform/buffer bindings, then read-only
  textures, then storage images last). For each texel: decode
  `(height, sunZenithAngle)`, ray-march the fixed sample-direction set,
  sampling `transmittanceLut` for each step's transmittance instead of
  re-deriving it, accumulate 2nd-order scattering + the multi-scattering
  "response" term, solve the geometric series, `imageStore()` the result.
- `AtmosphereLutRenderer` gains a second method,
  `TextureHandle AddMultiScatteringLutPass(RenderGraphBuilder&, Renderer&,
  const AtmosphereParametersGpu&, TextureHandle transmittanceLutHandle)` —
  same lazily-initialized-pipeline/descriptor-set/output-texture shape as
  Phase 3's method, reusing the SAME `AtmosphereParametersGpu` uniform
  buffer Phase 3 already created (do not create a second, duplicate uniform
  buffer for the same data — thread the existing one through).
- Register the output under the literal name
  `"AtmosphereMultiScatteringLut"`.
- Extend this phase's own temporary validation call site (from Phase 3,
  still marked `// TODO(ATMOSPHERE_PHASE7): relocate...`) to also call
  `AddMultiScatteringLutPass()` right after the Transmittance pass, and
  capture it once via `/get_texture?texture_name=AtmosphereMultiScatteringLut`
  to visually sanity-check (expect a much smoother, lower-frequency
  gradient than the Transmittance LUT — no sharp horizon banding, since
  multiple scattering is a diffuse, low-frequency phenomenon by nature; a
  result that LOOKS as sharp/banded as the Transmittance LUT itself is a
  strong signal something is wrong, e.g. accidentally sampling the wrong
  LUT or getting the UV parameterization swapped).

## Step 4: What We Will NOT Do

- No attempt to build a CPU oracle for this specific LUT (see Phase 1's own
  explicit scope note deferring this) — Phase 9's validation for THIS LUT is
  visual/plausibility-based (the smoothness check above, plus an energy
  sanity check: multi-scattering LUT texels should never make total sky
  brightness look implausibly higher than single-scattering alone, a
  order-of-magnitude sanity check, not a bit-exact one) rather than a
  pixel-exact numeric comparison.
- No configurable sample-count/quality knob exposed to the Editor — the
  sample count is a fixed, hardcoded constant matching the reference,
  documented in a shader comment, not a runtime-tunable parameter.

## Step 5: Their Role

- Do not skip the visual sanity capture — this LUT has no simple CPU oracle
  to lean on, so eyeballing the captured texture via `/get_texture` really is
  this phase's primary correctness signal; take it seriously, and compare
  side-by-side against any reference screenshot found while reading
  `pl-sky`'s own repository (its README/screenshots, if any) during Phase 1.
- Confirm `RenderGraphCompiler` genuinely orders this pass after Phase 3's
  (e.g. by temporarily reordering the two `AddXLutPass()` calls in the wrong
  order and confirming the compiler/validation layers actually complain,
  then putting the correct order back) — this is cheap, concrete proof the
  dependency declaration is doing real work, not simply "happening to run in
  written order" by coincidence.
