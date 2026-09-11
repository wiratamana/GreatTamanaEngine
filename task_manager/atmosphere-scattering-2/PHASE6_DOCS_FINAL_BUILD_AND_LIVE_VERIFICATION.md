# PHASE6 — Documentation, Final Build, and Live Verification

Parent: `PHASE0_MASTER_STRATEGY.md`. Depends on ALL of Phases 1-5 already
being complete and committed. This is the campaign's closing phase — mirrors
`atmosphere-scattering-1`'s own Phase 9 and `network-impl-6`'s own Phase 6
precedent exactly (docs + full build + full regression + a final live smoke
pass + a campaign completion report).

## Step 1 — The Goal (Where are we going?)

1. Make `AGENTS.md`/`README.md`/`TODO.md` accurately describe the new,
   post-campaign reality: aerial-perspective tunables are live/Editor-
   sourced (not hardcoded per-shader constants), new defaults exist, a new
   scattering-exaggeration knob exists, the volume-texture HTTP preview now
   has an atmosphere-aware mode, and a new numeric inspection tool exists.
2. Run a full clean build + full regression suite, proving zero regressions
   across the whole engine, not just this campaign's own touched files.
3. Run a final, live, end-to-end smoke test that visually confirms the
   ORIGINAL reported problem is fixed: far-distance geometry now visibly
   shows the blueish aerial-perspective haze.
4. Write `AERIAL_PERSPECTIVE_CAMPAIGN_COMPLETION_REPORT.md`, tying every
   phase's own completion report together (mirrors
   `ATMOSPHERE_CAMPAIGN_COMPLETION_REPORT.md`/
   `NETWORK_IMPL_6_CAMPAIGN_COMPLETION_REPORT.md`'s own shape).

## Step 2 — The Situation (Where are we now?)

By the start of this phase, Phases 1-5 have each already:
- Committed their own code + `PHASEn_COMPLETION_REPORT.md`.
- Passed their own fast/targeted compile check (never yet a full build).
- Never yet run the full `ctest` regression suite.

`AGENTS.md`'s existing "Atmosphere Scattering" section (search for `##
Atmosphere Scattering`) currently states aerial-perspective's max
distance/depth exponent/samples-per-slice are the ORIGINAL
`atmosphere-scattering-1` campaign's fixed values with no Editor-tunable
knob beyond `aerialPerspectiveStrength`/`skyExposure`/the debug-slice index
— every one of those specific claims is now stale after Phases 1-3.
`AGENTS.md`'s existing "Networking"/volume-texture bullets (added by
`network-impl-6`) describe the generic-only interpretation — stale after
Phase 4. `TODO.md`'s existing "Atmosphere Scattering" section lists deferred
non-goals (scene serialization, day-night cycle, etc.) that this campaign
does not touch, but should gain a note about the NEW tunables now existing
(so a future reader doesn't assume they're still hardcoded).

## Step 3 — The Plan (Detailed Steps)

### 3.1 — `AGENTS.md` updates

In the existing `## Atmosphere Scattering` section, add a new bullet (after
the existing "Aerial perspective is applied... post-process compositing
pass" bullet) documenting:

- The Aerial Perspective froxel volume's own max-distance/depth-exponent/
  samples-per-slice/scattering-exaggeration are now real, Editor-tunable
  `AtmosphereSettings` fields (`aerialPerspectiveMaxDistanceKm`/
  `aerialPerspectiveDepthExponent`/`aerialPerspectiveSamplesPerSlice`/
  `aerialPerspectiveScatteringExaggeration`), threaded through
  `AtmosphereFrameUniforms` (volume-generation shader) and the composite
  pass's own push constants — no longer hardcoded, disconnected `const`
  literals duplicated across shader files. Cite this campaign
  (`task_manager/atmosphere-scattering-2/`) by name, mirroring how the
  original campaign is already cited by name in this same section.
- The new default values (`0.5` km max distance,
  `6.0`-or-whatever-Phase-3-actually-shipped exaggeration — use the REAL
  final numbers from `PHASE3_COMPLETION_REPORT.md`, not this document's own
  placeholder) and WHY they changed from `pl-sky`'s own original 10km/1.0
  defaults (the scale-mismatch root cause — cross-reference
  `AERIAL_PERSPECTIVE_INVESTIGATION_FINDINGS.md`, which should stay in
  `task_manager/atmosphere-scattering-2/` as permanent historical record,
  not be deleted).
- The scattering-exaggeration multiplier is applied strictly LOCALLY inside
  `AtmosphereAerialPerspectiveVolume.comp` — the shared
  `AtmosphereMath.h`/`AtmosphereCommon.glsl` oracle functions remain
  untouched and still 100% physically accurate for the Sky-View/
  Transmittance/Multi-Scattering LUTs.

