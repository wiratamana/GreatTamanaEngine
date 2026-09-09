# PHASE5_POLISH_AND_VERIFICATION — Tuning, Edge Cases, Final Regression

> Parent: `PHASE0_MASTER_STRATEGY.md`. Depends on: `PHASE4_RENDERGRAPH_INTEGRATION.md`
> (the grid must already be visibly rendering in "Scene" before this phase
> starts).

## Step 1: The Goal (Where are we going?)

Harden the feature against edge cases a first pass through Phase 1-4
deliberately deferred, tune the visual constants Phase 2 left as named,
adjustable values, and finish with a full clean regression build in every
CMake configuration this engine supports. Every step in this phase is a
real code change (constant tuning counts as a code change) or a real,
executed build/test command — never a "no code" step.

## Step 2: The Situation / The Problem (Where are we now?)

By the end of Phase 4, the grid is functionally complete and visible, but:

- Every visual constant (`kMinorCellSize`, `kMajorCellSize`,
  `kFadeDistance`, colors, alpha caps, axis line width) in
  `SceneGrid.frag` was chosen as a reasonable-but-unverified first guess in
  Phase 2, written before ever seeing it rendered.
- A few genuinely tricky camera configurations were called out in Phase
  1/2/3's own comments but never explicitly, deliberately tested against
  the real, running Editor: looking straight down (bird's-eye), looking
  straight up, extreme dolly-in (camera very close to the ground plane,
  where `nearPoint`/`farPoint` can be very close together and derivatives
  can spike), and extreme dolly-out (very large `kFadeDistance` multiples).
- `EditorCamera`'s own near/far clip planes
  (`src/Editor/EditorCamera.h`'s `m_camera.nearZ`/`farZ`, defaulted via
  `Camera`'s own defaults) bound how far `t <= 1.0` (Phase 1/2's own "beyond
  the far clip plane" rejection) lets the grid actually extend — if the
  default far plane is small relative to `kFadeDistance`, the grid could be
  clipped away by the far plane before it ever visually fades out via
  `kFadeDistance`, which would look like an abrupt "wall" instead of a
  smooth fade. This interaction must be checked with real numbers, not
  assumed.

## Step 3: The Plan

### 3.1 — Verify (and if needed, fix) the far-plane / fade-distance interaction

Read `src/ECS/Components/Camera.h`'s actual default `farZ` value and
`EditorCamera`'s own `m_camera` default-construction. Compare it against
`SceneGrid.frag`'s `kFadeDistance` constant (currently `100.0`, Phase 2's
own placeholder):

- If the camera's default far plane is comfortably larger than
  `kFadeDistance` (e.g. far plane is 1000+ world units), no code change is
  needed here — the grid will always fully fade out via `kFadeDistance`
  well before the far-plane clip ever becomes visible.
- If the far plane is smaller than or comparable to `kFadeDistance`,
  lower `kFadeDistance` in `SceneGrid.frag` (and re-derive
  `kAxisLineHalfWidthWorld`/LOD fade constants proportionally if needed) so
  the grid always finishes fading out comfortably before the far clip
  plane, never right at/past it. Document the final chosen value with an
  explicit comment citing the camera's actual far-plane value it was tuned
  against.

### 3.2 — Verify grazing-angle / near-the-ground camera behavior

With the engine running (`run_app_background`, then
`gte_send_request("/get_swapchain")` for visual checks, then
`stop_app_background` when done), in the "Scene" panel:

- Dolly the Editor camera down close to `y ≈ 0` and look nearly level with
  the horizon. Confirm the grid does not flicker/z-fight and fades smoothly
  toward the horizon rather than aliasing into a solid line-noise mess —
  if it does alias, increase the minor-grid LOD fade-out aggressiveness in
  `GridLineCoverage`'s caller (`SceneGrid.frag`'s `minorFade` computation —
  lower the `2.0` threshold in `clamp(2.0 - minorLod, 0.0, 1.0)`, e.g. to
  `1.5`, and re-check).
