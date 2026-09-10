# Atmosphere Reference Notes — distilled from `hoffstadt/pl-sky`

Produced by Phase 1 (`ATMOSPHERE_PHASE1_REFERENCE_ANALYSIS_AND_PHYSICAL_MODEL_FOUNDATIONS_v1.md`,
Step 3.1). Source: `_reference/pl-sky` (cloned locally, `--depth 1`, NOT committed —
see `.gitignore`). This is the shared reference every later phase (3-7) should
read alongside its own strategy document.

Repository layout that matters:
- `shaders/sky_interop.inc` — the `plAtmosphereSettings` CPU/GPU-shared struct
  (the reference's equivalent of our `AtmosphereParametersGpu` +
  `AtmosphereFrameUniforms` combined into one).
- `shaders/sky.inc` — shared math: LUT UV parameterization, phase functions,
  ray/sphere intersection, `pl_calculate_coefficients()` (density profiles ->
  scattering/extinction coefficients).
- `shaders/sky_transmission_lut.comp` — Transmittance LUT (our Phase 3).
- `shaders/sky_multiscatter_lut.comp` — Multi-Scattering LUT (our Phase 4).
- `shaders/sky_lut.comp` — Sky-View LUT (our Phase 5).
- `shaders/sky_aerial_lut.comp` — the aerial-perspective camera volume (our
  Phase 6).
- `shaders/sky.frag` — sky background compositing (our Phase 7, sky part).
- `shaders/postprocess.comp` — aerial-perspective compositing onto opaque
  scene color using scene depth (our Phase 7, composite part) — CONFIRMS
  Locked Design Decision 2 (a full-screen POST-PROCESS COMPUTE pass, not a
  forward-shader edit).
- `src/app.c` — the actual default numeric constants (lines ~202-228) and LUT
  resolutions (lines ~203-206).

## 1. Physical constants (`AtmosphereParametersGpu`, Phase 1's own deliverable)

All heights/radii in **kilometers**, all scattering/extinction/absorption
coefficients in **per kilometer (km⁻¹)** — this matches pl-sky's own units
directly (`planetRadius = 6371.0f` is unmistakably Earth's radius in km, and
`scatteringRayleighGround.x = 0.0058` matches the well-known Rayleigh
scattering coefficient of ~5.8×10⁻³ km⁻¹ for the red channel at sea level).

Read directly from `_reference/pl-sky/src/app.c`:

