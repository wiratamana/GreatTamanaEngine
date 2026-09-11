# Why "there is no aerial perspective" — Investigation Findings

**Scope:** compared `pl-sky_aerial_perspective.md` (this same folder — technical
notes on the `_reference/pl-sky` implementation) against this engine's own
real, currently-committed implementation. Files read: `AtmosphereCommon.glsl`,
`AtmosphereAerialPerspectiveVolume.comp`, `AtmosphereAerialPerspectiveComposite.comp`,
`AtmosphereSkyViewLut.comp`, `AtmosphereLutRenderer.h/.cpp`,
`AtmosphereParameters.h/.cpp`, `AtmosphereTypes.h`, `AtmospherePassSequence.h/.cpp`,
`Application.cpp` (the offscreen render-graph build lambda), `ImGuiEditorLayer.cpp`,
`Panels/AtmospherePanel.cpp`, plus the campaign's own `task_manager/atmosphere-scattering-1/`
phase completion reports (Phase 6/7), which turned out to already contain the
answer, first-hand, from the session that originally implemented and tested this.

## Bottom line

**The implementation is not broken. It is wired correctly end-to-end and does
run every frame — the effect is just physically too small to see at the
distances this engine's test scenes normally use.** This was already
discovered and explicitly documented by the original implementing session in
`ATMOSPHERE_PHASE7_COMPLETION_REPORT.md` (see the "Visual verification"
section, quoted below) — it is not a new bug, it is a known, previously-logged
limitation that nobody has revisited since.

> "Aerial perspective's own visible contribution is genuinely subtle at
> typical, near-camera scene scales... A cube/sphere spawned ~5 world units
> (0.005 km) from the camera sits deep in the first froxel slice, where
> transmittance is essentially 1.0 and in-scattering is essentially 0... The
> pass IS demonstrably running and producing sane, non-garbage numeric output
> ... its visual magnitude is simply small at this content scale."

The Phase 6 completion report's own CPU-readback numbers make this concrete —
sampled at the volume's center froxel column:

| Z slice | in-scattering (r,g,b) | transmittance |
|---|---|---|
| 0 (nearest) | (1.79e-7, 7.15e-7, 2.68e-6) | 0.999512 |
| 4 | (3.70e-6, 1.22e-5, 4.59e-5) | 0.995605 |
| 31 (farthest, 10 km) | (3.70e-6, 1.22e-5, 4.59e-5) | 0.995605 |

Even at the volume's *farthest* configured distance (10 km), transmittance
never drops below ~0.9956 and in-scattering never exceeds ~4.6e-5 — both are
essentially invisible once composited into an 8-bit `sceneColor` (`finalColor
= sceneColor * transmittance + inScattering`). At the few-meters range typical
of a test cube/sphere placed a few units from the camera, the numbers are
another 2-3 orders of magnitude smaller still.

## Root cause: a scale mismatch, not a code bug

1. World-unit convention (`AtmosphereParameters.h`): **1 world unit = 1
   meter**, and **1000 world units = 1 km** (`kWorldUnitsPerKilometer`).
