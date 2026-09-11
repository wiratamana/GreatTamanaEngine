# Aerial Perspective — Technical Notes from `pl-sky`

Source reference: `_reference/pl-sky` (Pilot Light–based prototype implementing
Sébastien Hillaire's *"A Scalable and Production Ready Sky and Atmosphere
Rendering Technique"*, EGSR 2020).

Files inspected:
- `src/app.c` — resource creation, per-frame update, pipeline wiring, UI.
- `shaders/sky_interop.inc` — CPU/GPU shared `plAtmosphereSettings` struct.
- `shaders/sky.inc` — shared atmosphere math (coefficients, phase functions, LUT UV mapping).
- `shaders/sky_transmission_lut.comp` — transmittance LUT (prerequisite input).
- `shaders/sky_aerial_lut.comp` — **the aerial-perspective compute pass itself**.
- `shaders/postprocess.comp` — **the compositing pass that applies the aerial LUT to the final image**.
- `shaders/scene.frag` / `shaders/sky.frag` — geometry/sky shading (aerial perspective is *not* applied per-object; it's a screen-space post pass).

---

## 1. High-level idea

Aerial perspective ("the atmosphere between the camera and everything it
sees") is precomputed once per frame into a small **3D lookup table (LUT)**
called `aerialPerspectiveLut`. Each texel of this volume stores, for a given
screen-space direction (x, y) and a given distance bucket (z / "froxel
slice"), the accumulated in-scattered light (RGB) and the accumulated
transmittance (A) from the camera out to that distance.

At final image composition time, a full-screen post-process compute pass
reconstructs the world-space distance to each pixel (using the depth
buffer, or a far/max distance for sky pixels), converts that distance into a
LUT `z` coordinate, samples the volume once (with bilinear filtering across
all 3 axes) and composites:

```
finalColor = originalSceneColor * aerial.transmittance + aerial.inscatteredLight
```

This is the standard "froxel" aerial-perspective volume technique (as in
Frostbite / Hillaire's paper), decoupled from actual geometry shading — scene
shaders (`scene.frag`) do **not** know about aerial perspective at all; it is
purely a screen-space compositing step.

---

## 2. Data flow / pipeline order (per frame)

1. **Transmittance LUT** (`sky_transmission_lut.comp`) — 2D, ray-marches
   Beer-Lambert extinction from a height/view-angle pair to the atmosphere
   boundary or ground. Output format `R11G11B10_FLOAT` (default res 256×256).
2. **Multiple-scattering LUT** (`sky_multiscatter_lut.comp`) — 2D, approximates
   higher-order scattering contributions (default res 64×64).
3. **Sky-View LUT** (`sky_lut.comp`) — 2D, camera-relative sky radiance for
   the sky background (default res 200×100).
4. **Aerial-Perspective LUT** (`sky_aerial_lut.comp`) — **3D**, ray-marches
   in-scattering + transmittance per froxel column, reusing the transmittance
   & multiple-scattering LUTs and the shadow map (default res 128×128×32,
   shown as 200×100/etc. in some UI screenshots — resolution is user
   configurable).
5. **Shadow pass** — depth-only render from the sun's point of view
   (`tShadowViewProj`), consumed both by scene shading and by the aerial LUT
   pass (sun-visibility per froxel sample).
6. **Forward scene pass** (`scene.frag`) — shades opaque geometry with direct
   sun light + shadow, **without** aerial perspective.
7. **Sky pass** (`sky.frag`) — draws the background sky using the sky-view LUT
   + an analytic sun disk.
8. **Post-process compute pass** (`postprocess.comp`) — reads scene color,
   scene depth, and the aerial LUT; composites aerial perspective on top of
   the already-shaded scene (and sky) image; applies linear→sRGB.

Compute dispatch order in `app.c`'s frame loop:
`shadow pass → aerial LUT pass → forward pass (scene + sky) → postprocess pass`
(aerial LUT is generated *before* the forward pass since it does not depend
on the current frame's color/depth buffer — it only depends on camera,
sun, shadow map, and the two earlier 2D LUTs).

---

## 3. Shared CPU/GPU settings struct (`plAtmosphereSettings`, `sky_interop.inc`)

```c
vec3  scatteringRayleighGround;  float planetRadius;
vec3  extinctionRayleighGround;  float atmosphereHeight;
vec3  ozoneExtinction;           float scatteringMieGround;
float extinctionMieGround;
float mieScatteringExponent;     // g, Cornette-Shanks asymmetry
float g_time;
int   bMultiScatter;

vec4  tSunColorRadius;   // rgb = sun color, a = angular radius (radians)
vec4  tSunDirection;     // xyz = direction TO the sun-lit side (negated dir), w = world-to-atmosphere scale
vec4  tCameraPos;        // world-space camera position

// tAerialInfo:
//   x = maximum aerial distance, in ATMOSPHERE units (km)
//   y = depth-slice distribution exponent
//   z = ray-march samples per slice
//   w = sun intensity / radiance multiplier
vec4  tAerialInfo;

mat4  tCameraProjectionInv;
mat4  tInvViewMatNoTranslation;
mat4  tShadowViewProj;
mat4  tCameraViewInv;
```

Default values set in `app.c` (`pl_app_load`):
- `planetRadius = 6371.0` (km), `atmosphereHeight = 100.0` (km)
- `tSunDirection.w = 0.001` → **world-to-atmosphere scale**: world-space
  units are assumed to be meters; multiplying by 0.001 converts them to the
  kilometers used by the physical atmosphere model. This same scale is
  reused inside the aerial-LUT shader to convert the ray-marched *atmosphere*
  distance back into *world* distance when sampling the shadow map.
- `tAerialInfo = (10.0 km max distance, 2.0 depth exponent, 8.0 samples/slice, 3.0 sun intensity)`
  — matches the UI screenshot ("Max. Distance 10.0", "Depth Exponent 2.0",
  "Samples per Slice 8").
- Aerial LUT resolution default: `128 × 128 × 32` (`tAerialLutResolution`,
  editable in UI as "Aerial LUT Res").

---

## 4. Aerial-perspective LUT generation (`sky_aerial_lut.comp`)

### 4.1 Resources
- **Bind group 0 (global):** `plAtmosphereSettings` UBO, linear-clamp sampler,
  nearest-clamp sampler.
- **Bind group 1:**
  - binding 0: `image3D aerialPerspectiveLut` (write, `rgba16f`)
  - binding 1: `texture2D transmissionLut` (read)
  - binding 2: `texture2D multiscatterLut` (read)
  - binding 3: `texture2D tShadowMap` (read, for per-sample sun shadowing)
- Dispatched with local size `8×8×1`; group counts derived from
  `aerialLutResolution.xy / 8` (the whole Z range is iterated inside the
  shader per invocation, i.e. one thread column ray-marches through *all* Z
  slices).

### 4.2 Per-thread (per froxel column) algorithm
1. Compute `cameraAltitude = max(cameraPos.y * worldToAtmosphereScale, 0)` and
   `viewHeight = planetRadius + cameraAltitude + tinyOffset`. The whole
   computation is done in a camera-centered atmosphere space where the camera
   sits at `(0, viewHeight, 0)` (planet center at some negative Y).
2. Reconstruct the view direction for this froxel column from its (x,y)
   index using the inverse projection (`tCameraProjectionInv`) and the
   inverse view rotation (`tInvViewMatNoTranslation`) — i.e. each froxel
   column corresponds to one screen pixel column/row bucket, matching the
   final composite pass's screen-space UV.
3. Compute `sunDirection`, `viewSunCos`, Rayleigh phase
   (`pl_phase_rayleigh`) and Mie phase (`pl_phase_cornette_shanks`, using
   `mieScatteringExponent` as `g`) — both are view-direction/sun-direction
   dependent and constant across the whole ray for this column (single
   direction assumption, standard froxel simplification).
4. Read `tAerialInfo`: `maxDistance` (x), `depthExponent` (y, clamped ≥ 1),
   `samplesPerSlice` (z, clamped to `[1, 8]` via
   `PL_AERIAL_MAX_SAMPLES_PER_SLICE`).
5. **Loop over every Z slice** (`0 .. volumeSize.z-1`):
   - `distance0`, `distance1` = start/end distance of the slice, computed via
     `pl_aerial_depth(sliceIndex, sliceCount, maxDistance, depthExponent) =
      maxDistance * pow(sliceIndex/sliceCount, depthExponent)`. This is a
     **non-linear (power-law) depth distribution** — with `depthExponent=2`
     slices are packed quadratically denser near the camera, giving more
     resolution to nearby, more perceptually important, atmosphere detail.
   - **Sub-ray-march within the slice** (`samplesPerSlice` sub-steps,
     midpoint rule: `sampleU = (i+0.5)/samplesPerSlice`):
     - `position = cameraPosition + viewDirection * sampleDistance` (camera-
       centered atmosphere space).
     - `currentHeight = length(position) - planetRadius`, clamped to
       `[0, atmosphereHeight]`; if the sample goes **below the planet
       surface** (`currentHeight < 0` before clamping), the column stops
       accumulating further (occluded by the planet).
     - `pl_calculate_coefficients(currentHeight, atmosphere)` — exponential
       height-falloff Rayleigh/Mie scattering & extinction plus a triangular
       ozone-absorption layer (see §6).
     - Look up `atmosphereUV` from height + `dot(localUp, sunDirection)` via
       `pl_compute_lut_uv`, then sample **transmittance-to-sun** from the
       transmittance LUT and **multiple-scattered luminance** from the
       multiscatter LUT (both bilinearly, clamped to texel centers via
       `pl_aerial_clamp_lut_uv`).
     - `planetVisibility` — a second, independent sphere-intersection test
       from the *sample position* toward the sun (`pl_planet_visibility`),
       to catch the sample itself being on the night side of the planet
       (self-shadowing by the planet, distinct from the transmittance LUT
       which only encodes atmosphere/height).
     - **Cascade shadow lookup**: convert the atmosphere-space sample back to
       world space (`worldSamplePosition = cameraPos + viewDir * sampleDistance
       / worldToAtmosphereScale`), project through `tShadowViewProj` +
       `biasMat`, and sample the depth shadow map (`textureProj`) — this is
       how terrain/geometry shadows are baked directly into the volumetric
       aerial-perspective result (light shafts / god rays through terrain).
     - `directSource = shadow * planetVisibility * transmittanceToSun *
       (scatterRayleigh*phaseRayleigh + scatterMie*phaseMie)`
     - `multipleSource = multiscatteredLuminance * (scatterRayleigh+scatterMie)`
       (zeroed out entirely if `bMultiScatter == 0`).
     - `source = sunIntensity * (directSource + multipleSource)`
     - **Analytic segment integration** (`pl_integrate_source`): for a
       constant-in-segment source `L` and extinction `σₜ` over a segment of
       length `Δs`, the standard closed-form scattering integral is used:
       `∫₀^Δs L·e^{-σₜ·s} ds = L·(1-e^{-σₜΔs})/σₜ`, with a linear fallback
       (`L·Δs`) for near-zero extinction to avoid a 0/0 division.
     - Accumulate: `luminance += transmittanceSoFar * segmentLuminance;
       transmittanceSoFar *= exp(-extinction * segmentLength)` — this is
       standard front-to-back volumetric compositing (Beer-Lambert chained
       across segments).
   - After the sub-steps for this slice, store the **running totals** (not
     per-slice deltas) into the 3D image:
     `imageStore(aerialPerspectiveLut, (x,y,slice), vec4(accumulatedLuminance, scalarTransmittance))`
     where `scalarTransmittance = dot(accumulatedTransmittance, 1/sunIntensity)`
     (an approximate scalar reduction of the RGB transmittance, later reused
     directly as the "how much of the background survives" alpha term).
   - Because each slice stores the **cumulative** integral from the camera up
     to that slice's outer boundary, the post-process pass can sample any
     single Z slice directly (no need to walk the volume at composite time).

### 4.3 Notable implementation details / design choices
- Format `rgba16f` (half-float) is enough dynamic range for luminance +
  transmittance.
- The per-slice sub-sampling (`samplesPerSlice`, up to 8) exists because the
  outer slices (at `depthExponent=2`) can still span a fairly long physical
  distance; sub-stepping keeps the extinction integral accurate even though
  the LUT itself is coarse (default 32 slices).
- The whole ray-march happens in a **camera-relative, planet-surface-relative
  space** (camera pinned at local `(0, viewHeight, 0)`), decoupling the
  atmosphere math from world-space precision issues at planetary scale.
- World ↔ atmosphere unit conversion (`tSunDirection.w`) is applied twice:
  once to get `cameraAltitude` from world Y, and once in reverse to project a
  ray-marched atmosphere-space distance back to world space for the shadow
  map lookup.

---

## 5. Compositing the aerial-perspective LUT onto the final image (`postprocess.comp`)

This is a full-screen compute pass (`8×8` local size) that runs **after**
scene + sky rendering:

- Inputs: `tColorImage` (rgba32f, read/write storage image — scene+sky
  already shaded), `tDepthImage` (scene depth, sampled), `aerialPerspectiveLut`
  (3D texture, sampled).
- For each pixel:
  1. Sample scene depth. `hasGeometry = depth > 1e-7`.
  2. **If geometry is present:** reconstruct the view-space and then
     world-space position from `(uv, depth)` via
     `tCameraProjectionInv` → `tCameraViewInv`, and compute
     `rayDistanceWorld = length(worldPosition - cameraPos)`.
  3. **If no geometry (sky pixel):** `rayDistanceWorld = maxAerialDistance /
     worldToAtmosphereScale` — i.e. treat sky pixels as being at the LUT's
     maximum configured distance (so the aerial LUT's far boundary blends
     with what the sky-view LUT already rendered, rather than showing 0
     distance / no aerial effect on the sky).
  4. Convert to atmosphere-space distance: `distanceAtmosphere = rayDistanceWorld
     * worldToAtmosphereScale`.
  5. `normalizedDistance = clamp(distanceAtmosphere / maxDistance, 0, 1)`.
  6. **Invert the power-law depth distribution** used when building the LUT:
     `boundaryU = pow(normalizedDistance, 1/depthExponent)` — this is the
     exact inverse of `pl_aerial_depth`'s `pow(u, depthExponent)`, so sampling
     at `boundaryU` gives the correct slice for this pixel's actual distance.
  7. Bias by half a texel (`aerialZ = boundaryU - 0.5/sliceCount`, clamped to
     texel-center range) because slice `i` in the LUT stores the *cumulative*
     value at the slice's **far** boundary, and volume texture sampling
     samples texel centers — this half-texel correction re-aligns the
     continuous `boundaryU` distance value with the discrete texel grid.
  8. Sample the 3D LUT (`sampler3D`, linear filtering across all 3 axes —
     smooth spatially *and* smooth across distance).
  9. **First-slice fade-in**: `firstSliceBlend = clamp(boundaryU * sliceCount, 0,
     1)`; blend from `(0,0,0,1)` (no scattering added, full transmittance) to
     the sampled aerial value. This avoids a visible seam/pop for very close
     geometry that falls inside the first (nearest) froxel slice, where the
     LUT's own internal precision is otherwise poor.
  10. **Composite:** `finalColor = originalColor * aerial.a + aerial.rgb`
      (`aerial.a` is the *scalar transmittance*, `aerial.rgb` is the
      accumulated in-scattered luminance) — this is the textbook aerial
      perspective blend equation.
  11. Apply `pl_linear_to_srgb` and write back into `tColorImage`.

So: **scene shading is done with zero atmosphere involvement**, and 100% of
the aerial-perspective effect (both the blueish/hazy distance fade and the
attenuation of the scene's own color) is applied as a single post-process
compositing step, driven entirely by the depth buffer and the precomputed 3D
LUT.

---

## 6. Supporting math shared with the aerial pass (`sky.inc`)

- **Height-based density falloff:**
  - Rayleigh: `exp(-height / 8)` (exponential scale height ≈ 8 km)
  - Mie: `exp(-height / 1.2)` (scale height ≈ 1.2 km, much thinner boundary layer)
  - Ozone: triangular layer `max(0, 1 - |height-25| / 15)` (peak absorption
    around 25 km altitude, ±15 km width) — ozone only contributes to
    extinction (absorption), not to scattering.
- **Phase functions:**
  - Rayleigh: `3/(16π) · (1+cos²θ)`
  - Mie: Cornette-Shanks, parameterized by asymmetry `g` ("Mie Scatter
    Asymmetry" in the UI, default `0.76`, strongly forward-scattering).
- **LUT UV convention** (`pl_compute_lut_uv`): `u = height/atmosphereHeight`,
  `v = dot(up, direction)*0.5+0.5` — shared by transmittance and multi-
  scatter LUT lookups, both inside the aerial pass and in `sky.frag`.
- `pl_calculate_coefficients` returns per-height `scatterRayleigh`,
  `scatterMie`, and total `extinction` (Rayleigh+Mie+ozone), consumed
  identically by the transmittance LUT, the multiple-scattering LUT, and the
  aerial-perspective LUT — a single physical model shared across all passes.

---

## 7. Default parameters (matches the UI screenshot)

| Parameter | Default | Meaning |
|---|---|---|
| Atmosphere Thickness | 100.0 km | `atmosphereHeight` |
| Planet Radius | 6371.0 km | Earth-like |
| Rayleigh Scattering / Absorption | (0.0058, 0.0135, 0.0331) | per-km, RGB |
| Ozone Absorption | (0.00065, 0.00188, 0.00008) | per-km, RGB |
| Mie Scattering / Absorption | 0.0060 / 0.00666 | per-km |
| Mie Scatter Asymmetry (g) | 0.76 | forward-scattering lobe |
| Sky LUT Res | 200×100 | 2D |
| Transmission LUT Res | 256×256 | 2D |
| Multiscatter LUT Res | 64×64 | 2D |
| **Aerial LUT — Max. Distance** | 10.0 (km, atmosphere units) | far plane of the aerial volume |
| **Aerial LUT — Depth Exponent** | 2.0 | power-law slice distribution (quadratic) |
| **Aerial LUT — Samples per Slice** | 8 (max 8, `PL_AERIAL_MAX_SAMPLES_PER_SLICE`) | sub-ray-march steps per Z slice |
| Aerial LUT resolution | 128×128×32 (screen-x, screen-y, distance) | 3D, `rgba16f` |
| Sun Intensity | 3.0 | `tAerialInfo.w`, multiplies both direct+multi-scatter source terms and the sun disk in `sky.frag` |
| World→Atmosphere scale | 0.001 | assumes world units = meters, atmosphere model units = km |

Toggles: `Show LUTs` (debug visualize the 2D LUTs on screen), `Show Sky`,
`Multiscatter` (enable/disable the multiple-scattering contribution both in
the sky-view LUT and in the aerial-perspective pass).

---

## 8. Summary of the technique in one paragraph

`pl-sky` implements aerial perspective as a **camera-frustum-aligned 3D
froxel volume** ("volumetric fog"-style LUT) that is regenerated every frame
by ray-marching, for every screen-space (x,y) direction, out to a configurable
max distance subdivided into a small number of non-linearly-spaced Z slices
(power-law, denser near the camera). Each froxel accumulates in-scattered
sunlight (direct single-scattering + optional precomputed multiple-scattering,
both attenuated by transmittance-to-sun LUT lookups, planet self-shadowing,
and the terrain's shadow map) and Beer-Lambert transmittance along the view
ray, using the same height-dependent Rayleigh/Mie/ozone coefficients as the
rest of the atmosphere system. A separate full-screen post-process compute
pass then reconstructs each final pixel's camera-space distance from the
depth buffer (or treats sky pixels as being at the LUT's far plane), inverts
the slice-distribution power law to find the matching LUT depth coordinate,
trilinearly samples the volume, and blends it onto the already-shaded
scene/sky color as `color·transmittance + inscatteredLight`. Because it is a
pure post-process, it requires no changes to per-object shaders and naturally
composites consistently over both opaque geometry and the sky background.