In the existing volume-texture-preview bullet (added by `network-impl-6`,
the "`GET /get_texture` now transparently resolves EITHER kind by name..."
paragraph), append a sentence noting the new atmosphere-aware interpretation
mode (Phase 4): auto-selected by volume name, `alpha` reinterpreted as "haze
amount" (`1 - transmittance`) and `rgb` exposure-adjusted + tonemapped for
the Aerial Perspective volume specifically, while every other (hypothetical
future) volume texture keeps the original, unchanged generic interpretation.

### 3.2 — `README.md` updates

Update the existing Atmosphere Scattering "Status" bullet to mention the new
tunables/defaults exist and that the effect is now visible at typical scene
scale by default (briefly — `README.md` stays a high-level summary, detailed
reasoning belongs in `AGENTS.md`/this campaign's own task_manager docs, not
here).

### 3.3 — `TODO.md` updates

In the existing `## Atmosphere Scattering` section, add one new bullet:
"Aerial-perspective's own max-distance/depth-exponent/samples-per-slice/
scattering-exaggeration are now `AtmosphereSettings` fields (Editor-tunable,
NOT scene-serialized — same limitation as every other `AtmosphereSettings`
field, see the existing 'Scene (de)serialization' bullet immediately above)
— see `task_manager/atmosphere-scattering-2/` for the campaign that added
them." Do not remove any existing bullet unless it is now factually
contradicted (re-read each one before touching it — most, e.g. "Volumetric
clouds, god-rays", remain entirely accurate and untouched by this campaign).

### 3.4 — Full clean build

```
cmake --build build
```//working directory: C:\Users\F5954\Documents\TAMANA\GreatTamanaEngine —
confirm `gte_core`, `GreatTamanaEngineTests`, and `GreatTamanaEngine` all
build with zero errors/new warnings introduced by this campaign's own files.

### 3.5 — Full regression suite

```
cd /d C:\Users\F5954\Documents\TAMANA\GreatTamanaEngine\build && ctest -C Debug --output-on-failure
```
Confirm the SAME pass count/skip count this repository's last known-good
baseline reported (per `network-impl-6`'s own final snapshot: "1265 tests,
1264 passed, 1 skipped" plus this campaign's own Phase 5 new tests — expect
a slightly HIGHER total test count now, all passing, same one pre-existing
machine-gated skip, zero NEW failures).

### 3.6 — Final live runtime smoke test (the actual "is the bug fixed" proof)

Using `run_app_background` + `gte_send_request`:

1. Launch the engine (Editor build) with a test scene containing several
   primitives placed at clearly varied distances from the camera (e.g.
   ~10m, ~100m, ~400m) — construct this scene via whatever the engine's own
   existing scene-setup mechanism is (Editor "Hierarchy" > create
   primitives, or an existing test/demo scene already checked into the
   repo, if one exists at a suitable scale).
2. `GET /get_game_view` (or `/get_swapchain`) — visually confirm the ~400m
   primitive now shows a clear, visible blueish haze/desaturation relative
   to the ~10m one, and that the effect increases smoothly with distance
   (not a hard cutoff) — this is the direct, final proof the originally-
   reported bug ("I can't see the blueish fog on far distance objects") is
   fixed.
3. `GET /get_texture?texture_name=AtmosphereAerialPerspectiveVolume_GameView`
   — visually confirm Phase 4's atmosphere-aware preview shows a legible
   spatial gradient (not a flat dark box).
4. `GET /list_textures` — confirm the aerial volume is still listed,
   unchanged shape (`"kind":"texture3d"`, correct `"depth"`).
5. Re-confirm a known, pre-existing 2D texture name behaves byte-for-byte
   unchanged (zero-regression discipline, same as every prior campaign's own
   final smoke test).
6. `stop_app_background` when finished — never leave a background engine
   process running after this phase ends.

Save any screenshots/captures worth keeping (via `load_image` on a locally
saved PNG, if the smoke test pipeline writes one to disk) as informal
evidence quoted in the completion report — this is the one place in this
whole campaign where an actual visual, human-eye judgment call
("does this look like fog now?") is unavoidable and appropriate, since the
original complaint was itself a visual one.

### 3.7 — `AERIAL_PERSPECTIVE_CAMPAIGN_COMPLETION_REPORT.md`

Write this file in `task_manager/atmosphere-scattering-2/`, mirroring
`ATMOSPHERE_CAMPAIGN_COMPLETION_REPORT.md`/
`NETWORK_IMPL_6_CAMPAIGN_COMPLETION_REPORT.md`'s own shape: a phase-by-phase
summary table, "what genuinely works, end to end, verified against a live
engine" section, new/modified file lists, deliberate non-goals recap, final
verification snapshot (build + regression + smoke test numbers), and a
one-paragraph conclusion.

## Verification

- This phase's own verification IS its Step 3 (3.4-3.6 above) — no separate
  checklist needed beyond what's already spelled out there.
- Write `PHASE6_COMPLETION_REPORT.md` (the per-phase report) AND
  `AERIAL_PERSPECTIVE_CAMPAIGN_COMPLETION_REPORT.md` (the whole-campaign
  report), then commit both together with the doc changes.
