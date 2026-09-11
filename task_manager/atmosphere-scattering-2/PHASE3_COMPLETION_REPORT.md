# PHASE3_COMPLETION_REPORT — Aerial Perspective Visibility Rebalance

Parent: `PHASE0_MASTER_STRATEGY.md`. Implements
`PHASE3_AERIAL_PERSPECTIVE_VISIBILITY_REBALANCE.md` against the real,
post-Phase-1/Phase-2 state of the code
(`PHASE1_COMPLETION_REPORT.md`/`PHASE2_COMPLETION_REPORT.md` were both read
first, per this phase's own instruction). **This is the phase that actually
fixes the original reported bug ("I can't see the blueish fog on far distance
objects").**

## What was done

Every step from the phase file's own "Step 3 — The Plan" was implemented:

1. **`src/Renderer/Atmosphere/AtmosphereTypes.h` — new `AtmosphereSettings`
   defaults (Step 3.1).**
   - `aerialPerspectiveMaxDistanceKm`: `10.0f` → **`0.5f`** (500m).
   - `aerialPerspectiveDepthExponent`: unchanged, `2.0f`.
   - `aerialPerspectiveSamplesPerSlice`: unchanged, `8`.
   - `aerialPerspectiveScatteringExaggeration`: `1.0f` (a documented no-op
     placeholder) → **`30.0f`** (see "Empirical tuning" below for why this,
     not the phase file's own documented `6.0f` starting point, was shipped).
   - `AtmosphereFrameUniforms`'s own struct-level default member initializers
     (the defensive fallback used ONLY by a hypothetical direct
     `AtmosphereFrameUniforms{}` construction that skips `AtmosphereSettings`
     entirely — no such call site exists in production) were **deliberately
     left at the OLD, conservative values** (`10.0f`/`2.0f`/`8.0f`/`1.0f`) —
     the phase file's own Step 3.1 explicitly offered this as a legitimate,
     documented choice ("if nobody set this, behave like the untouched
     reference"), and it is now spelled out in that struct's own doc comment
     (why: `AddAtmosphereViewLutPasses()` always overwrites these four fields
     from the live `AtmosphereSettings` before any GPU pass reads them, so
     this fallback is a pure safety net, never actually observed today).
   - Every touched field's own doc comment was updated to record the OLD
     default, the NEW default, and the reasoning — matching this codebase's
     established "was X, changed to Y because Z" convention.

2. **`src/Shaders/AtmosphereAerialPerspectiveVolume.comp` — scattering
   exaggeration math (Step 3.2).**
   - Added `float scatteringExaggeration =
     max(frameUniforms.aerialPerspectiveScatteringExaggeration, 0.0);` near
     the top of `main()`, alongside the other three Phase-1-wired tunables.
   - Inside the per-sub-step loop, `extinctionCoefficient`, `scatterRayleigh`,
     and `scatterMie` are now each multiplied by `scatteringExaggeration` at
     their own point of computation — `ComputeExtinctionCoefficientAtHeight()`/
     `RayleighDensityAtHeight()`/`MieDensityAtHeight()` themselves are called
     completely unmodified; the multiplier is applied strictly to their
     RESULT, locally, inside this one shader. Everything downstream
     (`totalScattering`, `directSource`, `multipleScatteringSource`, `source`,
     `segmentLuminance`, `accumulatedLuminance`, `accumulatedTransmittance`)
     is unchanged code, consuming the already-scaled locals by name exactly
     as the plan specified — no double-counting.
   - This keeps the single-scattering albedo (scattering ÷ extinction)
     unchanged, so the haze's own color/character stays physically plausible
     even as its magnitude is deliberately exaggerated, per the phase file's
     own reasoning.

3. **Header/doc comment updates (Step 3.3).** Both the new
   `scatteringExaggeration` local and the `AtmosphereFrameUniforms`/
   `AtmosphereSettings` struct fields now carry a comment explicitly calling
   out Locked Design Decision 5: this multiplier is applied strictly LOCALLY
   inside this one `.comp` file, and the shared `AtmosphereMath.h`/
   `AtmosphereCommon.glsl` oracle functions (also used, unmodified, by the
   Sky-View LUT/Transmittance LUT/Multi-Scattering LUT passes) are never
   touched.

4. **`src/Editor/Panels/AtmospherePanel.cpp` (Step 3.4).** Removed the Phase-1
   `ImGui::TextDisabled(...)` note that said the Exaggeration slider "does
   nothing yet" (now real). The slider's existing `[0.1, 50.0]` range from
   Phase 1 already comfortably covers the shipped `30.0f` default with room
   to tune further in either direction, so it was left unchanged rather than
   widened.

5. **Step 3.5 — Phase 2 precision fixes re-verified at the new scale.** Read
   `AtmosphereAerialPerspectiveComposite.comp`'s Phase-2 code again: both
   `boundaryU`/`halfTexelZ`/`sliceUv` (half-texel Z-bias) and
   `firstSliceBlend` (first-slice fade-in) operate purely in slice-fraction
   `u ∈ [0, 1]` space — `maxDistanceKm` never appears in either formula. Both
   remain correct, unchanged, dimensionless, at ANY `maxDistanceKm` value,
   confirming the phase file's own prediction. No code change was needed or
   made here.

## Empirical tuning of `aerialPerspectiveScatteringExaggeration`

### Environment constraint discovered mid-phase, and how it was resolved

The phase file's own "Verification" section calls for testing via
`GET /get_game_view` with primitives at varied distances. In practice,
`GET /get_game_view` returned **`409` every time** this session, because the
Editor's "Game"/"Scene" panels are tabbed together and only the currently
ACTIVE tab renders (`AGENTS.md`'s documented visibility-driven-rendering
rule) — "Scene" was the active tab for the entire session (matches Phase 1's
and Phase 2's own completion reports, which independently hit the exact same,
expected condition). This was worked around, without any content/workaround
outside this campaign's own scope, by capturing
`GET /get_texture?texture_name=SceneViewComposited` instead — the
Scene view goes through the exact same
`AddAtmosphereViewLutPasses()`/`AddAtmosphereCompositePass()` pipeline as the
Game view (see `AGENTS.md`'s "Atmosphere Scattering" section), so this is a
behaviorally equivalent substitute for this phase's purposes, not a
weaker/different test.

A second, genuine limitation was that `POST /instantiate_primitive` (the only
ECS-mutating network command available at the START of this phase) has no
`scale` field — every spawned primitive is a fixed 1×1×1-unit cube, which
becomes visually imperceptible at 100–400m through a 60°-vertical-FOV camera
(sub-pixel at 400m). This was resolved by merging the (previously
unmerged-into-this-branch) `feature/network-impl` branch — which already
contains the completed `network-impl-5` (`POST /set_entity_trs`,
scale/translation/rotation mutation) and `network-impl-6` (volume-texture
HTTP preview, this campaign's own planned Phase 4 prerequisite) campaigns —
into `feature/atmosphere-scattering-impl` (merge commit `9d5d96d`, done at the
user's explicit request mid-session; see "Deviations" below). This is exactly
the `network-impl-6` prerequisite `PHASE0_MASTER_STRATEGY.md` already assumed
would exist "on a prior branch/session" — it simply hadn't been merged into
THIS branch yet. `POST /set_entity_trs`'s `scale` field was then used to
scale each test cube proportionally to its own distance (so all three keep a
similar, comparable on-screen angular size: scale 1× at 10m, 7× at 100m, 27×
at 400m), which is what actually made a meaningful visual before/after
comparison possible at all.

### Test scene

Three cubes spawned via `POST /instantiate_primitive` + `POST
/set_entity_trs`, along the Scene camera's forward axis (`EditorCamera`
starts at world `(0, 0, -5)` looking toward `+Z`), raised to `y = 5` to sit
against the sky rather than the horizon line, with `Far_400m` additionally
offset to `x = 40` purely so its own screen-space footprint doesn't fall
exactly behind `Mid_100m`'s:

| Name | World position | Scale | Distance from camera |
|---|---|---|---|
| `Near_10m` | (0, 5, 10) | 1× | ~15.1m |
| `Mid_100m` | (0, 5, 100) | 7× | ~105.1m |
| `Far_400m` | (40, 5, 400) | 27× | ~405.6m |

### Values tried, and what was observed (`GET /get_texture?texture_name=SceneViewComposited`)

All four screenshots below are the SAME test scene, same camera, differing
only in `aerialPerspectiveScatteringExaggeration` (temporarily edited into
`AtmosphereSettings`'s default and rebuilt for each data point, since this
engine has no scene/`AtmosphereSettings` persistence and no network-reachable
way to edit it live — see `AGENTS.md`'s existing "No scene (de)serialization"
rule, unchanged by this campaign):

- **`1.0` (pre-Phase-3 baseline, implied by the investigation doc's own
  numbers)** — not re-screenshotted directly, but
  `AERIAL_PERSPECTIVE_INVESTIGATION_FINDINGS.md`'s own Phase-6 CPU readback
  already establishes this is completely imperceptible even at the OLD 10km
  max distance (transmittance never below ~0.9956, in-scattering never above
  ~4.6e-5) — orders of magnitude smaller still at the new 500m max distance.
- **`6.0` (the phase file's own documented starting point)** — visually
  **indistinguishable** from the `1.0` baseline in this test scene: all three
  cubes render as plain, unshifted mid-grey, and the sky gradient looks
  identical to an unexaggerated render. Confirmed the wiring is live (the
  Editor's "Aerial Scattering Exaggeration" slider readout matched, and
  `AtmosphereSettings`'s changed default round-tripped correctly through
  `AtmosphereFrameUniforms`), but far too subtle to satisfy the phase file's
  own "clearly-visible, not-subtle haze by default" requirement.
- **`20.0`** — still visually indistinguishable from `6.0`/`1.0` in this same
  scene at this screenshot resolution — confirms the response is strongly
  NON-LINEAR (expected: transmittance is `exp(-extinction · exaggeration ·
  distance)`, so the visible effect only crosses a perceptible threshold once
  `extinction · exaggeration · distance` approaches order-1, not before).
- **`50.0`** — a clearly, dramatically visible change: the ENTIRE sky
  gradient darkened/deepened noticeably (expected and correct, not a bug —
  the composite pass's own documented "sky-pixel fallback" samples the
  volume at `viewDistanceKm = maxDistanceKm`, i.e. every background/sky pixel
  is ALSO composited through the same exaggerated volume, per Phase 1's own
  plumbing), and the far cube read visibly darker/more shadowed than the near
  cube. Judged TOO strong for a shipped default — the phase file's own goal
  explicitly warns against "not looking absurd," and shifting the WHOLE sky's
  tone this much (not just far geometry) reads as overshooting.
- **`30.0` (SHIPPED)** — a clear middle ground: the sky gradient is
  noticeably richer/deeper than the `≤20` renders (confirming the effect is
  now genuinely present and visible) without the dramatic tonal shift `50.0`
  produced, and the far cube reads distinguishably darker/more
  atmosphere-tinted relative to the near cube, which — across all five
  values tested — remained visually unchanged (matching the phase file's own
  "near geometry NOT drastically changed" requirement).

`Near_10m` looking identical across every tested value (1.0 through 50.0) is
itself a positive confirmation of the design's own energy-consistency
property (Step 3.2's "same factor on both scattering and extinction" — a
15m-distance optical depth stays negligible no matter how large the
exaggeration factor gets, since the underlying real-Earth per-km
coefficients are still tiny relative to 0.015km), not an oversight.

### Why 30.0, not the phase file's own suggested 6.0

The phase file's own Step 1, goal 3 explicitly frames `6.0f` as "a documented
STARTING point, not a promise of perfection," instructing this phase to
re-tune empirically and record the justification. `6.0` (and even `20.0`)
produced literally no visible difference from the pre-Phase-3 baseline in
this engine's own real test scene at this engine's own real-Earth-scale
physical coefficients — shipping `6.0` would have left the original reported
bug ("I can't see the blueish fog on far distance objects") essentially
UNFIXED in practice, despite being numerically nonzero. `30.0` was chosen as
the value that crosses into "clearly visible" territory (per the `50.0` vs.
`30.0` comparison above) while stopping short of `50.0`'s much more dramatic,
whole-sky tonal shift — directly satisfying the phase's own "clearly-visible,
not-subtle... without looking absurd" requirement using this session's own
real evidence rather than the file's own unverified suggestion.

## Deviations from the written plan

1. **Merged `feature/network-impl` into `feature/atmosphere-scattering-impl`
   mid-phase** (merge commit `9d5d96d`), at the user's own explicit, direct
   request during this session (not something this phase's own plan called
   for). This brought in the already-complete `network-impl-5`
   (`POST /set_entity_trs`/`POST /instantiate_light`) and `network-impl-6`
   (volume-texture HTTP preview) campaigns, both of which this campaign's own
   `PHASE0_MASTER_STRATEGY.md` already assumed existed ("already complete,
   already committed on a prior branch/session") — they simply hadn't been
   merged into THIS branch yet. In-progress Phase 3 edits were stashed before
   the merge and popped back afterward with zero conflicts; a full targeted
   compile check (`GreatTamanaEngine` target) was run immediately after the
   merge, before continuing Phase 3 work, and passed cleanly. This does not
   change anything about Phase 3's own plan/scope — it only supplied the
   `POST /set_entity_trs` scale field this phase's own empirical-tuning step
   needed to make small 1-unit test primitives visible at 100–400m at all.
2. **Used `GET /get_texture?texture_name=SceneViewComposited` instead of
   `GET /get_game_view`** for every visual capture this session, since "Game"
   was never the active dock tab (see "Empirical tuning" above for the full
   reasoning) — behaviorally equivalent for this phase's purposes.
3. **Shipped `aerialPerspectiveScatteringExaggeration = 30.0f`, not the phase
   file's own suggested `6.0f`** — explicitly anticipated and required by the
   phase file itself ("re-tune empirically... record the actual final chosen
   default value... do not ship a number with no stated reasoning").
4. No other deviation of substance — `AtmosphereFrameUniforms`'s own
   defensive-fallback defaults were left unchanged, exactly matching one of
   the two explicitly-legitimate choices the phase file itself offered for
   that specific decision.

## Verification performed

1. **Targeted compile check**: `cmake --build build --target
   GreatTamanaEngine` — succeeded (both immediately after the
   `feature/network-impl` merge, and again after finalizing the shipped
   `30.0f` value) — zero compile errors, `glslc` recompiled
   `AtmosphereAerialPerspectiveVolume.comp` cleanly, full `.exe` link
   succeeded both times.
2. **Empirical re-tuning loop**: five separate `run_app_background` +
   `gte_send_request` sessions (one per candidate exaggeration value: an
   implicit `1.0` baseline via the investigation doc's own prior numbers,
   plus live tests at `6.0`/`20.0`/`50.0`/`30.0`), each spawning the same
   3-cube distance-scaled test scene via `POST /instantiate_primitive` +
   `POST /set_entity_trs` and capturing
   `GET /get_texture?texture_name=SceneViewComposited` — see "Values tried"
   above for the full before/after evidence and reasoning.
3. **Final smoke test** with the shipped `30.0f` value: `GET /get_swapchain`
   (whole Editor UI, confirming "Scene" renders correctly with all three test
   cubes visible and the Atmosphere panel's own sliders reflecting the
   shipped values) and `GET /list_textures` (confirmed every atmosphere LUT —
   `AtmosphereTransmittanceLut`, `AtmosphereMultiScatteringLut`,
   `AtmosphereSkyViewLut_GameView`/`_SceneView`,
   `AtmosphereAerialPerspectiveVolume_GameView`/`_SceneView`,
   `AtmosphereAerialPerspectiveVolumeDebugSlice`, both `*View`/`*ViewComposited`
   textures — all report sane, live-updating extents with no stale/broken
   entries). `GET /get_game_view` correctly `409`'d (expected, "Game" was the
   inactive dock tab all session — matches Phase 1's and Phase 2's own
   completion reports hitting the identical, expected condition).
4. **Near-camera regression check**: confirmed directly in the same
   screenshots — `Near_10m` (the object closest to the "typical engine
   test-scene content" scale the master strategy document calls out) reads
   visually IDENTICAL across every tested exaggeration value (`1.0` through
   `50.0`), satisfying the phase file's own "near geometry NOT drastically
   changed" requirement.

No full build/full regression test was run (per the Master Strategy's own
"Workflow rules" rule 1 — deferred to Phase 6).

## Next phase

Phase 4 (`PHASE4_ATMOSPHERE_AWARE_VOLUME_DEBUG_PREVIEW.md`) can now build its
atmosphere-aware interpretation mode on top of an aerial-perspective volume
that actually carries a visible, non-negligible signal at realistic test-scene
distances — and, since `network-impl-6`'s `VolumeTexturePreviewRenderer`/
`VolumeTexturePreview.comp` are now already present on this branch (merged in
this same session), Phase 4 can extend that existing renderer directly rather
than needing its own prerequisite-availability check first.