| Field | Value | Reference line |
|---|---|---|
| `planetRadius` | `6371.0f` km | app.c:211 |
| `atmosphereHeight` (atmosphere thickness above the planet surface) | `100.0f` km | app.c:209 |
| `scatteringRayleighGround` (vec3, Rayleigh scattering coeff. at sea level) | `(0.0058, 0.0135, 0.0331)` km⁻¹ | app.c:212-214 |
| `extinctionRayleighGround` | same as scattering (Rayleigh has ~zero absorption) | app.c:215-217 |
| `ozoneExtinction` (vec3, ozone absorption coeff., Chappuis band) | `(0.00065, 0.00188, 0.00008)` km⁻¹ | app.c:218-220 |
| `scatteringMieGround` (float, grey/wavelength-independent) | `0.006` km⁻¹ | app.c:221 |
| `extinctionMieGround` (float) | `0.00666` km⁻¹ | app.c:222 |
| Mie absorption (derived: extinction − scattering) | `0.00066` km⁻¹ | derived |
| `mieScatteringExponent` (Cornette-Shanks phase `g`) | `0.76` | app.c:223 |
| Ground albedo (used by the multi-scattering LUT's planet-bounce term) | `vec3(0.3)` | sky_multiscatter_lut.comp:107 |
| Sun color | `(1.0, 0.95, 0.85)` | app.c:228 |
| Sun angular radius | `0.00465` rad | app.c:228 (`.w`) |
| Sun intensity/illuminance scale (`tAerialInfo.w`) | `3.0` | app.c:227 |

### Density profiles (`shaders/sky.inc`'s `pl_height_factor_*`, lines 280-295)

```
RayleighDensityAtHeight(h) = exp(-h / 8.0)     // Rayleigh scale height = 8 km
MieDensityAtHeight(h)      = exp(-h / 1.2)     // Mie scale height = 1.2 km
OzoneDensityAtHeight(h)    = max(0, 1 - |h - 25.0| / 15.0)   // tent, center 25km, half-width 15km
```

This is a SIMPLER ozone tent than Bruneton's original two-layer piecewise
model (no separate linear-term/exp-term layers) — pl-sky's own tent is a
single symmetric triangle. Phase 1's own strategy document anticipated a
more complex two-layer Bruneton-style piecewise shape; the ACTUAL cloned
reference uses this simpler one-line tent, so `OzoneDensityAtHeight()` is
transcribed to match the real source, not the strategy document's guess (see
that phase's own "the real source always wins" rule, Phase 0 Step 5).

### Coefficient assembly (`pl_calculate_coefficients()`, sky.inc:297-313)

```
scatterRayleigh(h) = RayleighDensityAtHeight(h) * scatteringRayleighGround        // vec3
scatterMie(h)       = MieDensityAtHeight(h) * scatteringMieGround                  // vec3 (broadcast from float)
extinction(h)       = RayleighDensityAtHeight(h) * extinctionRayleighGround
                    + MieDensityAtHeight(h) * extinctionMieGround   (broadcast)
                    + OzoneDensityAtHeight(h) * ozoneExtinction
```

### Optical depth / transmittance (`shaders/sky_transmission_lut.comp`)

Numerical integration via **Beer's law composed per fixed-length step**
(NOT a single closed-form optical-depth-then-exp — the reference multiplies
per-step transmittance directly), `PL_SAMPLE_COUNT = 40` steps along the ray
from the sample point to the top of the atmosphere (or to the ground, in
which case transmittance is forced to `vec3(0)`):

```
T = vec3(1)
stepLength = pathLength / 40
for i in 0..40:
    pos += V * stepLength
    h = distance(pos, earthCenter) - planetRadius
    coeffs = pl_calculate_coefficients(h)
    T *= exp(-coeffs.extinction * stepLength)
```

`AtmosphereMath.h`'s CPU oracle (`ComputeOpticalDepthToTopOfAtmosphere()` +
`ComputeTransmittanceToTopOfAtmosphere()`) mirrors this exactly: accumulate
`extinction(h_i) * stepLength` per sample (equivalent to the same per-step
Beer's-law composition, since `exp(a)*exp(b) == exp(a+b)`), then take one
final `exp(-accumulatedOpticalDepth)` — mathematically identical to the
shader's per-step multiply, and easier to unit-test as a single optical-depth
value. Phase 9's parity check compares `exp(-CPU optical depth)` against the
GPU's own per-step-accumulated transmittance — same numerical method (40
uniform-length forward samples), so they must agree to within floating-point
tolerance.

## 2. LUT resolutions (`src/app.c` lines 203-206)

| LUT | Resolution | Reference |
|---|---|---|
| Transmittance LUT | 256 × 256 | `tTransmissionLutResolution` |
| Multi-Scattering LUT | 64 × 64 | `tMultiscatterLutResolution` |
| Sky-View LUT | 200 × 100 | `tSkyLutResolution` |
| Aerial Perspective volume | 128 × 128 × 32 | `tAerialLutResolution` |

## 3. UV/angle parameterization (the trickiest part — transcribe faithfully)

- **Transmittance LUT** (`sky_transmission_lut.comp`): `x = texel.x / (width-1)`
  maps LINEARLY to height `[0, atmosphereHeight]`; `y = texel.y / (height-1)`
  maps to `upDot = y*2-1` (dot of view direction with local up), clamped away
  from exactly `-1`. NOT the Bruneton non-linear (asin-based) parameterization
  — pl-sky uses a plain linear height/angle grid for this LUT specifically.
- **Multi-Scattering LUT** (`sky_multiscatter_lut.comp`): same linear
  height/upDot grid as the Transmittance LUT (`x`, `y` in `[0,1)`, NOT
  `(width-1)` denominator here — off-by-one-texel difference vs. the
  Transmittance LUT is present in the real source, not a transcription typo).
