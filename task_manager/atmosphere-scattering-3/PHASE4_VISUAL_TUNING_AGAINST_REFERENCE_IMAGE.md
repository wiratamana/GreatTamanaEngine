# PHASE4 — Visual Tuning Against the Reference Image

Parent: `PHASE0_MASTER_STRATEGY.md`. Depends on Phase 2 AND Phase 3 (both
mandatory, both already landed by the time this phase starts) — this phase
tunes the FINAL production shape (Phase 3's frustum), not a fallback box.

## Step 1 — The Goal (Where are we going?)

Phase 2 and Phase 3 together fix the STRUCTURAL bug (wrong box shape/
camera angle, then wrong proxy geometry entirely). This phase is a separate,
explicitly ITERATIVE round of numeric constant tuning — exposure, density
scale, step count, camera angle/FOV/distance margin, and the frustum's own
far-plane width/height — driven by a real "change a constant in code,
rebuild, request the texture, look at it, compare against the reference
PNG, adjust again" loop, the same disciplined, documented-empirical-tuning
approach `atmosphere-scattering-2`'s own Phase 3
(`aerialPerspectiveScatteringExaggeration` tuning) already established as
this codebase's precedent for exactly this kind of problem.

This phase produces CODE CHANGES at every iteration (constant edits) — it is
not a passive "look at it and approve" phase.

## Step 2 — The Situation (Where are we now?)

- Every tunable constant this phase might touch already has a name and a
  home:
  - `kAerialPreviewXYHalfExtent`/`kAerialPreviewDepthHalfExtent`/
    `kAerialPreviewAzimuthDegrees`/`kAerialPreviewElevationDegrees`/
    `kAerialPreviewFovYDegrees`/`kAerialPreviewDistanceMargin`
    (`VolumeTexturePreviewMath.cpp`, Phase 2) — box/frustum proportions and
    camera framing.
  - `kAerialPreviewExposure` (`VolumeTexturePreviewRenderer.cpp`,
    `atmosphere-scattering-2` Phase 4, already shipped) — HDR exposure
    before the shader's own Reinhard tonemap.
  - `kDensityScale`, `kStepCount` (`VolumeTexturePreviewRenderer.h`,
    `network-impl-6` Phase 3, already shipped, generic to both
    interpretation modes today) — how quickly accumulated "haze amount"
    saturates to opaque, and how many raymarch samples are taken.
  - The frustum's own `farHalfWidth`/`farHalfHeight` multiplier inside
    `ComputeAtmosphereAerialPerspectivePreviewFrustum()` (Phase 3, already shipped).
- The reference target,
  `task_manager/atmosphere-scattering-3/aerial-persepective-lut-3d-texture.png`,
  is a schematic/diagram (not a literal raymarch render) — this phase's own
  acceptance bar is therefore **"clearly, recognizably the same KIND of
  visual" (a receding, progressively-hazier shape with a visible near/far
  gradient, viewed from a similar 3/4-side angle)**, not literal pixel
  similarity. Do not chase an exact match; chase a genuinely legible,
  structurally-similar result.

## Step 3 — The Plan (Detailed Steps)

This phase is inherently iterative — the exact number of edit/rebuild/
screenshot cycles cannot be predicted in advance. Follow this loop:

### 3.1 — Establish the current baseline

`run_app_background` the current build (post Phase 2/3), `gte_send_request`
`GET /get_texture?texture_name=AtmosphereAerialPerspectiveVolume_GameView`,
and `load_image` (or view directly) both that result and the reference PNG
side by side. Write down, concretely, what's still wrong — e.g. "too dark
overall," "the gradient is there but very subtle," "the shape reads as a
box, not a frustum," "background/box edges are hard to distinguish."

### 3.2 — Pick ONE constant to adjust per iteration

