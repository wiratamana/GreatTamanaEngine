# PHASE2 — Atmosphere-Aware Preview Camera + Box Framing (the primary fix)

Parent: `PHASE0_MASTER_STRATEGY.md`. Depends on Phase 1 (its characterization
test is the documented "before" baseline this phase's own new function is
explicitly designed to NOT reproduce). This is the primary, highest-value
fix in this whole campaign — after this phase lands, the preview should
already look dramatically closer to the reference image, even before
Phase 3/4's further refinements.

## Step 1 — The Goal (Where are we going?)

Give the Aerial Perspective volume's HTTP preview
(`GET /get_texture?texture_name=AtmosphereAerialPerspectiveVolume_GameView`/
`..._SceneView`) its own SECOND, dedicated camera + raymarch-proxy-box setup
— auto-selected the exact same way `atmosphere-scattering-2`'s Phase 4
already auto-selects its color-interpretation mode (by `texture_name`
prefix, zero new HTTP parameter, zero Application.cpp changes) — so the
depth (Z / camera-distance) axis, the ONLY axis carrying this LUT's entire
near-vs-far story, is rendered as the visually DOMINANT axis, viewed from an
angle that actually reveals its gradient, instead of the generic function's
4x-flattened, wrong-angle result Phase 1 just codified as a checked fact.

**Zero shader changes in this phase** — `VolumeTexturePreview.comp`'s
existing `IntersectRayBox()`/box-sampling logic is completely correct and
untouched; only WHICH box shape/camera position/orientation numbers get fed
into it (still via the exact same `PushConstants` fields that already
exist) changes, and only for the Aerial Perspective case.

## Step 2 — The Situation (Where are we now?)

- `src/Renderer/VolumeTexturePreviewRenderer.cpp`'s `RenderPreview()`
  currently has exactly ONE call to `ComputeVolumeCameraSetup(...)`
  (unconditional, regardless of `interpretation`):
  ```cpp
  const VolumeCameraSetup setup = ComputeVolumeCameraSetup(
      static_cast<int>(volume.extent.width), static_cast<int>(volume.extent.height), static_cast<int>(volume.extent.depth));
  ```
  This is the ONE call site this phase changes.
- `RenderPreview()` ALREADY receives an `interpretation` parameter
  (`VolumeTexturePreviewInterpretation::AtmosphereAerialPerspective` vs.
  `GenericDensityInAlpha`), already correctly set per-request by
  `Application.cpp`'s existing `isAerialPerspectiveVolume` check
  (`requestedName.rfind("AtmosphereAerialPerspectiveVolume", 0) == 0`, line
  ~962) — this phase reuses that SAME already-computed, already-correct
  value to ALSO select the camera/box setup; it does not add any new
  parameter to `RenderPreview()`, and it does not touch `Application.cpp` at
  all.
- `VolumeCameraSetup` (the struct both the old and new function return) is
  already a plain, generic bag of `eyePosition`/`forward`/`right`/`up`/
  `tanHalfFovY`/`boxHalfExtents` — nothing about it is generic-volume-
  specific, so no struct changes are needed either; only a second FUNCTION
  that fills it out differently is needed.

## Step 3 — The Plan (Detailed Steps)

### 3.1 — `VolumeTexturePreviewMath.h`: declare the new function

Add, immediately after the existing `ComputeVolumeCameraSetup()`
declaration:

