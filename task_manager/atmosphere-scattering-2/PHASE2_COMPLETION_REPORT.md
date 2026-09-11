# PHASE2_COMPLETION_REPORT — Aerial Composite Precision Fixes

Parent: `PHASE0_MASTER_STRATEGY.md`. Implements
`PHASE2_AERIAL_COMPOSITE_PRECISION_FIXES.md` exactly, against the real,
post-Phase-1 state of `AtmosphereAerialPerspectiveComposite.comp`
(`PHASE1_COMPLETION_REPORT.md` was read first, per this phase's own
instruction — Phase 1's actual committed shader matched the strategy file's
own predicted shape closely: local variables are named `maxDistanceKm`/
`depthExponent`, read from `pc.aerialPerspectiveStrengthAndPad.y`/`.z`, and
the pre-existing code at the edit site was byte-for-byte what Phase 2's own
"Step 2 — Current code" snippet predicted, so no adaptation was needed).

## What was done

Both documented precision gaps from `pl-sky_aerial_perspective.md` §5 (points
7 and 9) were fixed in `src/Shaders/AtmosphereAerialPerspectiveComposite.comp`,
exactly per the phase file's own "Step 3 — The Plan":

1. **Half-texel Z-bias (§5 point 7, Step 3.1).** The old code computed
   `slice` via `ViewDepthToFroxelSlice(...)` and used
   `sliceUv = clamp(slice / volumeSize.z, 0, 1)` directly as the volume's Z
   texture coordinate — no correction for the fact that slice `i` stores the
   *cumulative* value at that slice's *far* boundary, while a 3D texture
   sample lands on texel *centers*. Replaced with:
   - `sliceCount = float(volumeSize.z)` (named, reused below instead of
     repeating `float(volumeSize.z)`).
   - `boundaryU = clamp(slice / sliceCount, 0.0, 1.0)` — the raw, un-biased
     boundary fraction (this is the exact value the old code used directly).
   - `halfTexelZ = 0.5 / sliceCount`; `sliceUv = clamp(boundaryU - halfTexelZ,
     halfTexelZ, 1.0 - halfTexelZ)` — the documented `-0.5/sliceCount` bias,
     re-clamped into the valid texel-center range, mirroring
     `ClampLutUvHalfTexelInset()`'s existing 2D pattern in
     `AtmosphereCommon.glsl` applied here to a single Z axis.
   - The raw volume sample is now named `sampledAerial` (previously `aerial`
     was assigned directly from the `texture()` call).

2. **First-slice fade-in blend (§5 point 9, Step 3.2).** Immediately after
   computing `sampledAerial`, added:
   - `firstSliceBlend = clamp(boundaryU * sliceCount, 0.0, 1.0)` — using the
     UN-biased `boundaryU` (not `sliceUv`) as the plan specifies, since this
     needs to measure "how far across slice 0's own span" the real pixel
     distance is, not the already-biased sample coordinate.
   - `aerial = mix(vec4(0.0, 0.0, 0.0, 1.0), sampledAerial, firstSliceBlend)`
     — blends from "no scattering added, full transmittance" (pure
     pass-through of `sceneColor`) up to the real sampled value across slice
     0's span, exactly as documented.
   - The rest of `main()` (`strength`/`inScattering`/`transmittance`/
     `finalColor`) was left completely untouched — it already consumed a
     local named `aerial`; only its right-hand-side definition changed.

3. **Header comment update (Step 3.3).** Added a new paragraph to the file's
   top-of-file doc comment, citing this campaign
   (`atmosphere-scattering-2`, Phase 2) and `pl-sky_aerial_perspective.md` §5
   points 7/9 by name, describing both fixes and pointing at the new
   `boundaryU`/`halfTexelZ`/`sampledAerial`/`firstSliceBlend` locals — mirroring
   how every other shader in this codebase cites its own reference-material
   section (e.g. the existing "Phase 8" comment a few lines below it, left
   unchanged).

4. **Sanity-check against the CPU-oracle discipline (Step 3.4).** Confirmed:
   this edit is entirely local to
   `AtmosphereAerialPerspectiveComposite.comp`'s own `main()`. It calls
   `ViewDepthToFroxelSlice()` with the exact same arguments as before (only
   the now-named `sliceCount` local replaces a repeated `float(volumeSize.z)`
   expression) and `ClampLutUvHalfTexelInset()` itself was NOT touched or
   even called — the half-texel logic was hand-written inline for the single
   Z scalar, per the plan's own snippet, rather than trying to force-fit the
   existing `vec2` helper. No changes were made anywhere in
   `AtmosphereMath.h`/`AtmosphereCommon.glsl`.

