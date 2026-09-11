# PHASE2 — Aerial Perspective Composite Pass: Precision Fixes

Parent: `PHASE0_MASTER_STRATEGY.md`. Depends on Phase 1 (this phase edits the
SAME `AtmosphereAerialPerspectiveComposite.comp` file Phase 1 just touched —
read `PHASE1_COMPLETION_REPORT.md` first in case Phase 1's own implementation
ended up shaping the file slightly differently than its own strategy
document predicted, e.g. a different local variable name for
`maxDistanceKm`/`depthExponent`).

## Step 1 — The Goal (Where are we going?)

Fix the two real, already-documented (not new discoveries — see
`AERIAL_PERSPECTIVE_INVESTIGATION_FINDINGS.md`'s own "Minor, real (but NOT
the cause of 'no effect at all') deviations from `pl-sky`" section) precision
gaps in the composite pass, so that once Phase 3 makes the effect's
magnitude large enough to actually be visible, it is also SAMPLED and BLENDED
correctly — matching `pl-sky_aerial_perspective.md` §5 (points 7 and 9)
exactly:

1. **Half-texel Z-bias** when converting a pixel's real view distance into a
   volume Z lookup coordinate.
2. **First-slice fade-in blend**, so geometry that falls inside the coarse,
   nearest froxel slice doesn't show a visible seam/pop.

## Step 2 — The Situation (Where are we now?)

From `AERIAL_PERSPECTIVE_INVESTIGATION_FINDINGS.md`, section "Minor, real...
deviations from `pl-sky`":

> 1. **No half-texel Z-bias when sampling the volume in the composite
>    pass.** `pl-sky_aerial_perspective.md` §5 point 7 describes biasing the
>    composite pass's Z lookup by `-0.5/sliceCount` before sampling, since
>    slice `i` stores the cumulative value at the slice's *far* boundary
>    while a 3D texture sample lands on texel *centers*.
>    `AtmosphereAerialPerspectiveComposite.comp` does
>    `sliceUv = clamp(slice / volumeSize.z, 0, 1)` with no such half-texel
>    correction — every composited pixel ends up reading roughly half a
>    slice "ahead" of where it should.
> 2. **No "first-slice fade-in" blend.** §5 point 9 describes blending from
>    `(0,0,0,1)` (no scattering, full transmittance) up to the sampled
>    aerial value across the first slice, specifically to avoid a seam for
>    geometry that falls inside the coarse first froxel slice.

Both gaps are currently harmless ONLY because the affected values are
already ~0 at meter-scale distances — once Phase 3 ships a much smaller
default max-distance and a scattering-exaggeration multiplier, these two
gaps stop being "harmless in relative terms" and start being genuinely
visible artifacts (a half-slice-early haze onset, and/or a seam right where
geometry crosses into the second froxel slice). Fixing them NOW (before
Phase 3 lands) means Phase 3's own before/after comparison is a clean,
single-variable change.