```cpp
// atmosphere-scattering-3 campaign, Phase 2
// (task_manager/atmosphere-scattering-3/PHASE2_ATMOSPHERE_AWARE_PREVIEW_CAMERA_FRAMING.md)
// - a SECOND, dedicated camera + proxy-box setup, used ONLY for the Aerial
// Perspective froxel volume's own HTTP preview (auto-selected by
// VolumeTexturePreviewRenderer::RenderPreview() based on its own
// `interpretation` parameter - mirrors VolumeTexturePreviewInterpretation's
// existing selection convention exactly, see VolumeTexturePreviewRenderer.h).
//
// UNLIKE ComputeVolumeCameraSetup() above, this deliberately does NOT scale
// boxHalfExtents proportionally to the volume's raw texel counts - for a
// camera-frustum-shaped froxel LUT, width/height (screen-space column/row
// index) and depth (camera-relative distance SLICE index) are fundamentally
// different UNITS that merely happen to be stored inside the same 3D
// texture; treating all three as comparable physical lengths (which is the
// textbook-CORRECT thing ComputeVolumeCameraSetup() does for an actual
// spatial volume, e.g. a smoke/cloud Texture3D) squashes the ONE axis that
// carries this LUT's entire near/far story into an imperceptible sliver -
// see PHASE1_ROOT_CAUSE_INSTRUMENTATION_AND_REGRESSION_TESTS.md's own
// characterization test (VolumeTexturePreviewMathTests.cpp,
// AerialPerspectiveVolumeDimensionsProduceSeverelyFlattenedDepthAxisUnderGenericFunction)
// for the exact measured numbers this function exists to avoid repeating.
//
// `width`/`height`/`depth` are accepted (matching ComputeVolumeCameraSetup()'s
// own signature, so both functions are trivially interchangeable at the call
// site) but deliberately UNUSED for the box shape itself - kept as
// parameters purely so a future maintainer isn't surprised the two
// functions don't share a signature; do not remove them.
VolumeCameraSetup ComputeAtmosphereAerialPerspectivePreviewCameraSetup(int width, int height, int depth);
```

### 3.2 — `VolumeTexturePreviewMath.cpp`: implement it

Add to the existing anonymous namespace (alongside `kAzimuthDegrees`/
`kElevationDegrees`/etc. - do not remove or rename anything already there):

```cpp
// atmosphere-scattering-3, Phase 2 - fixed box half-extents for the Aerial
// Perspective preview: X/Y stay modest (narrower than the generic
// function's own 0.5, deliberately, to leave headroom below), while Z (the
// depth/distance axis) is given a much LARGER, fixed half-extent so it
// reads as the visually dominant axis - matching how the reference concept
// diagram (task_manager/atmosphere-scattering-3/
// aerial-persepective-lut-3d-texture.png) draws it: a shape that visibly
// RECEDES away from the camera, not a flat slab. These two constants (and
// the camera angle ones below) are explicitly re-tuned empirically in
// PHASE4_VISUAL_TUNING_AGAINST_REFERENCE_IMAGE.md - treat the values below
// as a reasonable, principled STARTING point, not a final, load-bearing
// choice.
constexpr float kAerialPreviewXYHalfExtent = 0.35f;
constexpr float kAerialPreviewDepthHalfExtent = 0.9f; // ~2.5x the XY half-extent.

// A near-SIDE-ON viewing angle - UNLIKE ComputeVolumeCameraSetup()'s own
// true-isometric 45/35.26 degree angle, this is chosen so the camera looks
// mostly ACROSS the elongated depth axis (rather than staring down its own
// barrel), which is what actually reveals a near-to-far color/opacity
// transition as a visible gradient across the rendered frame - the same
// "camera positioned to one side, subject recedes toward the other side of
// frame" composition the reference diagram itself uses.
constexpr float kAerialPreviewAzimuthDegrees = 75.0f;
constexpr float kAerialPreviewElevationDegrees = 18.0f;
constexpr float kAerialPreviewFovYDegrees = 40.0f;
constexpr float kAerialPreviewDistanceMargin = 1.15f;
```

Then, mirroring `ComputeVolumeCameraSetup()`'s own existing body structure
exactly (same spherical-to-Cartesian eye placement, same look-at basis
construction) but reading from the new constants above and completely
ignoring `width`/`height`/`depth`:

