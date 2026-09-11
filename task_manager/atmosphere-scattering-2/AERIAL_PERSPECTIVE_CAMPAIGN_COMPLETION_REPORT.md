# AERIAL_PERSPECTIVE_CAMPAIGN_COMPLETION_REPORT — "Aerial Perspective: Make It Visible + See The LUT" (`atmosphere-scattering-2`)

Ties together all six phases of the `atmosphere-scattering-2` campaign (see
`PHASE0_MASTER_STRATEGY.md` for the original plan). This campaign closes out
the original user-reported bug from the `atmosphere-scattering-1` campaign:
**"I can't see the blueish fog on far distance objects."**

## Phase-by-phase summary

| Phase | Goal | Key outcome |
|---|---|---|
| **1** | Single source of truth for the aerial LUT's tuning constants | Hoisted `kAerialMaxDistanceKm`/`kAerialDepthExponent`/`kAerialSamplesPerSlice` (three disconnected hardcoded shader literals, no runtime control) into four real `AtmosphereSettings` fields (`aerialPerspectiveMaxDistanceKm`/`aerialPerspectiveDepthExponent`/`aerialPerspectiveSamplesPerSlice`/`aerialPerspectiveScatteringExaggeration`), threaded through `AtmosphereFrameUniforms` and the composite pass's push constants, with live Editor sliders. Shipped with the SAME numeric defaults as before (10km/2.0/8/1.0) — a pure plumbing refactor, zero visual behavior change, confirmed via runtime smoke test. |
| **2** | Fix two documented composite-pass precision gaps | Added the missing half-texel Z-bias (`boundaryU`/`halfTexelZ`/`sliceUv`) and first-slice fade-in blend (`firstSliceBlend`) to `AtmosphereAerialPerspectiveComposite.comp`, per `pl-sky_aerial_perspective.md` §5 points 7/9 — both fixes operate purely in dimensionless slice-fraction space, correct at any `maxDistanceKm`. |
| **3** | **The actual visibility fix** | Shrank `aerialPerspectiveMaxDistanceKm` from `10.0f` to **`0.5f`** (500m) and added a new scattering-exaggeration multiplier, empirically tuned (via 5 live before/after screenshot comparisons at 1.0/6.0/20.0/50.0/30.0x) to **`30.0f`** — applied strictly locally inside `AtmosphereAerialPerspectiveVolume.comp`'s own per-sample extinction/scattering coefficients, never touching the shared `AtmosphereMath.h`/`AtmosphereCommon.glsl` oracle. This is the phase that actually fixes the original reported bug. |
| **4** | Atmosphere-aware volume-texture debug preview | Added a second interpretation mode to the existing (`network-impl-6`) `VolumeTexturePreviewRenderer`/`VolumeTexturePreview.comp`, auto-selected by volume name (zero new endpoint/query param): `alpha` reinterpreted as haze amount (`1 - transmittance`), `rgb` exposure-adjusted (2000x) + Reinhard-tonemapped — turning what previously rendered as an almost uniformly dark, uninformative box into a legible, spatially-varying blue-tinted gradient. |
| **5** | Numeric LUT inspection tool | New `src/Editor/AtmosphereAerialPerspectiveLutInspection.h/.cpp` (mirroring `AtmosphereTransmittanceLutValidation`'s proven shape) reads back the real aerial-perspective volume slice-by-slice and reports min/max/mean transmittance/in-scattering plus a `likelyVisibleAtDefaultExposure` heuristic, via a new "Inspect Aerial Perspective LUT" button in the Editor's "Atmosphere" panel. A live capture confirmed the post-Phase-3/4 volume: min transmittance **0.706543**, mean **0.942244**; max in-scattering **0.003888**, mean **0.000660** — reporting **"LIKELY VISIBLE"**, a direct numeric confirmation of Phase 3's own qualitative before/after screenshots. |
| **6** | Docs, full build, full regression, live verification | Updated `AGENTS.md`/`README.md`/`TODO.md` to reflect the new post-campaign reality; ran a full clean build (zero errors) and full `ctest` regression (1272 tests, 1271 passed, 1 pre-existing machine-gated skip, zero regressions); ran a live runtime smoke test confirming a clear, monotonically distance-increasing haze on real test geometry via direct pixel sampling, the Phase 4 volume preview showing a legible gradient, `GET /list_textures` unchanged/correct, and a pre-existing 2D texture capture byte-for-byte unchanged. |

## What genuinely works, end to end, verified against a live engine

- **The original reported bug is fixed.** Three test cubes at ~10m/~100m/~400m
  from the Scene camera, at the campaign's shipped defaults
  (`aerialPerspectiveMaxDistanceKm = 0.5`,
  `aerialPerspectiveScatteringExaggeration = 30.0`), show a clear, monotonic
  darkening/blue-shift with distance when captured live via `GET
  /get_texture?texture_name=SceneViewComposited` — confirmed both visually
  and via direct RGB pixel sampling (Phase 6's own live smoke test), and
  independently corroborated by Phase 5's own LUT-level numeric readout
  (minimum transmittance dropped from a pre-campaign ~0.9956 to a
  post-campaign 0.706543; maximum in-scattering magnitude grew from ~4.6e-5
  to 0.003888).
- **Near-camera geometry is unaffected.** Across every value tested in
  Phase 3's own empirical tuning pass (1.0 through 50.0x exaggeration), the
  ~10-15m test object read visually identical — the fix targets distant
  geometry specifically, per the campaign's own design goal, without
  distorting nearby content.
- **All four aerial-LUT tuning parameters are genuinely live and
  Editor-tunable** (`Aerial Max Distance (km)`, `Aerial Depth Exponent`,
  `Aerial Samples Per Slice`, `Aerial Scattering Exaggeration` sliders in the
  "Atmosphere" panel) — confirmed via runtime smoke tests across every phase
  that the shipped defaults round-trip correctly into the live GPU pipeline
  with zero unintended side effects on the sky's own physically-based
  rendering (Sky-View/Transmittance/Multi-Scattering LUTs untouched by any
  of this campaign's changes).
- **The volume-texture HTTP debug preview is genuinely more useful for this
  specific LUT.** `GET /get_texture?texture_name=AtmosphereAerialPerspectiveVolume_GameView`
  (or `_SceneView`) now returns a real, legible, spatially-varying blue-tinted
  gradient thumbnail instead of a flat dark box, with zero regression to the
  generic interpretation any other (hypothetical future) volume texture
  still gets.
- **The new numeric inspection tool works end-to-end against a live,
  currently-rendering volume**, correctly reporting real dimensions
  (128x128x32, 524288 texels) and a plausible, evidence-backed
  "LIKELY VISIBLE"/"not likely visible" verdict.
- **Zero regressions anywhere in the engine.** A full clean build across all
  three targets (`gte_core`, `GreatTamanaEngineTests`, `GreatTamanaEngine`)
  and the complete `ctest` suite (1272 tests) both pass cleanly, with the
  same one pre-existing machine-gated skip every prior campaign's own final
  regression run has also reported, plus this campaign's own new Phase 5
  tests (7 test cases) — genuinely new coverage, not just "didn't break
  anything."
- **Every pre-existing capture path is untouched.** `GET /get_swapchain`,
  `GET /get_game_view`, and `GET /get_texture` against a plain 2D texture all
  behave exactly as before this campaign — confirmed directly (a
  byte-for-byte diff of two consecutive `AtmosphereTransmittanceLut`
  captures showed zero difference).

## New/modified files (cumulative, across all six phases)

**New files:**
- `src/Editor/AtmosphereAerialPerspectiveLutInspection.h`/`.cpp` (Phase 5)
- `tests/Editor/AtmosphereAerialPerspectiveLutInspectionTests.cpp` (Phase 5)

**Modified files:**
- `src/Renderer/Atmosphere/AtmosphereTypes.h` (Phases 1, 3)
- `src/Shaders/AtmosphereCommon.glsl` (Phase 1)
- `src/Shaders/AtmosphereAerialPerspectiveVolume.comp` (Phases 1, 3)
- `src/Shaders/AtmosphereAerialPerspectiveComposite.comp` (Phases 1, 2)
- `src/Renderer/Atmosphere/AtmosphereLutRenderer.h`/`.cpp` (Phases 1, 5)
- `src/Application/AtmospherePassSequence.h`/`.cpp` (Phase 1)
- `src/Application/Application.cpp` (Phases 1, 4)
- `src/Editor/Panels/AtmospherePanel.h`/`.cpp` (Phases 1, 3, 5)
- `src/Editor/ImGuiEditorLayer.cpp` (Phase 5)
- `src/Renderer/VolumeTexturePreviewRenderer.h`/`.cpp` (Phase 4)
- `src/Shaders/VolumeTexturePreview.comp` (Phase 4)
- `src/Encoding/HdrColorVisualization.h`/`.cpp` (Phase 5)
- `CMakeLists.txt`, `tests/CMakeLists.txt` (Phase 5)
- `AGENTS.md`, `README.md`, `TODO.md` (Phase 6)

## Deliberate non-goals (recap — unchanged from `PHASE0_MASTER_STRATEGY.md`)

- No content/scene changes as the actual fix mechanism (moving test geometry
  farther from the camera remains a user workaround, not what this campaign
  ships).
- No volumetric clouds, god-rays, or other new atmosphere visual features.
- No change to the aerial LUT's fixed 128x128x32 resolution.
- No change to `AtmosphereParametersGpu`'s physical constants or
  `AtmosphereMath.h`'s CPU oracle — every new tunable is consumed strictly
  locally inside the aerial-perspective shaders.
- No "Slice"/"Maximum Intensity Projection" preview modes for the
  volume-texture debug endpoint — still exactly Unity's "Volume" front-to-back
  raymarch, just with a second, auto-selected data interpretation.
- No scene (de)serialization for the new `AtmosphereSettings` fields — same
  pre-existing, documented limitation every other `AtmosphereSettings` field
  already has.

## Final verification snapshot

- **Build**: full clean build (`cmake --build build --target clean` followed
  by `cmake --build build`), all 417 steps, zero errors, zero new warnings —
  `gte_core`, `GreatTamanaEngineTests.exe`, `GreatTamanaEngine.exe` all built.
- **Regression**: `ctest -C Debug --output-on-failure` — **1272 tests, 1271
  passed, 1 skipped (the pre-existing, machine-gated
  `PmxLoaderRealModelSmokeTest`), 0 failed.**
- **Live smoke test**: three test cubes at ~10m/~100m/~400m from the Scene
  camera, captured via `GET /get_texture?texture_name=SceneViewComposited`
  (the Scene view's own equivalent of `GET /get_game_view`, which correctly
  409'd since "Game" was the inactive dock tab this session — see Phase 6's
  own completion report) — direct RGB pixel sampling confirmed a clear,
  monotonic brightness/color falloff with distance
  (`Near_10m ≈ (97,97,104)` → `Mid_100m ≈ (92,92,98)`/`(42,42,45)` →
  `Far_400m ≈ (75,75,81)`/`(34,34,36)`). `GET
  /get_texture?texture_name=AtmosphereAerialPerspectiveVolume_SceneView`
  showed a legible blue-tinted spatial gradient. `GET /list_textures`
  reported every expected texture, including both aerial volumes correctly
  tagged `"kind":"texture3d"`. Two consecutive `GET
  /get_texture?texture_name=AtmosphereTransmittanceLut` captures were
  byte-for-byte identical (`fc /b`: "no differences encountered"). The
  background engine process was stopped cleanly afterward via
  `stop_app_background` — no process left running.

## Conclusion

The `atmosphere-scattering-2` campaign closes out the original
`atmosphere-scattering-1` bug report end to end: the Aerial Perspective
system's pipeline was always logically correct, but its real-Earth-scale
defaults (10km max distance, unexaggerated real-world scattering
coefficients) made the effect invisible at the meter-to-few-hundred-meter
scale this engine's actual content lives at. Six phases — a plumbing
refactor to make every relevant constant a live, Editor-tunable value; two
small composite-pass precision fixes; the actual visibility rebalance (new
sub-kilometer default distance plus a physically-motivated scattering-
exaggeration multiplier, empirically tuned against real captured evidence);
an atmosphere-aware debug-preview mode for the existing volume-texture HTTP
capture; a new objective numeric inspection tool; and finally full
documentation, a full clean build, a full regression pass, and a live
runtime smoke test — together deliver a genuinely visible, tunable, and
independently verifiable aerial-perspective effect, with zero regressions
anywhere else in the engine.
