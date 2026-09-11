# PHASE4_COMPLETION_REPORT — Visual Tuning Against the Reference Image

Parent: `PHASE0_MASTER_STRATEGY.md`. Implements
`PHASE4_VISUAL_TUNING_AGAINST_REFERENCE_IMAGE.md` in full. Read
`PHASE3_COMPLETION_REPORT.md` first, per the task instructions — it confirmed
Phase 3's frustum-shaped proxy landed exactly as scoped, with one explicitly
flagged, non-blocking follow-up for this phase: the far-cap corner sat at only
~92.8% of the allowed FOV half-angle (a thin, but not clipping, margin), which
this phase treated as a real input, not a surprise, and fixed as part of its
own tuning loop (see Iteration 1 below).

## Summary

This phase ran a real, iterative "change one constant, rebuild, screenshot via
`GET /get_texture?texture_name=AtmosphereAerialPerspectiveVolume_GameView`,
compare against the reference PNG, adjust" loop against Phase 3's now-final,
shipped frustum proxy — never a fallback box. Starting from Phase 3's own
baseline capture (a widening wedge, correctly shaped but small, tucked into
the bottom-right corner of the 256x256 frame, with a fairly subtle near/far
color contrast), four single-constant iterations produced a clearly improved,
better-centered, better-margined, brighter-far-end result that a human/LLM
agent would recognize as "the same kind of thing" as the reference diagram's
receding, widening, progressively-hazier fan — the acceptance bar this phase's
own strategy document explicitly set (structural/conceptual similarity, never
literal pixel similarity, since the reference is a schematic, not a render).

No shader/logic/push-constant changes were made in this phase — every edit was
a numeric constant already introduced by Phase 2 (`VolumeTexturePreviewMath.cpp`)
or `atmosphere-scattering-2`'s Phase 4 (`VolumeTexturePreviewRenderer.cpp`),
exactly as required by the phase document's own Step 3.5 ("do not change
interpretation/shape LOGIC in this phase"). `kDensityScale`/`kStepCount`/the
frustum's own far-plane multiplier were considered (per the phase document's
own priority list) but left unchanged — the two constants actually tuned
(camera distance margin/angle, and exposure) were sufficient to reach a clearly
improved, legible result, and changing more than necessary was judged an
unjustified risk to the "zero regression to the generic/non-Aerial-Perspective
path" guarantee (`kDensityScale`/`kStepCount` are shared with that path, per
`VolumeTexturePreviewRenderer.h`'s own doc comments; the camera/frustum
constants touched here are exclusively read by the Aerial-Perspective-only
code path, so no such risk applied to them).

## Step 3.1 — Baseline (post Phase 2/3, pre Phase 4)

Launched the current build (`run_app_background`), requested
`GET /get_texture?texture_name=AtmosphereAerialPerspectiveVolume_GameView`, and
compared it against
`task_manager/atmosphere-scattering-3/aerial-persepective-lut-3d-texture.png`.
Concrete observations written down before any edit:

- **Correct shape, wrong framing.** The wedge genuinely widened (Phase 3's own
  achievement), but it sat small and tucked into the bottom-right corner of
  the frame, with most of the 256x256 canvas unused dark background — legible,
  but not as clear/confident a "receding volume" read as it could be.
- **Gradient present but subtle.** The near (apex) end was visibly darker than
  the far (wide) end, correctly matching "near = less haze/more transparent,
  far = more haze/brighter" — but the far end's own blue brightness was modest,
  not the bright, almost-glowing far-end blue/white the reference diagram's own
  quads show near the horizon.
- **Numerically explained by Phase 3's own flagged risk**: Phase 2's camera
  distance was sized against the OLD, narrower box's bounding radius
  (`Length((0.35, 0.35, 0.9))`), not Phase 3's actual, WIDER far-cap corners —
  this under-sized distance is what produced both the thin ~92.8% FOV margin
  Phase 3 measured AND (counter-intuitively) a shape that still looked small,
  because the asymmetric margin (tight in one diagonal, loose everywhere else)
  meant the whole shape had to be kept conservatively small/off-center to avoid
  clipping that one corner.

## Step 3.2/3.3 — Iteration log (one constant per iteration, rebuild + re-capture + compare each time)

