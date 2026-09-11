# PHASE0_MASTER_STRATEGY — "Aerial Perspective: Make It Visible + See The LUT" (atmosphere-scattering-2 campaign)

Orchestrator document. Every other `PHASEn_*.md` file in this same folder
(`task_manager/atmosphere-scattering-2/`) is a child task of this one. Read
this file FIRST, then read the current `PHASEn` file the Task Status list
below points you at. **Always read the previous phase's own
`PHASEn_COMPLETION_REPORT.md`** (once it exists) before starting the next
phase — it may contain corrections/clarifications that supersede a stale
assumption in this master file, exactly like every prior multi-phase
campaign in this repository (`atmosphere-scattering-1`, `network-impl-6`).

Also read, before Phase 1, both of this same folder's pre-existing
investigation documents — they are the evidence base this whole campaign is
built on, and every phase below cites them by section:

- `pl-sky_aerial_perspective.md` — technical notes on the reference
  implementation (`_reference/pl-sky`) this engine's own Aerial Perspective
  system was ported from.
- `AERIAL_PERSPECTIVE_INVESTIGATION_FINDINGS.md` — the root-cause analysis
  comparing this engine's real, committed code against that reference,
  concluding the pipeline is wired correctly end-to-end and the "no visible
  fog" complaint is a **scale mismatch**, not a logic bug, plus two smaller,
  real, independently-fixable precision gaps.

## Step 1 — The Goal (Where are we going?)

Two deliverables, both pure code changes:

1. **Make the Aerial Perspective effect (the "blueish fog on far-distance
   objects") actually visible in this engine's real running scenes**, without
   throwing away the physically-based model this engine already has — by
   fixing the genuine scale mismatch the investigation found (the froxel
   volume's fixed 10km far plane and real-Earth scattering coefficients are
   both correct for their own units, but that means the effect is
   ~2-3 orders of magnitude too faint to see at the few-meters-to-few-hundred-
   meters distances this engine's real content lives at), plus fixing the two
   smaller, already-identified, real precision gaps in the composite pass
   (missing half-texel Z-bias, missing first-slice fade-in) that would
   otherwise become newly-visible artifacts once the scale mismatch is fixed.
2. **Give the Aerial Perspective froxel volume a genuinely useful, live,
   network-reachable visual debugging view**, building on the ALREADY-SHIPPED
   `network-impl-6` campaign's generic "Texture3D over HTTP" capability
   (`GET /get_texture` on a volume-texture name already returns a live
   raymarched PNG thumbnail today) — but specialized for THIS LUT's actual
   data semantics (transmittance in alpha, tiny HDR in-scattering in rgb),
   since the existing generic interpretation (alpha = density, rgb = raw
   color) renders this specific volume as an uninformative, nearly-flat dark
   box regardless of whether the underlying data is actually correct or not.

Both deliverables are 100% code (shader + C++ + Editor UI); no asset,
content, or camera-placement changes are part of this campaign's own
definition of done (moving test geometry farther from the camera is a valid
workaround a user could do today, but it is explicitly NOT what this
campaign delivers — see `AERIAL_PERSPECTIVE_INVESTIGATION_FINDINGS.md`'s own
"Recommendations" list, first bullet, which this campaign deliberately does
NOT implement).

## Step 2 — The Situation / The Problem (Where are we now?)

### 2.1 — The Aerial Perspective bug

- The real, committed implementation (`AtmosphereAerialPerspectiveVolume.comp`,
  `AtmosphereAerialPerspectiveComposite.comp`,
  `src/Renderer/Atmosphere/AtmosphereLutRenderer.cpp`,
  `src/Renderer/Atmosphere/AtmosphereTypes.h`) faithfully reproduces
  `pl-sky_aerial_perspective.md`'s own documented pipeline, per-froxel
  algorithm, and composite formula — confirmed section-by-section in
  `AERIAL_PERSPECTIVE_INVESTIGATION_FINDINGS.md`'s own "Confirmed correct"
  section. This is NOT a logic bug hunt; there is no wrong sign, no
  transposed matrix, no missing barrier.
- `kAerialMaxDistanceKm = 10.0` (km) and `kAerialDepthExponent = 2.0` are
  each **hardcoded as separate, disconnected literal constants** in THREE
  places: `AtmosphereAerialPerspectiveVolume.comp` (line ~70),
  `AtmosphereAerialPerspectiveComposite.comp` (line ~54), and only
  informally mirrored (never actually read from) by a comment in
  `AtmosphereLutRenderer.cpp`. `kAerialSamplesPerSlice = 8` is a fourth,
  separately hardcoded constant, only in the volume-generation shader. There
  is no single source of truth, and no way to tune any of them without
  hand-editing GLSL and recompiling shaders.
- This engine's own world-unit convention (`AtmosphereParameters.h`):
  **1 world unit = 1 meter**, **1000 world units = 1 km**
  (`kWorldUnitsPerKilometer`). Typical engine test content (primitives
  spawned "in front of the camera", default `EditorCamera` a few units away)
  sits at ~0.005 km — literally ~2000x closer than the volume's own 10km far
  plane, deep inside the first, near-camera froxel slice, where the
  ray-marched optical depth is essentially zero. Real Earth-like
  Rayleigh/Mie coefficients (~0.0058-0.0331 per **kilometer**) genuinely
  produce negligible optical depth over anything less than several
  kilometers — this is physically correct (real-world haze is not visible
  5-500 meters away either), but it means the shipped defaults make the
  effect invisible for essentially every realistic test scene this engine
  currently has.
- Two smaller, real, independently-confirmed precision gaps in
  `AtmosphereAerialPerspectiveComposite.comp` (both currently harmless ONLY
  because the values they'd affect are already ~0 at meter-scale distances —
  seePhase 2's own file for the exact reasoning):
  1. No half-texel Z-bias when sampling the volume (every composited pixel
     reads roughly half a froxel slice "ahead" of where it should).
  2. No first-slice fade-in blend (a seam/pop risk for geometry that falls
     inside the coarse first froxel slice once the effect's magnitude
     actually matters).
- `AtmosphereSettings::aerialPerspectiveStrength` (Editor "Atmosphere" panel,
  default 1.0, range `[0, 2]`) is a pure OUTPUT-side linear multiplier — it
  cannot rescue an already-tiny (`~1e-5`) signal; turning it up to its
  maximum (2.0x) still leaves the effect imperceptible. A genuine fix must
  act BEFORE this multiplier, on the underlying distance scale and/or
  scattering magnitude, not just amplify the final blend weight further.

### 2.2 — The 3D-texture visual debugging situation

- `task_manager/network-impl-6/` (already complete, already committed on a
  prior branch/session) added exactly the capability the user is asking
  about: `GET /get_texture?texture_name=AtmosphereAerialPerspectiveVolume_GameView`
  already returns a real PNG — a fixed-camera, front-to-back
  alpha-composited raymarch thumbnail of the LIVE volume, rendered fresh on
  every request — through the exact same endpoint/format negotiation every
  2D texture capture already used. `GET /list_textures` already lists every
  volume texture too (tagged `"kind":"texture3d"`, with a `"depth"` field).
  **This part of the user's request is therefore already built** — this
  campaign does not need to re-invent it.
- However, `VolumeTexturePreviewRenderer`/`VolumeTexturePreview.comp`
  (network-impl-6, Phase 3) is **deliberately, explicitly generic**: it
  treats `sample.a` as "density" and `sample.rgb` as "color", exactly Unity's
  own generic `Texture3D` "Volume" inspector-preview convention. For THIS
  engine's own Aerial Perspective volume specifically, `alpha` is
  **transmittance** (how much light SURVIVES — the opposite polarity of a
  density; it sits at ~0.99-1.0 almost everywhere at today's defaults) and
  `rgb` is **raw HDR in-scattered luminance** (often `1e-5`-`1e-3`,
  functionally black without any exposure adjustment). Fed through the
  generic interpretation, this volume previews as an almost uniformly dark,
  nearly-opaque box regardless of whether the underlying data is healthy or
  broken — the wrong tool for confirming this campaign's own Phase 3 fix
  actually worked, and not useful for live visual debugging as the user
  intends.
- Confirmed with the project owner (see this repo's own conversation record
  for this campaign): build a new, ADDITIVE, atmosphere-aware interpretation
  mode into the EXISTING `VolumeTexturePreviewRenderer`/`.comp` (auto-detected
  by volume name, zero new query parameters, zero new endpoints — mirrors
  `network-impl-6`'s own "zero opt-in" philosophy applied to itself), rather
  than building a second, parallel preview mechanism.

## Step 3 — The Plan (How do we get there?)

Six phases, strictly ordered (each depends on the previous phase's new code
already existing and compiling):

| Phase | One-line goal | Primary files touched |
|---|---|---|
| **1** | Single source of truth: hoist the 4 aerial-LUT tuning constants (max distance, depth exponent, samples/slice, + a new scattering-exaggeration knob placeholder) out of 3 disconnected hardcoded shader literals into `AtmosphereSettings`, threaded through `AtmosphereFrameUniforms`/composite push-constants, with live Editor sliders. Deliberately ships with the SAME numeric defaults as today (10km/2.0/8/1.0) — a pure plumbing refactor, zero visual behavior change — so Phase 3 can safely change the actual shipped numbers in exactly one place. | `AtmosphereTypes.h`, `AtmosphereCommon.glsl`, both `AtmosphereAerialPerspective*.comp`, `AtmosphereLutRenderer.h/.cpp`, `AtmospherePassSequence.h/.cpp`, `Application.cpp`, `AtmospherePanel.cpp` |
| **2** | Fix the two documented composite-pass precision gaps: half-texel Z-bias and first-slice fade-in blend, matching `pl-sky_aerial_perspective.md` §5 exactly. | `AtmosphereAerialPerspectiveComposite.comp` |
| **3** | The actual visibility fix: ship new DEFAULT values (sub-kilometer max distance) + a new, physically-motivated "scattering exaggeration" multiplier applied inside the aerial volume's own ray-march (never the sky-view/transmittance LUTs), live-tunable via the sliders Phase 1 already built. | `AtmosphereTypes.h` (new default values + new field), `AtmosphereAerialPerspectiveVolume.comp`, `AtmosphereFrameUniforms` group (already added by Phase 1) |
| **4** | Atmosphere-aware interpretation mode for the existing (`network-impl-6`) volume-texture HTTP preview: auto-detected by volume name, transmittance-as-haze + tonemapped in-scattering, zero new endpoints/query params, zero regression to the existing generic preview for any other volume. | `VolumeTexturePreviewRenderer.h/.cpp`, `VolumeTexturePreview.comp`, `Application.cpp` |
| **5** | A numeric, non-visual CPU-readback inspection tool for the aerial volume (mirrors the proven `ValidateAtmosphereTransmittanceLut` pattern) — min/max/mean transmittance and in-scattering magnitude, printed in the Editor panel, giving an objective before/after signal for Phase 3's rebalancing that doesn't depend on eyeballing a screenshot. | `src/Editor/AtmosphereAerialPerspectiveLutInspection.h/.cpp` (new), `AtmospherePanel.cpp` |
| **6** | Documentation (`AGENTS.md`/`README.md`/`TODO.md`) + full clean build/regression + a live runtime smoke test (via `run_app_background`/`gte_send_request`) that visually confirms the blue haze is now actually visible on far geometry, closing out the campaign. | `AGENTS.md`, `README.md`, `TODO.md`, a completion report |

### Locked Design Decisions (confirmed with the project owner — do not re-litigate)

1. **Both the scale fix AND an artistic exaggeration knob are in scope**
   (not an either/or) — shrink the default aerial max-distance to
   sub-kilometer scale AND add a scattering-exaggeration multiplier for
   extra visible punch on top of that. Neither one alone was judged
   sufficient on its own.
2. **All new aerial-perspective tunables are live Editor sliders**, mirroring
   `AtmosphereSettings::aerialPerspectiveStrength`'s own existing pattern
   exactly (a `DragFloat`/`DragInt` in `AtmospherePanel.cpp`, backed by a
   plain `AtmosphereSettings` field, no persistence/serialization — see
   `AGENTS.md`'s existing "No scene (de)serialization of
   `DirectionalLight`/`AtmosphereSettings`" rule, which this campaign does
   NOT change).
3. **The volume-texture debug preview gets a genuine, additive,
   atmosphere-aware interpretation mode** (Phase 4) rather than being left
   generic or replaced by a numeric-only tool — auto-detected by volume
   name, so it stays a zero-opt-in, zero-new-endpoint extension of
   `network-impl-6`'s own established contract.
4. **The aerial LUT's fixed resolution (128x128x32) is NOT made tunable by
   this campaign** — only max distance / depth exponent / samples-per-slice
   / scattering exaggeration become tunable, exactly mirroring `pl-sky`'s own
   reference UI (which keeps `tAerialLutResolution` fixed while exposing
   those same 3-4 knobs). Do not add a resolution slider; it is out of
   scope.
5. **No change to `AtmosphereParametersGpu`'s own physical Rayleigh/Mie/ozone
   coefficients or `AtmosphereMath.h`'s CPU oracle.** The new "scattering
   exaggeration" knob is a multiplier applied LOCALLY inside
   `AtmosphereAerialPerspectiveVolume.comp`'s own per-sample coefficients —
   it never touches the shared `ComputeExtinctionCoefficientAtHeight()`/
   `RayleighDensityAtHeight()`/etc. oracle functions other passes (sky-view
   LUT, transmittance LUT) also call, so the sky's own physically-accurate
   rendering is completely unaffected by this campaign.
6. **No new HTTP endpoint, no new query parameter, for the volume-texture
   preview.** Phase 4 extends the EXISTING `GET /get_texture` volume branch
   purely by inspecting the already-known `texture_name` string server-side
   — this is a strict, backward-compatible continuation of
   `network-impl-6`'s own Locked Design Decision 5 ("`GET /get_texture` is
   extended, not duplicated"), applied one layer deeper.

### Non-goals (explicitly out of scope for this whole campaign)

- No content/scene changes (moving test geometry farther from the camera,
  adding new test scenes, etc.) — the fix must work by changing code/
  defaults, not by asking a user to move their objects.
- No volumetric clouds, god-rays, or any other new atmosphere visual feature
  — this campaign only fixes/tunes the EXISTING aerial-perspective system.
  and improves debugging of it.
- No change to the aerial LUT's fixed 128x128x32 resolution.
- No change to `AtmosphereParametersGpu`'s physical constants or
  `AtmosphereMath.h`'s CPU oracle.
- No "Slice"/"Maximum Intensity Projection" preview modes for the volume-
  texture debug endpoint (still exactly Unity's "Volume" front-to-back
  raymarch, per `network-impl-6`'s own still-standing non-goal) — Phase 4
  only changes HOW density/color are DERIVED from the sampled texel for one
  specific, auto-detected volume, not the raymarch/compositing algorithm
  itself.
- No scene (de)serialization for the new `AtmosphereSettings` fields (same
  pre-existing, documented limitation every other `AtmosphereSettings` field
  already has).

### Cross-phase file map (every file this campaign touches, for quick reference)

**New files:**
- `src/Editor/AtmosphereAerialPerspectiveLutInspection.h` (Phase 5)
- `src/Editor/AtmosphereAerialPerspectiveLutInspection.cpp` (Phase 5)

**Modified files:**
- `src/Renderer/Atmosphere/AtmosphereTypes.h` (Phases 1, 3)
- `src/Shaders/AtmosphereCommon.glsl` (Phase 1)
- `src/Shaders/AtmosphereAerialPerspectiveVolume.comp` (Phases 1, 3)
- `src/Shaders/AtmosphereAerialPerspectiveComposite.comp` (Phases 1, 2)
- `src/Renderer/Atmosphere/AtmosphereLutRenderer.h` (Phase 1)
- `src/Renderer/Atmosphere/AtmosphereLutRenderer.cpp` (Phase 1)
- `src/Application/AtmospherePassSequence.h` (Phase 1)
- `src/Application/AtmospherePassSequence.cpp` (Phase 1)
- `src/Application/Application.cpp` (Phases 1, 4)
- `src/Editor/Panels/AtmospherePanel.h` (Phase 5, if the result-storage member needs a new type forward-declared)
- `src/Editor/Panels/AtmospherePanel.cpp` (Phases 1, 3, 5)
- `src/Renderer/VolumeTexturePreviewRenderer.h` (Phase 4)
- `src/Renderer/VolumeTexturePreviewRenderer.cpp` (Phase 4)
- `src/Shaders/VolumeTexturePreview.comp` (Phase 4)
- `AGENTS.md`, `README.md`, `TODO.md` (Phase 6)

### Workflow rules every phase must follow

1. **No full build/full regression test until Phase 6.** Phases 1-5 only
   need a fast, targeted compile check (see each phase's own "Verification"
   section) — mirrors `atmosphere-scattering-1`/`network-impl-6`'s own
   precedent exactly.
2. **Every phase writes its own `PHASEn_COMPLETION_REPORT.md`** in this same
   folder once its own compile check passes, then commits (code + report) to
   git — never bundle two phases into one commit.
3. **Any change to `AtmosphereParametersGpu`/`AtmosphereFrameUniforms` (C++
   struct in `AtmosphereTypes.h`) MUST be mirrored, same session, in
   `AtmosphereCommon.glsl`'s own hand-maintained GLSL copy** — this campaign
   has no shader reflection, exactly like the original atmosphere-scattering-1
   campaign's own rule (see `AGENTS.md`, "Atmosphere Scattering"). Update
   BOTH struct definitions' own `static_assert`/byte-count comments in the
   SAME commit as the field change.
4. **`AtmosphereMath.h`/`AtmosphereCommon.glsl`'s shared oracle functions
   (`ComputeExtinctionCoefficientAtHeight`, `RayleighDensityAtHeight`, etc.)
   are NEVER modified by this campaign** — every new tunable this campaign
   adds is consumed strictly LOCALLY inside whichever `.comp` file needs it
   (see Locked Design Decision 5).
5. **Never regress the existing, shipped behavior of `GET /get_texture`/
   `GET /list_textures` for 2D textures, or the generic volume-preview
   interpretation for any volume texture OTHER than the Aerial Perspective
   one.** Phase 4's new branch must be strictly ADDITIVE.
6. **Every Tier-1-testable piece of new pure logic gets a real test in the
   same phase that introduces it** (per `TESTING.md`'s existing Tier
   classification) — Phase 5's new min/max/mean-aggregation helper function
   should be written as a small, pure, Tier-1-testable function (taking a
   `std::vector<float>`/raw pixel buffer and returning the stats struct)
   with a matching test file, even though the CPU-readback wrapper around it
   is Tier-2 (GPU-touching, no automated test, same precedent as
   `AtmosphereTransmittanceLutValidation`).