```cpp
VolumeCameraSetup ComputeAtmosphereAerialPerspectivePreviewCameraSetup(int width, int height, int depth)
{
    (void)width;
    (void)height;
    (void)depth; // Deliberately unused - see this function's own header comment in VolumeTexturePreviewMath.h.

    VolumeCameraSetup setup;
    setup.boxHalfExtents = Vec3(kAerialPreviewXYHalfExtent, kAerialPreviewXYHalfExtent, kAerialPreviewDepthHalfExtent);

    const float fovYRadians = DegToRad(kAerialPreviewFovYDegrees);
    setup.tanHalfFovY = std::tan(fovYRadians * 0.5f);

    const float boundingRadius = Length(setup.boxHalfExtents);
    const float distance = (boundingRadius / std::sin(fovYRadians * 0.5f)) * kAerialPreviewDistanceMargin;

    const float azimuthRadians = DegToRad(kAerialPreviewAzimuthDegrees);
    const float elevationRadians = DegToRad(kAerialPreviewElevationDegrees);
    const float cosElevation = std::cos(elevationRadians);
    const Vec3 directionFromCenter = Vec3(cosElevation * std::sin(azimuthRadians), std::sin(elevationRadians),
        cosElevation * std::cos(azimuthRadians));

    setup.eyePosition = directionFromCenter * distance;
    setup.forward = Normalize(Vec3::Zero() - setup.eyePosition);
    const Vec3 worldUp = Vec3::Up();
    setup.right = Normalize(Cross(worldUp, setup.forward));
    setup.up = Cross(setup.forward, setup.right);
    return setup;
}
```

Confirm the box's LOCAL +Z axis (which spans `[-kAerialPreviewDepthHalfExtent,
+kAerialPreviewDepthHalfExtent]` and is what `VolumeTexturePreview.comp`'s
existing `uvw = (localPos / pc.boxHalfExtents) * 0.5 + 0.5` maps to the
texture's own `w` (3rd `sampler3D`) coordinate) still corresponds exactly to
the same froxel Z/depth-slice axis it always did — this mapping is entirely
inside the ALREADY-CORRECT, unmodified shader math; only the box's own
proportions/camera angle change in this phase, never the sampling formula.

### 3.3 — `VolumeTexturePreviewRenderer.cpp`: select the new function

In `RenderPreview()`, replace the single unconditional call:

```cpp
const VolumeCameraSetup setup = ComputeVolumeCameraSetup(
    static_cast<int>(volume.extent.width), static_cast<int>(volume.extent.height), static_cast<int>(volume.extent.depth));
```

with:

```cpp
const VolumeCameraSetup setup = (interpretation == VolumeTexturePreviewInterpretation::AtmosphereAerialPerspective)
    ? ComputeAtmosphereAerialPerspectivePreviewCameraSetup(
          static_cast<int>(volume.extent.width), static_cast<int>(volume.extent.height), static_cast<int>(volume.extent.depth))
    : ComputeVolumeCameraSetup(
          static_cast<int>(volume.extent.width), static_cast<int>(volume.extent.height), static_cast<int>(volume.extent.depth));
```

This is the ENTIRE code change needed in this file for this phase — every
other line of `RenderPreview()` (descriptor rewrite, push-constant fill,
`ImmediateSubmit()` dispatch, readback) is unchanged, since `setup`'s fields
are consumed identically regardless of which function produced them.

Add `#include "VolumeTexturePreviewMath.h"` is already present in this file
(it already calls `ComputeVolumeCameraSetup()`) — no new include needed.

### 3.4 — Update stale doc comments

`VolumeTexturePreviewRenderer.h`'s `RenderPreview()` doc comment currently
only describes `interpretation` as selecting "the raw-texel -> density/color
derivation" — extend it (in place) to also mention it now selects the
camera/box FRAMING too, per this phase, so the comment stays honest. Do the
same for `VolumeTexturePreviewInterpretation`'s own enum doc comment in the
same header if it makes the same claim.

### 3.5 — Zero regression to the generic path

Confirm, by inspection: any `texture_name` that does NOT match
`isAerialPerspectiveVolume` still resolves `interpretation ==
GenericDensityInAlpha`, and therefore still calls the ORIGINAL,
byte-for-byte-unchanged `ComputeVolumeCameraSetup()` — this campaign adds a
new branch, it must never alter the existing one's own behavior or code
path.