Prefer changing exactly one tunable at a time so its effect is legible —
mirrors `atmosphere-scattering-2`'s own Phase 3 tuning discipline (it tried
6.0, then 20.0, then 30.0, then 50.0 for ONE constant, in sequence, rather
than changing several at once). Plausible starting adjustments, in rough
priority order:

1. **`kAerialPreviewExposure`** if the whole shape reads as uniformly too
   dark/near-black, or conversely uniformly blown-out/white.
2. **`kDensityScale`** if the shape reads as either too transparent
   (barely visible against the background at all) or too quickly opaque
   (turns into a flat silhouette with no visible internal gradient once
   `accum.a` saturates past `0.995` too early along the ray).
3. **Camera angle constants** (`kAerialPreviewAzimuthDegrees`/
   `kAerialPreviewElevationDegrees`) if the gradient exists but is hard to
   see from the current angle (e.g. too close to face-on, or too close to
   a pure profile view that hides the box's/frustum's own 3D shape).
4. **`kAerialPreviewFovYDegrees`/`kAerialPreviewDistanceMargin`** if the
   shape is too small/too large/too tightly cropped within the 256x256
   output frame.
5. **`kStepCount`** only if visible banding/stair-stepping artifacts show up
   along the gradient (unlikely to need changing — 64 is already generous
   for a 256x256 output — but included for completeness).
6. **The frustum's own far-plane multiplier**, if the widening itself is
   too subtle or too extreme to read clearly at a glance.

### 3.3 — Rebuild, re-capture, compare, iterate

After each single-constant edit: fast, targeted compile of
`GreatTamanaEngine` (only forces a shader recompile if `VolumeTexturePreview.comp`
itself was touched — most iterations in this phase only touch `.cpp`
constants and do not need a shader rebuild at all), `run_app_background`,
`gte_send_request` the same endpoint, compare again, `stop_app_background`.
Repeat until the result is judged a clear, legible improvement over the
Phase 2/3 baseline and a reasonable structural match to the reference image.

### 3.4 — Record the final chosen values and why

Once satisfied, write a short table (constant name -> old value -> new
value -> one-sentence reason), mirroring
`AtmosphereTypes.h`'s own `AtmosphereSettings::aerialPerspectiveScatteringExaggeration`
doc-comment precedent exactly (that field's own comment records the 6.0 ->
20.0 -> 30.0 -> 50.0 history and why 30.0 was chosen) — add this table
directly into whichever constant's own doc comment ended up changing the
most (most likely `kAerialPreviewExposure` or `kDensityScale` in
`VolumeTexturePreviewRenderer.cpp`, or the camera-angle constants in
`VolumeTexturePreviewMath.cpp`), so a future maintainer re-tuning this again
later inherits the same empirical history this codebase's own conventions
always preserve.

### 3.5 — Do not change interpretation/shape LOGIC in this phase

This phase only ever edits NUMERIC CONSTANTS already introduced by Phase 2/3
— it must not add new branches, new push-constant fields, or new functions.
If, during tuning, a genuinely new piece of logic starts to feel necessary
(e.g. "we need a second exposure curve"), stop, write a short note in this
phase's own completion report describing the idea, and treat it as a
possible FUTURE follow-up campaign, not something to bolt on here
mid-tuning.

## Verification

- After the final iteration, one more fast, targeted compile +
  `run_app_background`/`gte_send_request` pass, capturing the FINAL result
  for the completion report.
- Re-confirm a known 2D texture (`Swapchain`) and a request against
  `shapeMode`'s generic path (i.e. any texture name that does NOT match the
  Aerial Perspective prefix — there is no other real volume texture in this
  engine today to test this against directly, so this is a
  code-inspection-only re-confirmation, not a live request) are both
  still unaffected by this phase's own constant changes (none of them are
  read by the generic code path at all).
- `stop_app_background` when done.
- Write `PHASE4_COMPLETION_REPORT.md` (include the before/after screenshots'
  descriptions and the final constants table from 3.4), then commit.