### Iteration 1 — `kAerialPreviewDistanceMargin`: `1.15` -> `1.45`

**Why**: directly addresses Phase 3's own flagged ~92.8%-margin risk by pulling
the camera back proportionally more before framing the shot.
**Result**: the far-cap corner clipping risk was clearly resolved (ample dark
margin on every side in the new capture) — but the shape now read as
noticeably TOO SMALL within the frame, overcorrecting the original problem.
**Verdict**: right direction, wrong magnitude — kept iterating on this same
constant.

### Iteration 2 — `kAerialPreviewAzimuthDegrees`: `75` -> `85`, `kAerialPreviewElevationDegrees`: `18` -> `10`

**Why**: the baseline capture's own off-center (bottom-right-corner) framing
was a camera-ANGLE problem, not just a distance problem — at azimuth 75°/
elevation 18°, the camera's own "right" axis was not well-aligned with the
frustum's world-Z tapering axis, so the widening swept diagonally toward one
screen corner instead of sweeping left-to-right across the frame's horizontal
middle (the reference diagram's own composition). Moving azimuth closer to a
side-on 90° aligns the camera's "right" basis vector much more closely with
world +Z (hand-derived: at azimuth=90°/elevation=0°, `right` reduces to
`(0, 0, cosElevation)` exactly, i.e. pure world Z); 85°/10° keeps enough
off-axis tilt to still read as a 3D wedge rather than a flat, edge-on profile.
**Result**: the wedge now sweeps roughly horizontally (apex near one side,
far/wide cap near the other), noticeably better centered vertically and
horizontally than the baseline — a clear, visible improvement.
**Verdict**: kept.

### Iteration 3 — `kAerialPreviewDistanceMargin`: `1.45` -> `1.25`

**Why**: with the angle fixed (Iteration 2) and the margin risk already
resolved (Iteration 1's `1.45`), the shape was still visibly smaller than
necessary — `1.45` was more safety margin than needed once the angle change
also improved how evenly the far corners' margins are distributed. `1.25`
was chosen as a middle point between the original, too-tight `1.15` and the
overcorrected `1.45`.
**Result**: a visibly larger, better-filling wedge, with comfortable (but not
excessive) dark-background margin on every side — no clipping observed at any
edge in the capture.
**Verdict**: kept as the final camera-framing value.

### Iteration 4 — `kAerialPreviewExposure`: `2000.0` -> `3200.0`

**Why**: with framing settled, the remaining gap against the reference was the
far end's own brightness/legibility — the reference diagram's far quads read
as a bright, glowing near-white/blue haze, while the current capture's far cap
was a solid but comparatively modest blue.
**Result**: the far end now shows a clearly brighter, more "glowing" highlight
near its widest region, while the near (apex) end stays dark — the gradient
reads more confidently without visibly blowing out or flattening the
transition (checked directly against the capture; no washed-out/clipped-white
region appeared).
**Verdict**: kept as the final exposure value.

**Constants considered but deliberately left unchanged** (per Step 3.2's own
priority list and Step 3.5's "do not change more than necessary" spirit):
`kAerialPreviewFovYDegrees` (40°, unchanged — framing was already resolved via
distance margin + angle, a FOV change was not needed), `kStepCount` (64,
unchanged — no banding/stair-stepping artifacts were observed in any capture),
`kDensityScale` (4.0, unchanged — shared with the generic/non-Aerial-
Perspective interpretation path; the gradient was already clearly legible
after the exposure change, so touching a constant with a wider blast radius
was not justified), and the frustum's own far-plane multiplier (`kAerialPreviewXYHalfExtent
* 2.0f`, unchanged — the widening was already dramatic/legible and matched the
reference's own fan concept well).

## Step 3.4 — Final tuning table

| Constant | File | Old value | New value | Reason |
|---|---|---|---|---|
| `kAerialPreviewAzimuthDegrees` | `VolumeTexturePreviewMath.cpp` | `75.0f` | `85.0f` | Aligns the camera's own "right" basis vector much more closely with the frustum's world-Z tapering axis, so the near->far widening sweeps left-to-right across the frame (matching the reference diagram's own composition) instead of diagonally toward one corner. |
| `kAerialPreviewElevationDegrees` | `VolumeTexturePreviewMath.cpp` | `18.0f` | `10.0f` | Keeps just enough tilt for the shape to read as a 3D wedge (not a flat edge-on profile) while no longer skewing the widening sweep toward one corner. |
| `kAerialPreviewDistanceMargin` | `VolumeTexturePreviewMath.cpp` | `1.15f` | `1.25f` | `1.15` left Phase 3's own far-cap corner at a thin ~92.8% FOV-boundary margin (camera distance was sized against the OLD, narrower box, not the wider Phase-3 frustum); `1.45` fixed the margin but made the shape look too small; `1.25` is the empirically-chosen middle point — comfortable safety margin, no observed clipping, while still filling a legible portion of the 256x256 frame. |
| `kAerialPreviewExposure` | `VolumeTexturePreviewRenderer.cpp` | `2000.0f` | `3200.0f` | Makes the far (hazy) end of the frustum read as a brighter, more glowing blue - closer to the reference diagram's own bright far-end glow - without visibly blowing out or flattening the near-to-far gradient in any captured image. |

`kAerialPreviewFovYDegrees`, `kStepCount`, `kDensityScale`, and the frustum's
own far-plane multiplier were all evaluated against the phase document's own
priority list and left at their Phase 2/3 values — see "Constants considered
but deliberately left unchanged" above for why each one specifically.

## Step 3.5 — No new logic

Confirmed by diff review: every edit in this phase is a numeric constant value
change (plus its own updated/expanded doc comment recording the tuning
history, mirroring `AtmosphereSettings::aerialPerspectiveScatteringExaggeration`'s
own precedent) inside `VolumeTexturePreviewMath.cpp`'s anonymous namespace or
`VolumeTexturePreviewRenderer.cpp`'s anonymous namespace. No new function, no
new branch, no new push-constant field, and no shader file was touched (no
`.comp` recompile occurred at any point in this phase's iterations — every
`cmake --build` step only recompiled `VolumeTexturePreviewMath.cpp.obj` and/or
`VolumeTexturePreviewRenderer.cpp.obj`, confirmed by the build logs quoted
below). No new idea requiring a future follow-up campaign came up during
tuning — the loop converged cleanly within the four iterations above.

## Verification performed

### Fast, targeted compiles (one per iteration, `.cpp`-only, no shader recompile)

```
[1/3] Building CXX object CMakeFiles/gte_core.dir/src/Renderer/VolumeTexturePreviewMath.cpp.obj
[2/3] Linking CXX static library libgte_core.a
[3/3] Linking CXX executable GreatTamanaEngine.exe; ...
```
(Iterations 1-3, `VolumeTexturePreviewMath.cpp` only)

```
[1/3] Building CXX object CMakeFiles/gte_core.dir/src/Renderer/VolumeTexturePreviewRenderer.cpp.obj
[2/3] Linking CXX static library libgte_core.a
[3/3] Linking CXX executable GreatTamanaEngine.exe; ...
```
(Iteration 4, `VolumeTexturePreviewRenderer.cpp` only)

Every build log's own "Staging ... .spv" lines are pre-existing copy/staging
steps for ALREADY-COMPILED `.spv` files (no `glslc` invocation appears anywhere
in any of these four build logs) - confirming no shader was ever recompiled
during this phase, exactly as expected since `VolumeTexturePreview.comp` was
never touched.

### Targeted test run (`GreatTamanaEngineTests`, filtered)

Rebuilt `GreatTamanaEngineTests` once, after the final (Iteration 4) constant
change, and ran:

```
ctest -C Debug -R VolumeTexturePreviewMathTest --output-on-failure
...
100% tests passed out of 19
```

All 19 pre-existing tests (Phase 1's characterization test, Phase 2's three
camera/box tests, Phase 3's seven frustum tests, plus the original
`network-impl-6` tests) still pass unchanged - none of them hardcode a numeric
angle/distance/exposure value tied to the tuned constants; they all assert
relative/structural properties (orthonormal camera basis, eye strictly outside
the bounding sphere, depth-dominant axis, correct ray-parameter math) that stay
true regardless of the exact angle/distance chosen, so no test needed updating.

### Runtime verification (real numbers/screenshots, final state)

Launched `GreatTamanaEngine.exe` via `run_app_background` (final PID 17764)
and queried the live embedded HTTP server:

- `GET /list_textures` - HTTP 200. Confirmed
  `AtmosphereAerialPerspectiveVolume_GameView`/`..._SceneView` are still live
  `"kind":"texture3d"`, `128x128x32` entries; every OTHER texture (`AtmosphereTransmittanceLut`,
  `AtmosphereMultiScatteringLut`, `AtmosphereSkyViewLut_GameView`/`_SceneView`,
  `AtmosphereAerialPerspectiveVolumeDebugSlice`, `GameView`/`GameViewComposited`,
  `SceneView`/`SceneViewComposited`, `Swapchain`) is present and unaffected -
  none of this phase's tuned constants are read by any of those.
- `GET /get_texture?texture_name=AtmosphereAerialPerspectiveVolume_GameView` -
  HTTP 200, `image/png`, 11795 bytes. **Final visual**: a clearly widening
  wedge, better centered than the Phase 3 baseline, comfortable dark-background
  margin on every side (no clipping), with a visibly brighter, more "glowing"
  far (wide) end and a darker near (apex) end - a confident, legible near/far
  gradient recognizable as "the same kind of thing" as the reference diagram's
  own widening, progressively-hazier fan.
- `GET /get_texture?texture_name=AtmosphereAerialPerspectiveVolume_SceneView` -
  HTTP 200, `image/png`, 11892 bytes - the same improved framing/gradient as
  the Game View capture (both camera-selecting call sites behave identically).
- `GET /get_swapchain` - HTTP 200, `image/png`, 105265 bytes - a normal, full
  Editor UI screenshot (Hierarchy/Scene/Inspector/Memory/Profiler/Render
  Graph/Atmosphere/Jobs/Project panels all visible, Scene view showing the
  expected atmosphere sky gradient, unaffected by this phase) - re-confirming
  the generic 2D capture path is completely unaffected.
- **Generic (non-Aerial-Perspective) volume path re-confirmation**: per the
  phase document's own Verification section, this is a code-inspection-only
  re-confirmation (there is no other real volume texture in this engine today
  to request live) - `ComputeVolumeCameraSetup()`/`IntersectRayBox()` and every
  constant in `VolumeTexturePreviewMath.cpp`'s FIRST anonymous-namespace block
  (`kAzimuthDegrees`/`kElevationDegrees`/`kFovYDegrees`/`kDistanceMargin`) were
  not touched by any edit in this phase - only the SECOND, Aerial-Perspective-
  only block's constants were changed. `kDensityScale`/`kStepCount` (shared
  with the generic path) were considered but deliberately left unchanged (see
  "Constants considered but deliberately left unchanged" above) specifically
  to avoid any risk to this guarantee.
- `stop_app_background(pid: 17764)` - engine closed cleanly afterward.

## Deviations from the strategy document

None. Every step (baseline capture, one-constant-at-a-time iteration,
rebuild + re-capture + compare loop, final tuning table with old -> new ->
reason, no new logic/branches/push-constant fields, no shader touched, no
new HTTP endpoint/query parameter) was followed exactly as
`PHASE4_VISUAL_TUNING_AGAINST_REFERENCE_IMAGE.md` specifies. The number of
iterations (4) fell within the phase document's own explicit acknowledgment
that "the exact number of edit/rebuild/screenshot cycles cannot be predicted
in advance" - the loop was judged converged once the result was a clear,
legible improvement over the Phase 2/3 baseline and a reasonable structural
match to the reference image, per the phase document's own acceptance bar.

## Files changed

- `src/Renderer/VolumeTexturePreviewMath.cpp` (`kAerialPreviewAzimuthDegrees`/
  `kAerialPreviewElevationDegrees`/`kAerialPreviewDistanceMargin` constant
  values changed, each with an updated doc comment recording the tuning
  history)
- `src/Renderer/VolumeTexturePreviewRenderer.cpp` (`kAerialPreviewExposure`
  constant value changed, with an updated doc comment recording the tuning
  history)
- `task_manager/atmosphere-scattering-3/PHASE4_COMPLETION_REPORT.md` (this
  file)

## Next step

Phase 5 (`PHASE5_DOCS_REGRESSION_SAFETY_AND_FINAL_BUILD.md`) - `AGENTS.md`/
`README.md` updates, an explicit zero-regression confirmation, one full
incremental build + the full existing test suite, and the campaign completion
report.
