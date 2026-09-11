# CAMPAIGN_COMPLETION_REPORT — atmosphere-scattering-3 ("Fix the Aerial Perspective 3D LUT preview")

Parent: `PHASE0_MASTER_STRATEGY.md`. This report closes out the whole
five-phase `atmosphere-scattering-3` campaign, tying together
`PHASE1_COMPLETION_REPORT.md` through `PHASE5_COMPLETION_REPORT.md`.

## 1. The original problem

The engine's embedded HTTP server already exposed a generic way to fetch a
live 3D (volume) texture as a raymarched thumbnail PNG (`GET
/get_texture?texture_name=<name>`, from the `network-impl-6` campaign), and
`atmosphere-scattering-2`'s Phase 4 had already taught that raymarch a
second, atmosphere-aware color interpretation specifically for the Aerial
Perspective froxel volume (`AtmosphereAerialPerspectiveVolume_GameView`/
`..._SceneView`) so its transmittance/in-scattering data reads as a legible
haze gradient instead of a raw, near-invisible density/color pair.

Despite that fix, this session's own live investigation (documented in
`PHASE0_MASTER_STRATEGY.md`) found that requesting
`GET /get_texture?texture_name=AtmosphereAerialPerspectiveVolume_GameView`
against the real, running engine still produced a **thin, nearly flat,
near-uniform dark-blue parallelogram** with no visible internal gradient and
no resemblance to the reference paper diagram
(`aerial-persepective-lut-3d-texture.png` — a camera on the left with a fan
of increasingly hazy, increasingly large quads receding into the distance).
A side-by-side comparison against `AtmosphereSkyViewLut_GameView` (a healthy,
normal-looking 2D LUT capture) confirmed the engine's rendering/HTTP/PNG
pipeline in general was fine — the defect was narrowly scoped to this one
volume's own preview shape/framing.

## 2. Root cause

Found by reading the real, current source, not by guesswork
(`PHASE0_MASTER_STRATEGY.md`, Step 2.2): the generic volume-preview
renderer's `VolumeTexturePreviewMath.h`'s `ComputeVolumeCameraSetup()` sizes
its raymarch proxy box's half-extents directly proportional to the volume's
own raw texel counts (`Vec3(w, h, d) * (0.5f / maxDim)`) — the textbook-
correct thing to do for a genuine spatial volume, where every axis measures
the same kind of physical length. The Aerial Perspective volume, however, is
`128x128x32`, and its three axes are **not comparable units at all**: X/Y are
screen-space froxel column/row indices, while Z is a camera-relative distance
SLICE index (non-linearly spaced via a quadratic depth exponent). Feeding
`(128, 128, 32)` into that formula produced `boxHalfExtents = (0.5, 0.5,
0.125)` — the ONE axis carrying the LUT's entire near/far story squashed to
exactly 1/4 the size of the other two — and the generic function's single,
fixed, "true isometric" camera angle was never chosen with an already-
paper-thin, extremely anisotropic box in mind, compounding the problem into
an unreadable, nearly edge-on sliver.

This was confirmed to be a **preview/visualization-only** defect, never a
data or physics defect: `AGENTS.md`'s own numeric readback tool already
proved the underlying LUT carries a real, non-trivial spatial gradient
(minimum transmittance ~0.71 vs. ~1.0 near the camera, maximum in-scattering
magnitude ~0.0039), and this campaign's own Phase 1 strengthened that same
proof with a new per-band (Near/Mid/Far) breakdown. The Aerial Perspective
LUT's own generation/composite shaders and `AtmosphereMath.h` were therefore
correctly identified as OUT OF SCOPE for this whole campaign and were never
touched by any phase.

## 3. What each phase did

