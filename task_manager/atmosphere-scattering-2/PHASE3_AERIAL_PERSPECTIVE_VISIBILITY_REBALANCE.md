# PHASE3 — Aerial Perspective Visibility Rebalance (the actual bug fix)

Parent: `PHASE0_MASTER_STRATEGY.md`. Depends on Phase 1 (the tunables must
already exist and be threaded through) and Phase 2 (the composite pass must
already sample/blend correctly before its magnitude is increased). Read both
phases' own `PHASE1_COMPLETION_REPORT.md`/`PHASE2_COMPLETION_REPORT.md`
first.

**This is the phase that actually fixes "I can't see the blueish fog on far
distance objects."** Phases 1-2 were prerequisite plumbing/precision work;
this phase changes real, shipped numbers and adds one new piece of real
math.

## Step 1 — The Goal (Where are we going?)

1. Ship new DEFAULT values for `AtmosphereSettings::aerialPerspectiveMaxDistanceKm`/
   `aerialPerspectiveDepthExponent` that put typical engine test-scene
   content (roughly tens to a few hundred meters from the camera) usefully
   inside the froxel volume's own dynamic range, instead of squashed into
   the very first, near-zero-optical-depth slice.
2. Add a new, physically-motivated **scattering exaggeration** multiplier —
   `AtmosphereSettings::aerialPerspectiveScatteringExaggeration` (already
   declared as a real field in Phase 1, unused until now) — applied strictly
   LOCALLY inside `AtmosphereAerialPerspectiveVolume.comp`'s own per-sample
   ray-march math, that uniformly scales BOTH the local scattering
   coefficients AND the local extinction coefficient together (i.e. "turn up
   the fog density", exactly the standard artistic override many games apply
   on top of a physically-based atmosphere model — see
   `AERIAL_PERSPECTIVE_INVESTIGATION_FINDINGS.md`'s own "Recommendations"
   list, third bullet). Scaling scattering and extinction by the SAME factor
   keeps the single-scattering albedo (the ratio between them) unchanged, so
   the haze's own COLOR/character stays physically plausible even when its
   MAGNITUDE is deliberately exaggerated.
3. Ship a sensible non-1.0 default for this new exaggeration multiplier too
   (not just leave it at the "no-op" 1.0 default Phase 1 shipped), tuned
   empirically (with Phase 5's inspection tool, once it exists, or by eye
   via Phase 4's improved preview / a live Game View screenshot) so that a
   typical test scene shows a clearly-visible, not-subtle haze by default,
   without looking absurd (e.g. not literally opaque white fog at 10 meters).

## Step 2 — The Situation (Where are we now?)

- Current (Phase-1-preserved) defaults: `aerialPerspectiveMaxDistanceKm =
  10.0` (km), `aerialPerspectiveDepthExponent = 2.0`,
  `aerialPerspectiveScatteringExaggeration = 1.0` (a real field that does
  nothing yet).
- `AERIAL_PERSPECTIVE_INVESTIGATION_FINDINGS.md`'s own numbers (Phase 6
  completion report's CPU readback, quoted verbatim in that file): even at
  the volume's FARTHEST configured distance (10km, at the OLD default),
  transmittance never drops below ~0.9956 and in-scattering never exceeds
  ~4.6e-5 — both essentially invisible once composited into an 8-bit
  `sceneColor`. At a few-meters-to-few-hundred-meters test distance, the
  numbers are 2-3 orders of magnitude smaller still.
- `AtmosphereAerialPerspectiveVolume.comp`'s own per-sample coefficient
  computation (inside the per-slice, per-sub-step loop) currently reads:
  ```glsl
  vec3 extinctionCoefficient = ComputeExtinctionCoefficientAtHeight(atmosphereParameters, currentHeightKm);
  vec3 scatterRayleigh =
      atmosphereParameters.rayleighScattering * RayleighDensityAtHeight(atmosphereParameters, currentHeightKm);
  vec3 scatterMie = atmosphereParameters.mieScattering * MieDensityAtHeight(atmosphereParameters, currentHeightKm);
  vec3 totalScattering = scatterRayleigh + scatterMie;
  ...
  vec3 source = sunRadiance * (directSource + multipleScatteringSource);
  vec3 segmentLuminance = IntegrateInscattering(source, extinctionCoefficient, segmentLengthKm);

  accumulatedLuminance += accumulatedTransmittance * segmentLuminance;
  accumulatedTransmittance *= exp(-extinctionCoefficient * segmentLengthKm);
  ```
  `extinctionCoefficient` comes from the SHARED oracle function
  `ComputeExtinctionCoefficientAtHeight()` (also used, unmodified, by the
  Transmittance LUT and Multi-Scattering LUT passes) — per this campaign's
  Master Strategy rule 4/Locked Design Decision 5, that shared function must
  NOT be edited; the exaggeration multiplier must be applied to a LOCAL copy
  of the value this shader itself computed, after calling the shared
  function, never inside it.

## Step 3 — The Plan (Detailed Steps)

### 3.1 — New default values (`AtmosphereTypes.h`)

Change `AtmosphereSettings`'s field DEFAULTS (the fields themselves already
exist from Phase 1) to:

```cpp
float aerialPerspectiveMaxDistanceKm = 0.5f;   // was 10.0f - 500m, matched to typical engine test-scene content scale (tens to a few hundred meters), per AERIAL_PERSPECTIVE_INVESTIGATION_FINDINGS.md's own recommended 0.05-0.5km range.
float aerialPerspectiveDepthExponent = 2.0f;   // unchanged - the quadratic near-camera slice density is still correct at this new, smaller scale.
int aerialPerspectiveSamplesPerSlice = 8;      // unchanged.
float aerialPerspectiveScatteringExaggeration = 6.0f; // was 1.0f (a documented STARTING point, not a promise of perfection - re-tune empirically using Phase 5's inspection tool/a live screenshot before finalizing; record whatever final value you actually ship in this phase's own completion report).
```

Update each field's own doc comment to record the OLD default and the
reasoning for the new one (mirrors this codebase's own established
"DEVIATION"/"was X, changed to Y because Z" comment convention, e.g. see
`AtmosphereAerialPerspectiveVolume.comp`'s own existing "DEVIATION FROM THE
REFERENCE" comment for the exact tone/format to match).

Also update `AtmosphereFrameUniforms`'s own field default for
`aerialPerspectiveMaxDistanceKm`/etc. — Phase 1 gave these C++-struct-level
defaults of `10.0f`/`2.0f`/`8.0f`/`1.0f` too (as a defensive fallback for any
code path that constructs `AtmosphereFrameUniforms{}` directly without going
through `AtmosphereSettings`) — decide whether to update those defaults to
match, or leave them as the "physically-accurate reference" fallback
distinct from the Editor-facing default; document whichever choice is made
and why in the completion report (leaving them at the OLD, conservative
values as a "if nobody set this, behave like the untouched reference" safety
net is a legitimate, defensible choice — just don't leave it undocumented).

### 3.2 — Scattering exaggeration math (`AtmosphereAerialPerspectiveVolume.comp`)

Read the new frame-uniforms field near the top of `main()` (alongside the
other 3 tunables Phase 1 already wired up):

```glsl
float scatteringExaggeration = max(frameUniforms.aerialPerspectiveScatteringExaggeration, 0.0);
```

Inside the per-sub-step loop, apply it to the LOCAL scattering AND
extinction values (never touching `ComputeExtinctionCoefficientAtHeight()`
itself):

```glsl
vec3 extinctionCoefficient =
    ComputeExtinctionCoefficientAtHeight(atmosphereParameters, currentHeightKm) * scatteringExaggeration;
vec3 scatterRayleigh =
    atmosphereParameters.rayleighScattering * RayleighDensityAtHeight(atmosphereParameters, currentHeightKm)
    * scatteringExaggeration;
vec3 scatterMie = atmosphereParameters.mieScattering * MieDensityAtHeight(atmosphereParameters, currentHeightKm)
    * scatteringExaggeration;
```

Everything downstream (`totalScattering`, `directSource`,
`multipleScatteringSource`, `source`, `segmentLuminance`,
`accumulatedLuminance`, `accumulatedTransmittance`) is UNCHANGED code — it
already consumes `extinctionCoefficient`/`scatterRayleigh`/`scatterMie` by
name, so multiplying those three local values by `scatteringExaggeration`
once, at their own point of computation, is sufficient; do not also
multiply `source`/`segmentLuminance` a second time (that would double-count
the exaggeration on the scattering side while under-counting it on the
extinction side, breaking the "same factor on both" energy-consistency
property this phase's own Step 1 goal 2 explicitly wants).

**Why scale extinction too, not just scattering**: scaling ONLY
`scatterRayleigh`/`scatterMie` (leaving `extinctionCoefficient` from the
shared oracle untouched) would brighten the in-scattered light without
correspondingly thickening the transmittance falloff — an increasingly
non-physical result as the exaggeration factor grows (you'd get bright haze
that never actually occludes anything behind it). Scaling both by the same
factor is equivalent to "increase the fog's own physical density", which
brightens AND thickens together, staying self-consistent at any
exaggeration value.

### 3.3 — Update this shader's own header/doc comments

Document the new `scatteringExaggeration` parameter's purpose (a Locked
Design Decision 5 callout: "applied LOCALLY here, the shared
`ComputeExtinctionCoefficientAtHeight()`/`RayleighDensityAtHeight()`/
`MieDensityAtHeight()` oracle functions themselves are NEVER modified — this
keeps the Sky-View LUT/Transmittance LUT/Multi-Scattering LUT, which also
call those same shared functions, completely physically-accurate and
unaffected by this per-view artistic knob").

### 3.4 — `AtmospherePanel.cpp`: remove the "does nothing yet" caveat text

Phase 1 added a one-line `ImGui::TextDisabled(...)` note saying the
Exaggeration slider doesn't do anything yet — delete that note now that it
is real. Consider widening its DragFloat range if empirical tuning in 3.1
lands outside the Phase-1-chosen `[0.1, 50.0]` bounds.

### 3.5 — Re-verify Phase 2's precision fixes still make sense at the new scale

At `maxDistanceKm = 0.5` (500m) instead of `10.0` (10km), the half-texel Z
bias (Phase 2) is now biasing by `0.5/32 * 500m ≈ 7.8m` per slice-width unit
near the far end (much smaller in absolute terms near the camera, where the
quadratic distribution packs slices densely) — re-confirm by inspection (no
code change expected here, just a sanity read) that Phase 2's formulas are
dimensionless (operate in slice-fraction `u` space, not raw kilometers) and
therefore remain correct at ANY `maxDistanceKm` value with zero further
change needed. This step is a verification/sanity-check, not expected to
require new code — but if it DOES reveal a problem, fix it here rather than
silently letting it ride.

## Verification

- Fast, targeted compile: build `GreatTamanaEngine`.
- This phase's own empirical tuning loop (the "do the numbers/pixels
  actually look right" part) is exactly what Phase 4 (improved volume
  preview) and Phase 5 (numeric inspection tool) are FOR — if either of
  those phases hasn't landed yet when you reach this phase in the strict
  phase order, it is acceptable (and expected) to do a first pass here using
  ONLY `gte_send_request` against `GET /get_game_view` with a simple test
  scene (a few primitives placed at varied distances, e.g. 10m/100m/400m
  from the camera) and re-tune `aerialPerspectiveScatteringExaggeration`'s
  shipped default by eye, then let Phase 4/5 give a MORE precise/objective
  confirmation afterward. Record the actual final chosen default value (and
  what visual/numeric evidence justified it) in
  `PHASE3_COMPLETION_REPORT.md` — do not ship a number with no stated
  reasoning.
- `run_app_background` + `gte_send_request` smoke test: confirm far geometry
  now visibly tints/attenuates toward the sky's own color relative to near
  geometry, and that near geometry (a few meters away) is NOT drastically
  changed (the first-slice fade from Phase 2 plus the still-small optical
  depth at true close range should keep very-near objects looking close to
  how they did before). `stop_app_background` when done.
- Write `PHASE3_COMPLETION_REPORT.md`, then commit.