## Deviations from the written plan

None of substance — the implementation matches the phase file's own GLSL
snippets (Step 3.1/3.2) essentially verbatim, since Phase 1's actual
committed shader already matched what Phase 2 assumed it would look like.
One cosmetic addition beyond the plan's own snippet: a blank line was
inserted between the new `aerial = mix(...)` line and the pre-existing
"Phase 8" `strength`/`inScattering` comment block, purely for readability
(no functional difference).

## Verification performed

1. **Targeted compile check**: `cmake --build build --target
   GreatTamanaEngine` — succeeded. Only the one touched shader was
   recompiled by `glslc`
   (`src/Shaders/AtmosphereAerialPerspectiveComposite.comp` ->
   `AtmosphereAerialPerspectiveComposite.comp.spv`); no other `.comp`/`.frag`
   needed rebuilding since `AtmosphereCommon.glsl` (an `#include`d dependency
   several other atmosphere shaders share) was NOT modified this phase — a
   narrower rebuild footprint than Phase 1's own compile check, which touched
   the shared include and rebuilt every dependent shader. Zero compile
   errors/warnings from `glslc`, full `.exe` link succeeded.
2. **Runtime smoke test** via `run_app_background` + `gte_send_request`:
   - Launched `GreatTamanaEngine.exe`, waited for the first frame, and
     captured `GET /get_swapchain` — the Editor's "Scene" view renders the
     same physically-plausible dusk sky gradient (blue zenith fading to a
     warm horizon glow) as Phase 1's own screenshot, with no visible
     artifact, crash, or black-screen regression.
   - `GET /list_textures` confirmed the full pass chain is still healthy and
     updating: `AtmosphereTransmittanceLut`, `AtmosphereMultiScatteringLut`,
     `AtmosphereSkyViewLut_GameView`/`_SceneView`,
     `AtmosphereAerialPerspectiveVolumeDebugSlice`, and both
     `GameView`/`GameViewComposited` (the composite pass's own output this
     phase directly modified) all report sane extents and non-stale
     `frames_since_update` values.
   - `GET /get_game_view` correctly returned `409` since "Game" was the
     currently-inactive/hidden dock tab this session (only "Scene" was the
     active tab) — expected per `AGENTS.md`'s "Editor Module Structure"
     visibility-driven-rendering rule, not a bug (matches Phase 1's own
     completion report, which hit the exact same expected condition).
   - This session's default scene had no spawned entities other than the
     default Camera (`Hierarchy` showed only `Entity 0 (Camera)`, no test
     geometry) — so there was no near-camera opaque object in frame to
     directly eyeball the first-slice-fade/half-texel-bias delta against.
     Combined with the fact that `AtmosphereSettings::aerialPerspectiveMaxDistanceKm`
     is still Phase 1's 10km default (Phase 3 is what will actually shrink
     it), this is exactly the **expected, imperceptible visual diff** result
     the phase file's own "Verification" section calls out in advance for
     this specific phase — nothing observed contradicts that expectation.
     No byte-level pixel diff against a pre-Phase-2 screenshot was performed
     beyond this qualitative comparison, since no near-camera geometry
     existed this session to localize a diff against in the first place; the
     sky-only frame is visually identical to Phase 1's own capture.
   - Stopped the app via `stop_app_background` afterward.

No full build/full regression test was run (per the Master Strategy's own
"Workflow rules" rule 1 — deferred to Phase 6).

## Next phase

Phase 3 (`PHASE3_AERIAL_PERSPECTIVE_VISIBILITY_REBALANCE.md`) can now safely
ship new, much smaller default `aerialPerspectiveMaxDistanceKm`/add the new
scattering-exaggeration multiplier — once the effect's magnitude actually
becomes large enough to see, this phase's half-texel bias and first-slice
fade-in fixes are already in place, so Phase 3's own before/after comparison
will be a clean, single-variable change rather than also having to account
for these two pre-existing precision gaps becoming visible at the same time.