- **Sky-View LUT** (`sky.inc`'s `fromSkyLut()`/`pl_to_sky_lut()`): a
  non-linear zenith-angle mapping that allocates more texel density near the
  horizon (`coord = 1 - (1-coord)^2` climbing from zenith to horizon, and
  `sqrt` climbing from horizon to nadir) — this is the well-known
  Hillaire/Bruneton "horizon-biased" Sky-View LUT parameterization. `uv.x` is
  a full 360° azimuth (`fract(0.5 - phi/(2π))`). This LUT is Phase 5's
  concern, not Phase 1's.
- **Aerial Perspective volume Z-slices** (`sky_aerial_lut.comp`'s
  `pl_aerial_depth()`): `distance(slice) = maxDistance * (slice/sliceCount)^depthExponent`,
  with `depthExponent = 2.0` by default (`tAerialInfo.y`) — a quadratic
  (not linear) distance distribution, concentrating slices near the camera.
  This is Phase 6's concern, not Phase 1's.

## 4. Phase functions (`shaders/sky.inc` lines 104-130)

```
RayleighPhaseFunction(cosTheta) = 3 / (16*PI) * (1 + cosTheta^2)

CornetteShanksMiePhaseFunction(g, cosTheta):
    numerator   = 3/(8*PI) * (1 - g^2) * (1 + cosTheta^2)
    denominator = (2 + g^2) * (1 + g^2 - 2*g*cosTheta)^1.5
    return numerator / denominator
```

Both transcribed verbatim into `AtmosphereMath.h`'s
`RayleighPhaseFunction()`/`CornetteShanksMiePhaseFunction()`.

## 5. Sample counts per pass (for Phases 3-6 to match, not this phase)

- Transmittance LUT: 40 fixed samples (`PL_SAMPLE_COUNT`).
- Multi-Scattering LUT: 8×8 = 64 sphere-sampling directions, 20 inner
  ray-march samples per direction (`PL_INNER_SAMPLE_COUNT`).
- Sky-View LUT: variable 32-64 samples (`SKY_VIEW_MIN/MAX_SAMPLE_COUNT`),
  quadratic distance distribution, blended at `t=0.3` within each segment.
- Aerial Perspective volume: up to 8 samples per Z-slice
  (`PL_AERIAL_MAX_SAMPLES_PER_SLICE`), 32 slices.

## 6. World-unit / atmosphere-space conversion

pl-sky itself uses a `tSunDirection.w` field as a "world-to-atmosphere-km"
scale factor (`0.001` in its own demo — i.e. its own world units are meters).
This engine adopts the SAME convention explicitly, matching
`PrimitiveMeshGenerator`'s own unit-size (1×1×1 cube, 0.5-radius sphere)
primitives, which are exactly Unity's own "1 world unit = 1 meter" default:

```
kWorldUnitsPerKilometer = 1000.0f   // 1 world unit = 1 meter
```

`AtmosphereParameters.h`'s `WorldPositionToAtmosphereSpaceKm()` divides a raw
engine world-space position by this constant. It intentionally does NOT
also add `planetRadius`/an up-offset here — composing "how far above the
virtual planet surface is the camera" is `AtmosphereFrameUniforms`'
job (Phase 5), which needs a per-scene notion of "world Y = 0 is the planet
surface" that does not exist yet at this phase.

## 7. What each later phase should read here

- Phase 3 (Transmittance LUT): Sections 1, 2 (256×256), 3 (linear
  height/upDot grid), 5 (40 samples).
- Phase 4 (Multi-Scattering LUT): Sections 1, 2 (64×64), 3, 5 (8×8×20
  samples), plus `sky_multiscatter_lut.comp`'s ground-bounce term (albedo
  0.3, isotropic phase `1/(4*PI)`).
- Phase 5 (Sky-View LUT + `AtmosphereFrameUniforms`): Sections 2 (200×100),
  3 (horizon-biased parameterization — read `fromSkyLut()`/`pl_to_sky_lut()`
  directly, they are non-trivial), 4, 6.
- Phase 6 (Aerial Perspective volume): Sections 2 (128×128×32), 3 (quadratic
  Z-slice distribution, `depthExponent = 2.0`), 5.
- Phase 7 (Sky background + composite passes): `shaders/sky.frag` (sky
  background + Sun disk) and `shaders/postprocess.comp` (aerial composite via
  `imageLoad`/`imageStore` on the scene color, blended as
  `original.rgb * aerial.a + aerial.rgb` — i.e. the aerial volume's alpha
  channel carries scalar transmittance, and its RGB carries already-scaled
  in-scattered luminance).