- Look straight down (bird's-eye). Confirm the grid renders correctly
  (a top-down view is actually the numerically SAFEST case for this
  algorithm — `rayDir.y` is at its largest magnitude here, farthest from
  the "parallel to plane" degenerate case) and that the axis lines still
  align with where a primitive placed at the world origin actually sits.
- Look straight up. Confirm the grid correctly disappears entirely (no
  plane in front of the camera in that direction) with no flicker/garbage
  pixels — this exercises the `t <= 0.0` rejection path in
  `SceneGrid.frag`.

### 3.3 — Confirm axis-line alignment against real geometry

Spawn a primitive (Hierarchy > "Create 3D Object" > Cube) at the world
origin via the Inspector's Transform fields (position `(0,0,0)`), and
another one at, say, `(5, 0, 0)`. Confirm:
- The cube at the origin sits exactly at the intersection of the red and
  blue axis lines.
- The cube at `(5, 0, 0)` sits exactly on the red (X-axis) line, 5 minor
  grid cells from the origin (countable by eye against the minor grid
  lines).

If either is visibly off, the bug is almost certainly in
`SceneGrid.frag`'s `AxisLineCoverage()` call sites (confirm `worldPos.z`
drives the X-axis line and `worldPos.x` drives the Z-axis line — a swapped
pair is the classic mistake here) — fix in `SceneGrid.frag`, then mirror the
exact same fix into `SceneGridMath.cpp`'s `ComputeAxisLineCoverage()` (see
`PHASE1_GRID_MATH_FOUNDATION.md`) and prove it with a
`tests/Editor/SceneGridMathTests.cpp` regression test, following the same
"CPU math is the spec, GLSL mirrors it" discipline
`PHASE1_GRID_MATH_FOUNDATION.md`'s own file comment establishes for
`ComputeGridPlaneHit()`/`ComputeGridLineCoverage()` — never patch
`SceneGrid.frag` in isolation and leave the CPU oracle silently out of date.
(`ComputeAxisLineCoverage()` was added to Phase 1's initial scope during
this campaign's own second-iteration strategy-document review, closing what
was originally a documented coverage gap here — this step now exists purely
to verify the two stay correct/in agreement, not to introduce the CPU
mirror for the first time.)

### 3.4 — Confirm zero effect on "Game" view / release build

- Switch to (or split open) the "Game" panel and confirm it shows no grid
  at all, ever — this should already be guaranteed structurally (the grid
  is only ever invoked from `AddSceneViewPass()`'s own new parameter, never
  `AddGameViewPass()`/`AddPresentPass()`), but confirm it visually as a
  final sanity check, not merely by code inspection.
- Full clean configure + build with `-DGTE_ENABLE_EDITOR=OFF` (see
  `BUILDING.md` for the exact CMake invocation this project uses) and
  confirm: it builds successfully, and grep the build output/binary for
  confirmation that `SceneGrid.vert.spv`/`SceneGrid.frag.spv` were never
  even compiled in this configuration (no `gte_add_shader` invocation for
  them fires, since that block itself is `if(GTE_ENABLE_EDITOR)`-gated —
  confirm no `shaders/SceneGrid.*.spv` file exists anywhere under that
  build's output directory).

### 3.5 — Full regression pass

- Full clean configure + build with the project's default configuration
  (`GTE_ENABLE_EDITOR=ON`, `GTE_ENABLE_PROJECT_PANEL=ON`).
- `cd /d <repo>\build && ctest -C Debug --output-on-failure` — confirm
  every pre-existing test still passes, plus the new
  `Editor/SceneGridMathTests.cpp` cases from Phase 1 (and any new
  axis-line-coverage tests added in Step 3.3 above, if that path was
  taken).
- Update `README.md`'s "Status" section with a short new bullet describing
  this feature (mirroring the terse, factual style every other "Status"
  entry already uses — see e.g. the existing Bone Viewer/Render Graph panel
  entries) — this is a documentation change, but it is a real, required
  code-adjacent artifact this campaign's own precedent (every other
  campaign in this repository updates `README.md`'s "Status" section as
  part of its own final phase) expects, not an optional nicety.

### 3.6 — Explicitly out of scope for this campaign (do not add speculatively)

- A user-facing on/off toggle for the grid (explicitly rejected — see
  `PHASE0_MASTER_STRATEGY.md`'s "Locked Design Decisions", item 2).
- Showing the grid in the "Game" view (explicitly rejected — item 4).
- Wiring the grid's draw call into `DrawStats`/the Profiler's per-pass
  draw-call/triangle counters, or into the "Render Graph" panel's own
  pass-snapshot table (the grid is not a declared RenderGraph pass at all
  — see `PHASE4_RENDERGRAPH_INTEGRATION.md`'s own explicit note in Step
  3.5) — a future phase could add this later via a real, reviewed design
  (e.g. threading `ctx.recordDraw` through `recordSceneOverlay`), but it is
  not required for this feature to be complete and should not be added
  without a fresh, explicit decision to do so.
- A configurable grid-cell-size/color Editor UI (e.g. an Inspector-style
  settings panel) — the constants in `SceneGrid.frag` are intentionally
  simple named constants for this campaign; exposing them as live-editable
  Editor settings is a natural, separate future enhancement, not part of
  this campaign's own confirmed scope.
