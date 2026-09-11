# PHASE3_COMPLETION_REPORT — Frustum-Shaped Raymarch Proxy (MANDATORY deliverable)

Parent: `PHASE0_MASTER_STRATEGY.md`. Implements
`PHASE3_FRUSTUM_SHAPED_RAYMARCH_PROXY.md` in full, including the corrections
already folded into its main body by its own "Double-Check Notes
(pre-full-review pass)" section (the ray-parameter-vs-raw-z test wording
fix, the new side-wall test, the `boxHalfExtents`/`FrustumProxy` field-order
callout, and the Phase-2-camera-vs-wider-far-cap clipping-risk callout). Read
`PHASE2_COMPLETION_REPORT.md` first, per the task instructions — it confirmed
Phase 2 landed exactly as scoped (the atmosphere-aware camera + elongated-box
framing) with no corrections that contradict this phase's own plan, so Phase
3 was implemented exactly as written in `PHASE3_FRUSTUM_SHAPED_RAYMARCH_PROXY.md`.

## Summary

**This mandatory deliverable is fully implemented and confirmed working.**
The Aerial Perspective volume's HTTP preview (`GET /get_texture?texture_name=
AtmosphereAerialPerspectiveVolume_GameView`/`..._SceneView`) now raymarches
against a literal, tapering `FrustumProxy` (new ray/half-space intersection
math, `IntersectRayFrustum()`/`MapFrustumLocalPositionToUvw()`) instead of
Phase 2's axis-aligned (even if depth-elongated) box — the rendered preview
genuinely **widens** from a narrow near end to a wide far end, matching the
reference diagram's fan-of-quads concept, not just "extends" along one axis
the way any axis-aligned box (no matter how stretched) necessarily would.
Every other, non-Aerial-Perspective volume texture is completely unaffected —
it still resolves through the original, byte-for-byte-unchanged
`IntersectRayBox()`/box `uvw` code path, confirmed both by code inspection
(the new branches are additive `if`/`else`s, never a replacement of the
existing path) and by a live runtime check of `GET /get_swapchain` and
`GET /list_textures` (see "Runtime verification" below).

**The far (wide) end is NOT visually/numerically clipped by the 256x256
output frame's edges** — this was the one explicitly-flagged risk in the
phase document (Phase 2's camera distance was sized against the OLD, narrower
box half-extents, not this phase's actual, wider far-cap corners). This
session verified it two ways: (1) visually, via a real `get_texture` capture
(see below) — the widened wedge shape sits comfortably inside the frame with
visible dark-background margin on every side, no flat cut-off edge anywhere
in the shape; and (2) numerically, by hand-deriving the camera-space
projection of every far-cap corner plus the near apex against the camera's
own `tanHalfFovY`/aspect (see "Numeric clipping verification" below) — the
single closest corner to the FOV boundary sits at **~92.8%** of the allowed
half-angle, comfortably inside it. This is flagged below as a real, measured
margin, not a wide one — a legitimate note for Phase 4's tuning pass, but NOT
an observed clipping defect this phase needs to fix.

## Implementation, step by step (matches the phase document's own Step 3 exactly)

### 3.1 — `VolumeTexturePreviewMath.h`: `FrustumProxy` + `IntersectRayFrustum()`/`MapFrustumLocalPositionToUvw()` declarations

Added verbatim, immediately after the existing `IntersectRayBox()`
declaration: the `FrustumProxy` struct (`halfDepth`/`farHalfWidth`/
`farHalfHeight`), `IntersectRayFrustum()`, and `MapFrustumLocalPositionToUvw()`
— plus `ComputeAtmosphereAerialPerspectivePreviewFrustum()` (Section 3.4),
declared non-static so `VolumeTexturePreviewRenderer.cpp` can call it.

### 3.2 — `VolumeTexturePreviewMath.cpp`: implementation

Added `#include <limits>` (for `std::numeric_limits<float>::infinity()`),
`HalfSpacePlane`/`ClipRayAgainstHalfSpace()` in a second anonymous namespace
(kept separate from the existing camera-constants namespace, matching the
phase document's own code layout), and `IntersectRayFrustum()`/
`MapFrustumLocalPositionToUvw()`/`ComputeAtmosphereAerialPerspectivePreviewFrustum()`
— all implemented verbatim from the phase document's own code blocks.

### 3.3 — Tier-1 tests (written before wiring into the renderer/shader)

Added all 7 tests specified in the phase document to
`tests/Renderer/VolumeTexturePreviewMathTests.cpp`, reusing the file's
existing `kEpsilon = 1e-4f`:

1. `IntersectRayFrustumAlongCentralAxisSpansFullDepth`
2. `IntersectRayFrustumSideWallEntryAndExitMatchHandComputedT`
3. `IntersectRayFrustumMissingRayReturnsFalse`
4. `IntersectRayFrustumRayFromInsideHasNonPositiveTEnter`
5. `MapFrustumLocalPositionToUvwApexMapsToCenterUAndV`
6. `MapFrustumLocalPositionToUvwFarCapCornersMapToUnitSquareCorners`
7. `MapFrustumLocalPositionToUvwWMatchesDepthFraction`

All 7 pass (see "Verification performed" below), alongside all 12
pre-existing tests in this file (19/19 total).

### 3.4 — `ComputeAtmosphereAerialPerspectivePreviewFrustum()`

Added exactly as specified — derives `FrustumProxy{halfDepth =
kAerialPreviewDepthHalfExtent, farHalfWidth = farHalfHeight =
kAerialPreviewXYHalfExtent * 2.0f}` from the SAME two constants Phase 2's
camera-setup function already uses for its (now-superseded, for the frustum
shape) `boxHalfExtents` — single source of truth for both.

### 3.5 — `VolumeTexturePreviewRenderer.h`/`.cpp`: threading the frustum through

`PushConstants` gained `std::int32_t shapeMode = 0;` at its true tail (after
`aerialPreviewExposure`), documented as `0 = box, 1 = frustum`. `boxHalfExtents`'s
own doc comment was updated in place to describe its Phase-3 dual-use
(`(farHalfWidth, farHalfHeight, halfDepth)` when `shapeMode == 1` — an
explicitly different field order than `FrustumProxy`'s own C++ struct
declaration order, called out both here and in the shader). In
`RenderPreview()`, when `interpretation ==
VolumeTexturePreviewInterpretation::AtmosphereAerialPerspective`, the code now
additionally sets `pushConstants.shapeMode = 1` and overwrites
`pushConstants.boxHalfExtents` with `Vec3(frustum.farHalfWidth,
frustum.farHalfHeight, frustum.halfDepth)` from
`ComputeAtmosphereAerialPerspectivePreviewFrustum()` — the camera's own
`eyePosition`/`forward`/`right`/`up`/`tanHalfFovY` from Phase 2's camera setup
function are left completely unchanged. The generic `GenericDensityInAlpha`
path leaves `shapeMode` at its default `0` and `boxHalfExtents` exactly as
`ComputeVolumeCameraSetup()` produced it — zero regression. Both the enum's
own doc comment and `RenderPreview()`'s own doc comment in
`VolumeTexturePreviewRenderer.h` were updated to mention the new shape
selection, mirroring Phase 2's own precedent for doc-comment upkeep.

### 3.6 — `VolumeTexturePreview.comp`: shader-side branch

Added `int shapeMode;` to the `layout(push_constant)` block (tail field,
matching the C++ struct's own new offset), plus a direct, hand-written GLSL
transcription of the CPU oracle: `HalfSpacePlane`/`ClipRayAgainstHalfSpace()`/
`IntersectRayFrustum()`/`MapFrustumLocalPositionToUvw()`, unpacking the reused
`pc.boxHalfExtents` as `farHalfWidth = .x, farHalfHeight = .y, halfDepth = .z`
exactly per the phase document's own explicit ordering reminder. One
deliberate implementation choice beyond what the phase document's own code
snippet spelled out: rather than relying on a literal `1.0/0.0` GLSL division
to seed `tEnter`/`tExit` at `+/-infinity` (which the pre-existing
`IntersectRayBox()` already does for its own slab method, where the division
is legitimately by a ray-direction component that can be zero), a plain,
explicit `const float kFloatMax = 3.402823466e+38;` sentinel is used instead
for `IntersectRayFrustum()`'s own initial `[tEnter, tExit]` range — this
avoids a literal divide-by-a-compile-time-constant-zero expression that some
GLSL compilers/validators warn on, while being numerically equivalent for
every value this feature's small, fixed-size proxy shapes could ever produce.
`main()` now branches on `pc.shapeMode` at both the intersection call site
(`IntersectRayFrustum()` vs. `IntersectRayBox()`) and the per-step `uvw`
derivation (`MapFrustumLocalPositionToUvw()` vs. the original
`(localPos / pc.boxHalfExtents) * 0.5 + 0.5` box formula), exactly as
specified.

### 3.7 — Zero regression to the generic path

Confirmed by code inspection (the `else`/`shapeMode == 0` branches at every
branch point are textually identical to the pre-Phase-3 unconditional code)
and, at runtime, by re-confirming `GET /get_swapchain` and `GET
/list_textures` still behave exactly as before (see "Runtime verification"
below) — the Aerial Perspective path itself is INTENTIONALLY and visibly
different now (that is this phase's entire purpose), never a regression to
guard against.

## Deviations from the strategy document

One minor, explicitly-justified implementation deviation (documented in
Section 3.6 above): using a `const float kFloatMax` sentinel instead of a
literal `1.0/0.0` GLSL division to seed `IntersectRayFrustum()`'s initial
`tEnter`/`tExit` range, for compiler-portability reasons. This is numerically
equivalent to the CPU oracle's `std::numeric_limits<float>::infinity()` for
every value this feature can ever produce (the frustum's own extents are on
the order of 1 unit, step counts are 64, so no ray parameter anywhere near
`3.4e38` is ever physically possible) and does not change
`IntersectRayFrustum()`'s observable behavior in any case exercised by this
phase's own Tier-1 tests or the runtime smoke check. No other deviation —
every other code block in the phase document (the C++ math, the
`PushConstants`/GLSL field-order plan, the Tier-1 test list, the renderer
wiring) was implemented verbatim.

## Verification performed

### Fast, targeted compile

- `cmake --build build --target GreatTamanaEngine` — succeeded. Rebuilt
  `VolumeTexturePreviewMath.cpp.obj`, `VolumeTexturePreviewRenderer.cpp.obj`,
  and (transitively) `Application.cpp.obj`; relinked `libgte_core.a`.
  **Confirmed `glslc` WAS invoked this time** (unlike Phase 2) — the build log
  shows `Compiling shader src/Shaders/VolumeTexturePreview.comp ->
  .../build/shaders/VolumeTexturePreview.comp.spv`, exactly as expected since
  this is the one phase in the whole campaign that touches a `.comp` file —
  followed by a full relink/shader-staging pass for `GreatTamanaEngine.exe`.
- `cmake --build build --target GreatTamanaEngineTests` — succeeded. Rebuilt
  `VolumeTexturePreviewMathTests.cpp.obj` and relinked the test binary.

### Targeted test run

`ctest -C Debug -R VolumeTexturePreviewMathTest --output-on-failure`:

```
100% tests passed out of 19
```

(12 pre-existing tests — including Phase 1's characterization test and
Phase 2's three camera/box tests — plus the 7 new Phase 3 tests, all
passing; every test completed in ~0.06s.)

### Runtime smoke check (real numbers/screenshots)

Built `GreatTamanaEngine.exe` (already built above), launched it via
`run_app_background` (PID 23120), and queried its embedded HTTP server:

- `GET /list_textures` — HTTP 200. Confirmed
  `AtmosphereAerialPerspectiveVolume_GameView`/`..._SceneView` are still live
  `"kind":"texture3d"`, `128x128x32` entries, unchanged from Phase 2.
- `GET /get_texture?texture_name=AtmosphereAerialPerspectiveVolume_GameView` —
  HTTP 200, `image/png`, 14430 bytes. **Visually**: a clearly WIDENING wedge
  shape — narrow near the top/center, fanning out wider toward the
  bottom-right — with the same bright-near/dark-far transmittance gradient
  Phase 2 already established, now genuinely reading as a receding, tapering
  frustum rather than Phase 2's uniformly-elongated parallelogram. This is an
  unambiguous, visible improvement over Phase 2's own "before" result and is
  clearly recognizable as "the same kind of thing" as the reference
  `aerial-persepective-lut-3d-texture.png` diagram's fan-of-quads concept.
- `GET /get_texture?texture_name=AtmosphereAerialPerspectiveVolume_SceneView` —
  HTTP 200, `image/png`, 14581 bytes — the same widening-wedge shape as the
  Game View capture (both camera-selecting call sites behave identically, as
  expected).
- `GET /get_swapchain` — HTTP 200, `image/png`, 105265 bytes — a normal, full
  Editor UI screenshot (Hierarchy/Scene/Inspector/Memory/Profiler/Render
  Graph/Atmosphere/Jobs/Project panels all visible, Scene view showing the
  expected atmosphere sky gradient), confirming the generic 2D capture path
  is completely unaffected by this phase's changes.
- `stop_app_background(pid: 23120)` — engine closed cleanly afterward.

### Far (wide) end clipping check — the phase document's own explicitly-flagged risk item

**Visual check**: the widened wedge shape sits with a clearly visible
dark-background margin on every side of the 256x256 frame — no flat,
straight-line cut-off anywhere along the shape's own silhouette (which is
what a genuine frame-edge clip would look like: a hard, axis-aligned cutoff
unrelated to the frustum's own tapering geometry).

**Numeric check** (performed by hand, using the exact real constants/formulas
this session's own code uses, not just eyeballing the PNG): the camera
distance from Phase 2's own `ComputeAtmosphereAerialPerspectivePreviewCameraSetup()`
is sized against the OLD box's bounding radius
(`Length((0.35, 0.35, 0.9)) ≈ 1.0271`), giving `distance ≈ 3.4534` and
`tanHalfFovY = tan(20°) ≈ 0.36397` (aspect = 1, since the output is
256x256 — horizontal and vertical FOV are identical). Projecting all five
"extreme" points of the NEW, WIDER frustum (the four far-cap corners
`(±0.7, ±0.7, 0.9)` plus the near apex `(0, 0, -0.9)`) into camera space
(`right/forward`, `up/forward` ratios, which must both stay within
`±tanHalfFovY` to remain inside the frame) gives:

| Point | right/forward | up/forward | Both within ±0.36397? |
|---|---|---|---|
| (+0.7, +0.7, 0.9) | 0.2902 | 0.1622 | Yes |
| (+0.7, -0.7, 0.9) | 0.2454 | **-0.3377** | Yes (92.8% of limit) |
| (-0.7, -0.7, 0.9) | 0.2569 | -0.1293 | Yes |
| (-0.7, +0.7, 0.9) | 0.2873 | 0.2195 | Yes |
| (0, 0, -0.9) (apex) | -0.2367 | 0.0198 | Yes |

The single closest point to the FOV boundary is the `(+0.7, -0.7, 0.9)`
far-cap corner, at **~92.8%** of the allowed half-angle (`0.3377 / 0.36397`)
— inside the frame, confirmed both visually and numerically, but with a
genuinely thin margin (~7% of the FOV half-angle), exactly as the phase
document's own risk callout predicted would be worth checking carefully.

**Conclusion: the far end is NOT clipped.** This is recorded here, per the
phase document's own instructions, as a **known, thin-margin follow-up for
Phase 4's tuning pass** — Phase 4 already owns both the camera-framing
constants and the frustum's own far-plane multiplier, and should treat
"increase this margin a bit for comfort" as a legitimate tuning goal
alongside its other visual-comparison-against-the-reference-image work, even
though nothing is actually broken today.

## Tool anomalies

Three `edit_line` calls during this session triggered the tool's own
documented auto-dedup safety net (removing a leftover boundary-duplicate line
immediately after an inserted block); a fourth `edit_line` call in this
session left behind a genuine duplicate that the auto-dedup safety net did
NOT catch (a full duplicated declaration line plus a duplicated closing
namespace brace in `VolumeTexturePreviewMath.h`, and a duplicated closing
namespace/brace pair in `VolumeTexturePreviewMathTests.cpp`) — both were
caught by this session's own follow-up full-file re-reads and corrected with
additional `edit_line` calls before compiling. All edited files were re-read
in full and confirmed structurally correct (matching brace counts, no
leftover duplicate lines) before every compile attempt in this session.

## Files changed

- `src/Renderer/VolumeTexturePreviewMath.h` (`FrustumProxy` struct,
  `IntersectRayFrustum()`/`MapFrustumLocalPositionToUvw()`/
  `ComputeAtmosphereAerialPerspectivePreviewFrustum()` declarations added)
- `src/Renderer/VolumeTexturePreviewMath.cpp` (`#include <limits>`,
  `HalfSpacePlane`/`ClipRayAgainstHalfSpace()`, and the three new function
  implementations added)
- `src/Renderer/VolumeTexturePreviewRenderer.h` (`PushConstants`'s doc comment
  is in `.cpp`; this file's `RenderPreview()`/enum doc comments updated to
  mention shape selection)
- `src/Renderer/VolumeTexturePreviewRenderer.cpp` (`PushConstants` gained
  `shapeMode`; `boxHalfExtents`'s doc comment updated for its dual-use;
  `RenderPreview()` now overrides `shapeMode`/`boxHalfExtents` for the Aerial
  Perspective interpretation)
- `src/Shaders/VolumeTexturePreview.comp` (`shapeMode` added to the
  push-constant block; `HalfSpacePlane`/`ClipRayAgainstHalfSpace()`/
  `IntersectRayFrustum()`/`MapFrustumLocalPositionToUvw()` GLSL functions
  added; `main()` branches on `pc.shapeMode` at both the intersection and
  `uvw`-derivation call sites)
- `tests/Renderer/VolumeTexturePreviewMathTests.cpp` (7 new tests added)
- `task_manager/atmosphere-scattering-3/PHASE3_COMPLETION_REPORT.md` (this
  file)

## Next step

Phase 4 (`PHASE4_VISUAL_TUNING_AGAINST_REFERENCE_IMAGE.md`) — the iterative
tuning pass against the reference image, which should treat the ~92.8%
FOV-boundary margin measured above as a legitimate (if not urgent) tuning
target alongside exposure/density/step-count/camera-angle/fan-factor
adjustments, since this phase's own frustum shape is now the FINAL,
production shape Phase 4 tunes against — not a fallback-to-box scenario.