### 3.6 — New Tier-1 tests

Add to `tests/Renderer/VolumeTexturePreviewMathTests.cpp` (new `TEST(...)`
blocks, alongside Phase 1's own new characterization test):

```cpp
TEST(VolumeTexturePreviewMathTest, AtmosphereAerialPerspectivePreviewProducesADepthDominantBox)
{
    const VolumeCameraSetup setup = ComputeAtmosphereAerialPerspectivePreviewCameraSetup(128, 128, 32);
    // The OPPOSITE of the generic function's own behavior for these same
    // dimensions (see PHASE1's characterization test) - depth must now be
    // the LARGEST axis, not the smallest.
    EXPECT_GT(setup.boxHalfExtents.z, setup.boxHalfExtents.x);
    EXPECT_GT(setup.boxHalfExtents.z, setup.boxHalfExtents.y);
}

TEST(VolumeTexturePreviewMathTest, AtmosphereAerialPerspectivePreviewCameraBasisIsOrthonormal)
{
    const VolumeCameraSetup setup = ComputeAtmosphereAerialPerspectivePreviewCameraSetup(128, 128, 32);
    EXPECT_NEAR(Length(setup.forward), 1.0f, 1e-4f);
    EXPECT_NEAR(Length(setup.right), 1.0f, 1e-4f);
    EXPECT_NEAR(Length(setup.up), 1.0f, 1e-4f);
    EXPECT_NEAR(Dot(setup.forward, setup.right), 0.0f, 1e-4f);
    EXPECT_NEAR(Dot(setup.forward, setup.up), 0.0f, 1e-4f);
    EXPECT_NEAR(Dot(setup.right, setup.up), 0.0f, 1e-4f);
}

TEST(VolumeTexturePreviewMathTest, AtmosphereAerialPerspectivePreviewEyeSitsOutsideTheBoundingSphere)
{
    const VolumeCameraSetup setup = ComputeAtmosphereAerialPerspectivePreviewCameraSetup(128, 128, 32);
    const float boundingRadius = Length(setup.boxHalfExtents);
    EXPECT_GT(Length(setup.eyePosition), boundingRadius);
}
```
(Mirror the exact assertion style/tolerances the existing generic-function
tests in this same file already use for these same three properties — read
the current file first so tolerances/helper usage stay consistent.)

## Verification

- Fast, targeted compile: build `gte_core` + `GreatTamanaEngineTests`
  (forces no shader recompile at all this phase — confirm `glslc` is NOT
  invoked for `VolumeTexturePreview.comp`, since this phase touches zero
  `.comp` files).
- `ctest -C Debug -R VolumeTexturePreviewMathTest --output-on-failure`.
- `run_app_background` the built `GreatTamanaEngine.exe`, then:
  - `gte_send_request` `GET /get_texture?texture_name=AtmosphereAerialPerspectiveVolume_GameView`
    and visually compare the result against BOTH (a) this campaign's own
    documented "before" screenshot (a thin, nearly flat, near-uniform blue
    parallelogram — see `PHASE0_MASTER_STRATEGY.md`, Step 2.1) and (b) the
    reference `aerial-persepective-lut-3d-texture.png`. Confirm the new
    capture shows a clearly elongated, non-flat shape with a VISIBLE
    transmittance/color gradient from one end to the other (it does not need
    to look identical to the reference PNG yet — Phase 3/4 refine this
    further — it only needs to be UNAMBIGUOUSLY better than the "before"
    baseline: real internal structure, not a flat slab).
  - Re-confirm a known 2D texture (`GET /get_texture?texture_name=Swapchain`
    or `GET /get_swapchain`) still returns its normal, unaffected capture.
  - `stop_app_background` when done.
- Write `PHASE2_COMPLETION_REPORT.md` (include a description of the
  before/after visual difference actually observed), then commit.