Current code (post-Phase-1, `AtmosphereAerialPerspectiveComposite.comp`,
`main()`, roughly where the investigation found it — verify against
Phase 1's actual committed result before editing):

```glsl
ivec3 volumeSize = textureSize(aerialPerspectiveVolume, 0);
float slice =
    ViewDepthToFroxelSlice(viewDistanceKm, float(volumeSize.z), maxDistanceKm, depthExponent);
float sliceUv = clamp(slice / float(volumeSize.z), 0.0, 1.0);

vec4 aerial = texture(aerialPerspectiveVolume, vec3(uv, sliceUv));
float strength = pc.aerialPerspectiveStrengthAndPad.x;
vec3 inScattering = aerial.rgb * strength;
float transmittance = mix(1.0, aerial.a, strength);

vec3 sceneColor = texture(sourceColor, uv).rgb;
vec3 finalColor = sceneColor * transmittance + inScattering;
```

## Step 3 — The Plan (Detailed Steps)

### 3.1 — Half-texel Z-bias

`slice` (the result of `ViewDepthToFroxelSlice()`) is already in "slice
units" (0..`volumeSize.z`), so `boundaryU = slice / float(volumeSize.z)` is
exactly the `boundaryU` value `pl-sky_aerial_perspective.md` §5 point 6
describes. Apply the documented `-0.5/sliceCount` bias, then re-clamp into
the valid texel-CENTER range (mirrors `ClampLutUvHalfTexelInset()`'s own
existing 2D pattern in `AtmosphereCommon.glsl`, applied here to a single Z
axis instead of a `vec2`):

```glsl
ivec3 volumeSize = textureSize(aerialPerspectiveVolume, 0);
float sliceCount = float(volumeSize.z);
float slice =
    ViewDepthToFroxelSlice(viewDistanceKm, sliceCount, maxDistanceKm, depthExponent);
float boundaryU = clamp(slice / sliceCount, 0.0, 1.0);

float halfTexelZ = 0.5 / sliceCount;
float sliceUv = clamp(boundaryU - halfTexelZ, halfTexelZ, 1.0 - halfTexelZ);

vec4 sampledAerial = texture(aerialPerspectiveVolume, vec3(uv, sliceUv));
```

(`sampledAerial` replaces the old `aerial` local — 3.2 below introduces the
final, fade-blended `aerial` value that the rest of `main()` then uses
unchanged.)

### 3.2 — First-slice fade-in blend

Immediately after computing `sampledAerial`, blend it with the "no effect
yet" value across the very first slice, exactly per §5 point 9:

```glsl
// First-slice fade-in (pl-sky_aerial_perspective.md §5, point 9): geometry
// that falls inside the COARSE first froxel slice would otherwise show a
// visible seam/pop, since that slice's own internal precision is the
// poorest in the whole volume (see AtmosphereAerialPerspectiveVolume.comp's
// own quadratic depth-slice distribution - slice 0 covers the smallest,
// most quickly-changing distance range). Blends from "no scattering added,
// full transmittance" (a pure pass-through of sceneColor) up to the real
// sampled value as boundaryU sweeps across slice 0's own span
// (0 .. 1/sliceCount).
float firstSliceBlend = clamp(boundaryU * sliceCount, 0.0, 1.0);
vec4 aerial = mix(vec4(0.0, 0.0, 0.0, 1.0), sampledAerial, firstSliceBlend);
```

The rest of `main()` (the `strength`/`inScattering`/`transmittance`/
`finalColor` computation) is UNCHANGED — it already consumes a local
variable named `aerial`; only its right-hand-side definition changes from a
direct `texture()` call to this new blended value.

### 3.3 — Update this shader's own header comment

The file's top-of-file doc comment currently only describes the un-biased,
non-fade version of the algorithm — update it to describe the half-texel
bias and first-slice fade now present, citing this campaign
(`atmosphere-scattering-2`, Phase 2) and `pl-sky_aerial_perspective.md` §5
points 7/9 by name, mirroring how every other shader in this codebase cites
its own reference-material section.

### 3.4 — Sanity-check against the CPU-oracle discipline

This campaign's own Master Strategy rule 4 says shared oracle functions in
`AtmosphereMath.h`/`AtmosphereCommon.glsl` are never touched. Confirm this
phase's edit stays entirely LOCAL to
`AtmosphereAerialPerspectiveComposite.comp`'s own `main()` — it must not
require any change to `ViewDepthToFroxelSlice()`/`ClampLutUvHalfTexelInset()`
themselves (both stay exactly as-is; this phase only calls them
differently/adds one small new local calculation inline).

## Verification

- Fast, targeted compile: build `GreatTamanaEngine` (forces `glslc` to
  recompile this one shader).
- A manual runtime smoke check is worthwhile here too, even though the
  visual delta is expected to be small/subtle at today's (still Phase-1-era)
  10km default: `run_app_background` + `gte_send_request` against
  `GET /get_game_view`, confirming no crash, no obviously-wrong full-black/
  full-white composited frame, and (if feasible) a byte-level diff against a
  pre-Phase-2 screenshot showing a small, localized change near the very
  closest geometry only (where the first-slice fade only now kicks in) —
  document whatever you observe in the completion report; a genuinely
  imperceptible diff at 10km-scale defaults is an EXPECTED, not alarming,
  result for this phase specifically (Phase 3 is what makes this visible).
  `stop_app_background` when done.
- Write `PHASE2_COMPLETION_REPORT.md`, then commit.