- **Phase 1 — Root-Cause Instrumentation and Regression Tests**
  (`PHASE1_COMPLETION_REPORT.md`): zero rendering/behavior changes. Added a
  permanent characterization test
  (`AerialPerspectiveVolumeDimensionsProduceSeverelyFlattenedDepthAxisUnder
  GenericFunction`) codifying the exact geometry defect above as a checked
  fact, plus a new per-band (Near/Mid/Far third) numeric summary
  (`AerialPerspectiveBandSummary`,
  `FinalizeAerialPerspectiveBandSummary()`) layered onto the existing
  whole-volume `AtmosphereAerialPerspectiveLutInspection` tool, with its own
  Tier-1 tests proving the reduction math (including the "near stays clearer
  than far" property) directly, with no live GPU/volume needed.
- **Phase 2 — Atmosphere-Aware Preview Camera Framing**
  (`PHASE2_COMPLETION_REPORT.md`): the primary fix. Added a SECOND, dedicated
  camera + proxy-box setup function,
  `ComputeAtmosphereAerialPerspectivePreviewCameraSetup()`, alongside the
  original, untouched `ComputeVolumeCameraSetup()`, auto-selected by the
  SAME `texture_name`-prefix check `Application.cpp` already used for the
  color-interpretation mode — zero new HTTP parameter, zero shader changes.
  Verified live: the preview went from a thin, nearly uniform slab to a
  clearly elongated, wedge-shaped volume with a real, visible bright-near/
  dark-far gradient.
- **Phase 3 — Frustum-Shaped Raymarch Proxy** (`PHASE3_COMPLETION_REPORT.md`,
  MANDATORY deliverable, confirmed with the project owner): replaced Phase
  2's still-axis-aligned (if depth-elongated) box with a literal tapering
  `FrustumProxy`, via new ray/half-space intersection math
  (`IntersectRayFrustum()`/`MapFrustumLocalPositionToUvw()`/
  `ComputeAtmosphereAerialPerspectivePreviewFrustum()`) mirrored by hand into
  `VolumeTexturePreview.comp` as a new `shapeMode` push-constant branch.
  Verified live: the preview now genuinely WIDENS from a narrow near end to a
  wide far end, matching the reference diagram's fan-of-quads concept — not
  merely "extended" the way any axis-aligned box necessarily would. Flagged
  one non-blocking follow-up for Phase 4: the far-cap corners sat at only
  ~92.8% of the camera's own FOV half-angle (a thin but non-clipping margin).
- **Phase 4 — Visual Tuning Against the Reference Image**
  (`PHASE4_COMPLETION_REPORT.md`): a real, iterative "change one constant,
  rebuild, screenshot, compare against the reference PNG, adjust" loop over
  Phase 3's final frustum shape. Four single-constant iterations
  (`kAerialPreviewDistanceMargin`: `1.15`→`1.45`→`1.25`;
  `kAerialPreviewAzimuthDegrees`: `75`→`85`; `kAerialPreviewElevationDegrees`:
  `18`→`10`; `kAerialPreviewExposure`: `2000.0`→`3200.0`) resolved the flagged
  clipping-margin risk, better centered the wedge in the 256x256 frame, and
  made the far (hazy) end read as a clearly brighter, more "glowing" blue —
  all with zero shader/logic changes and zero risk to the shared
  `kDensityScale`/`kStepCount` constants (deliberately left untouched).
- **Phase 5 — Docs, Regression Safety, and Final Build** (this campaign's
  last phase, `PHASE5_COMPLETION_REPORT.md`): updated `AGENTS.md` ("Atmosphere
  Scattering" and "Networking" -> "Named Texture Capture" sections) and
  `README.md`'s "Status" section to document the whole fix, performed an
  explicit zero-regression confirmation for every other (non-Aerial-
  Perspective) volume/2D texture, and ran the one full incremental build +
  full, unfiltered `ctest` regression pass this campaign's own conventions
  reserve for the final phase.

## 4. The final visual result

Before this campaign: `GET /get_texture?texture_name=
AtmosphereAerialPerspectiveVolume_GameView` returned a thin, nearly flat,
near-uniform dark-blue parallelogram with no visible internal gradient and no
resemblance to a receding, hazier-with-distance volume.

After this campaign (final capture, re-confirmed in Phase 5's own runtime
smoke check): the same endpoint returns a clearly widening, tapering
wedge/frustum shape — dark and narrow near the apex (camera-near, low haze),
fanning out into a visibly brighter, more "glowing" blue far end
(camera-far, high haze) — well-centered in the 256x256 frame with a
comfortable, non-clipping dark-background margin on every side. A human or
LLM agent looking at this image side-by-side with the reference paper diagram
(`aerial-persepective-lut-3d-texture.png`) would recognize it as "the same
kind of thing": a receding, widening, progressively hazier volume — exactly
the acceptance bar `PHASE0_MASTER_STRATEGY.md` set at the start of this
campaign.

Every other texture this engine can capture over HTTP — every other 2D LUT
(`AtmosphereTransmittanceLut`, `AtmosphereMultiScatteringLut`,
`AtmosphereSkyViewLut_GameView`/`_SceneView`), the debug-slice mirror
(`AtmosphereAerialPerspectiveVolumeDebugSlice`), the Game/Scene render
targets, and the swapchain itself — is completely unaffected by any change in
this campaign, confirmed both by code inspection (every generic-path branch
is byte-for-byte identical to its pre-campaign form) and by repeated live
HTTP re-confirmation across all five phases.

## 5. Final full build/test pass result (Phase 5, Step 3.4)

- `cmake --build build --target GreatTamanaEngine` and
  `cmake --build build --target GreatTamanaEngineTests` — both reported
  `ninja: no work to do.` (already fully up to date; this phase's own changes
  were documentation-only and triggered no recompile — every prior phase's
  code change had already been built and verified in its own session).
- `ctest -C Debug --output-on-failure` (the full suite, no `-R` filter):
  **100% tests passed out of 1286** (1 pre-existing, machine-gated
  `PmxLoaderRealModelSmokeTest` skipped, a long-standing condition unrelated
  to this campaign). Zero regressions of any kind.
- Final live smoke check: `GET /get_texture?texture_name=
  AtmosphereAerialPerspectiveVolume_GameView` (the campaign's own final
  "after" screenshot — a clearly widening, hazier-with-distance wedge),
  `GET /get_swapchain` (a normal, full Editor UI screenshot, unaffected), and
  `GET /list_textures` (every texture, 2D and volume, still present and
  correctly typed) all returned HTTP 200 with the expected content.

## 6. Campaign disposition

All five phases of `atmosphere-scattering-3` are complete. The Aerial
Perspective volume's HTTP/LLM-agent preview now genuinely conveys the LUT's
own real, physically-grounded near/far haze gradient in a widening,
frustum-shaped raymarch — the mandatory frustum deliverable (Phase 3) shipped
exactly as required, and the whole preview path was tuned (Phase 4),
documented (Phase 5), and re-verified with zero regressions via a full clean
incremental build and full test suite pass. This campaign is closed.