2. The aerial-perspective volume's max distance is a **fixed 10 km**
   (`kAerialMaxDistanceKm`, hardcoded in both `AtmosphereAerialPerspectiveVolume.comp`
   and `AtmosphereAerialPerspectiveComposite.comp` — matches `pl-sky`'s own
   default of 10.0 exactly, per the reference notes' §7 table).
3. Real Earth-like Rayleigh/Mie scattering coefficients (~0.0058-0.0331 per
   **kilometer**) genuinely produce a negligible optical depth over anything
   less than several kilometers — this is physically correct (you do not see
   visible haze 5-50 meters away in reality either).
4. Typical engine test content (primitives spawned "in front of the camera",
   default `EditorCamera` at `(0,0,-5)`, etc.) sits **a few world units — a
   few *meters*** from the camera, i.e. ~0.005 km — literally 2000x closer
   than the volume's own far plane, deep inside the first, near-camera froxel
   slice where the ray-marched optical depth is essentially zero.

So the pipeline is doing exactly what the physical model says it should do
for that distance; there is nothing to "turn on" — the haze is there, it's
just too faint at meter-scale distances to be visible without either (a)
much larger scene distances, or (b) deliberately exaggerating the effect
past its physically-accurate value.

## Confirmed correct / faithfully matching the reference

Going through `pl-sky_aerial_perspective.md` section by section against the
real GLSL/C++:

- **Pipeline order** (§2): shared Transmittance+Multi-Scattering LUTs once
  per frame, then per-view Sky-View LUT + Aerial-Perspective volume, then the
  view's own forward/sky draws, then the composite pass — matches exactly
  (`AtmospherePassSequence.cpp`, `Application.cpp`).
- **Per-froxel-column algorithm** (§4.2): camera-centered planet-surface
  frame, quadratic (`depthExponent=2.0`) slice distribution
  (`FroxelSliceToViewDepth`), midpoint sub-stepping within a slice (8
  samples/slice), transmittance-to-sun + multi-scatter LUT lookups, planet
  self-shadowing (`PlanetVisibility`), analytic segment integration
  (`IntegrateInscattering`), Beer-Lambert chaining of transmittance across
  segments, and storing the **cumulative** (not per-slice-delta) totals per
  Z slice — all reproduced correctly. Defaults (`maxDistance=10km`,
  `depthExponent=2.0`, `samplesPerSlice=8`, `rgba16f`) match §7's table
  exactly.
- **Composite pass** (§5): depth reconstruction via inverse view-projection,
  sky pixels forced to the volume's far plane, inverse power-law Z lookup
  (`ViewDepthToFroxelSlice`), trilinear volume sample, and the textbook
  `finalColor = sceneColor * transmittance + inScattering` blend — all
  present and correctly ordered relative to the Game/Scene View draws.
- One genuine (and reasonable) engine-side improvement over the reference:
  `sky_aerial_lut.comp`'s own `pl_get_aerial_view_direction()` reads specific
  diagonal entries of a separate inverse-projection matrix; this engine's
  `FroxelColumnToViewRayDirection()` instead unprojects two NDC points
  through one combined `invViewProjection` — a more general, projection-
  layout-agnostic technique, documented as a deliberate deviation.

## Minor, real (but NOT the cause of "no effect at all") deviations from `pl-sky`

These are worth fixing for polish/precision, but neither one would hide the
effect completely at any distance — flagging them since they came up
side-by-side with the reference doc:

1. **No half-texel Z-bias when sampling the volume in the composite pass.**
   `pl-sky_aerial_perspective.md` §5 point 7 describes biasing the composite
   pass's Z lookup by `-0.5/sliceCount` before sampling, since slice `i`
   stores the cumulative value at the slice's *far* boundary while a 3D
   texture sample lands on texel *centers*. `AtmosphereAerialPerspectiveComposite.comp`
   (line ~92) does `sliceUv = clamp(slice / volumeSize.z, 0, 1)` with no such
   half-texel correction — every composited pixel ends up reading roughly
   half a slice "ahead" of where it should. Harmless in relative terms
   (half of ~0.3km-scale slices near the camera), but a real, fixable
   precision gap.
2. **No "first-slice fade-in" blend.** §5 point 9 describes blending from
   `(0,0,0,1)` (no scattering, full transmittance) up to the sampled aerial
   value across the first slice, specifically to avoid a seam for geometry
   that falls inside the coarse first froxel slice. This engine's composite
   shader samples the volume directly with no such fade — for the vast
   majority of test content (which sits *inside* that first slice, per the
   root cause above) this omission is currently irrelevant because the
   values it would fade between are both already ~zero, but it would become
   visible once scene scale/distance is fixed.
3. **`aerialPerspectiveStrength` (Editor "Atmosphere" panel, default `1.0`,
   slider range `[0, 2]`) cannot compensate for the scale mismatch.** The
   strength multiplies an already-tiny signal (`inScattering *= strength`,
   `transmittance = mix(1, aerial.a, strength)`) — even at the slider's
   maximum (2.0x), a value on the order of `1e-5` in-scattering / `0.995`
   transmittance stays imperceptible. This is a UX gap worth knowing about:
   turning the slider up will not make the effect visible at meter-scale
   test distances; only moving content farther away (or otherwise
   rebalancing the model's distance scale) will.

## Recommendations, if you want the effect to actually be visible

Pick one (or combine):

- **Move the camera/test geometry much farther away** — hundreds of meters
  to kilometers, not a handful of units, so the ray-marched distance actually
  reaches into the volume's mid/far froxel slices where the numbers above
  stop being ~0.
- **Reduce `kAerialMaxDistanceKm`** (currently hardcoded to 10.0 km in both
  `.comp` files) to something much smaller (e.g. 0.05-0.5 km) so a normal,
  meter-scale test scene's distances land well inside the volume's dynamic
  range instead of all being crushed into slice 0. This constant would need
  to change in three places kept in sync by hand today (both shaders' own
  `kAerialMaxDistanceKm` and `AtmosphereLutRenderer.cpp`'s matching
  constant/comment) — there is no single source of truth for it.
- **Deliberately exaggerate the physical coefficients** (Rayleigh/Mie
  scattering/extinction, currently real-Earth values in
  `AtmosphereParameters.cpp`) for a stylized, more visible haze rather than a
  physically-accurate one — the same kind of artistic override many games
  apply on top of Hillaire's model.
- Add real headroom to the `aerialPerspectiveStrength` slider's exposure
  approach (e.g. a `pow()`/logarithmic exaggeration curve instead of a
  linear multiply) so it can meaningfully brighten an otherwise-tiny signal
  rather than just linearly scaling it (linearly scaling `1e-5` by 2x is
  still `2e-5`).

None of the above are "bugs" in the sense of incorrect code — the pipeline,
math, and wiring all faithfully reproduce `pl-sky`'s own technique. The gap
is a **content/scale mismatch**: real-world atmospheric haze genuinely is not
visible over the few-meter distances this engine's current test scenes use,
and the two minor precision gaps noted above (half-texel Z bias, first-slice
fade-in) are pre-existing, previously-undiscovered polish items that would
only start to matter once that scale mismatch is addressed.
