# ATMOSPHERE_PHASE9_VALIDATION_DEBUG_TOOLING_AND_DOCS_v1.md

### Child document 9 of 9 (final phase) — see `ATMOSPHERE_PHASE0_MASTER_STRATEGY_v1.md` for the full campaign map.
### Depends on: every prior phase (1-8) fully landed. This is the ONLY phase allowed to run a full build + full `ctest` regression pass and update `README.md`/`AGENTS.md`/`TODO.md`, per Phase 0's own workflow rule.

## Step 1: The Goal

Close out the campaign: prove the Transmittance LUT's GPU output is
numerically correct against Phase 1's permanent CPU oracle (not just
"looks plausible"), give the volume texture the debug visibility Phase 2
deliberately deferred, wire real GPU timing for the whole atmosphere pass
sequence into the existing Profiler/Render Graph panels, do a final full
build + full regression pass, and update every doc this campaign is allowed
to touch.

## Step 2: The Situation

- `AtmosphereMath.h`'s `ComputeTransmittanceToTopOfAtmosphere()` (Phase 1) has
  never actually been checked against the real `.comp` shader's (Phase 3)
  output — every phase since has only used it as a stated intention, per the
  campaign-wide "permanent CPU oracle" rule (Phase 0). This is the one real
  numeric-parity proof this campaign owes itself, mirroring the GPU Vertex
  Skinning campaign's own `GpuSkinningValidation` tool (`src/Editor/
  GpuSkinningValidation.h/.cpp`) and `ComputeBlurValidation`'s general
  "there is no live-`VkDevice` automated test infra in this repo, so
  correctness proof is a manual, Editor-side, numeric-comparison TOOL, not a
  GoogleTest" precedent (see `AGENTS.md`'s "Testability & Regression Safety"
  and the GPU Skinning campaign's own Phase 6 v2 revision for why an
  automated Tier-2 test was explicitly NOT attempted).
- The aerial-perspective volume (Phase 6) still has no debug-visibility path
  at all — `GET /get_texture` cannot capture a 3D resource (Phase 2's own
  explicit, accepted scope limit).
- `GpuTimingService`/`GpuTimingSlot` (see `AGENTS.md`, "Profiling") today only
  knows about `Offscreen0`/`Offscreen1`/`SwapchainPresent` — the atmosphere
  pass sequence's own passes have no GPU-timing visibility in the "Profiler"
  panel yet, and the "Render Graph" panel (`RenderGraphSnapshot.h`/
  `Panels/RenderGraphPanel.cpp`) shows every pass's GPU time as "N/A" by
  design today (see `AGENTS.md`'s own note that real per-render-graph-pass
  GPU timing is still an open, campaign-wide TODO, not unique to this
  feature) — do not treat closing that broader gap as this phase's job; it
  is explicitly out of scope (see Step 4).
- `README.md`'s "Status" section and `AGENTS.md` have no atmosphere-scattering
  entries yet — this is the only phase allowed to add them.

## Step 3: The Plan

### 3.1 — `AtmosphereTransmittanceLutValidation` (numeric parity tool)

- New `src/Editor/AtmosphereTransmittanceLutValidation.h/.cpp`, directly
  modeled on `GpuSkinningValidation.h/.cpp`'s shape: a small Editor-only
  tool that, on demand (a button, e.g. in the new "Atmosphere" panel from
  Phase 8), reads back the REAL, currently-computed
  `"AtmosphereTransmittanceLut"` texture (via a `vkCmdCopyImageToBuffer` +
  mapped staging buffer, the same readback technique Phase 2/6's own
  disposable validation already used, now built as a small permanent,
  reusable helper instead of throwaway code), decodes each texel's UV back
  into `(height, zenithAngle)` using the SAME shared parameterization
  function (call the GLSL-mirrored C++ version — if this parameterization
  function currently only exists in GLSL, port it into `AtmosphereMath.h`
  now, since Phase 1 only explicitly required the density/transmittance
  formulas there, not necessarily the UV parameterization itself — check
  Phase 1's actual delivered scope first), calls
  `AtmosphereMath::ComputeTransmittanceToTopOfAtmosphere()` for that same
  `(height, zenithAngle)`, and reports the max/mean absolute per-channel
  difference across every texel, plus a pass/fail against a documented
  tolerance (floating-point/sample-count differences between the CPU's and
  GPU's numerical integration are expected to produce a SMALL, non-zero
  delta — document the chosen tolerance and why it's reasonable, mirroring
  `GpuSkinningValidation`'s own tolerance-setting reasoning).
- Surface the result as plain text/a small table in the Editor's
  "Atmosphere" panel (Phase 8) — a "Validate Transmittance LUT" button plus
  a "last result" readout, exactly mirroring how the GPU Skinning campaign's
  own validation tool surfaces its result.
- If a real, meaningful mismatch is found, that is a genuine bug in EITHER
  the Phase 3 shader or Phase 1's CPU oracle — fix it now, in whichever side
  is actually wrong (per the campaign-wide "CPU oracle is right by
  definition" rule, the shader is the default suspect, but confirm by hand
  before assuming).

### 3.2 — Volume-texture debug visibility (the Phase 2-deferred gap)

- Add a small, permanent "debug slice" mirror: a tiny compute or graphics
  pass (reuse whichever is simpler) that copies ONE Z-slice (a `uint
  debugSliceIndex`, defaulting to the middle slice, exposed as a small slider
  in the new "Atmosphere" panel) of the aerial-perspective volume into a
  REAL, registered 2D texture, e.g. `"AtmosphereAerialPerspectiveVolumeDebugSlice"`,
  via `CreateTexture()`/`ImportTexture()` (per Phase 0's own naming rule) —
  this is now automatically visible via the EXISTING `GET /get_texture`
  endpoint with zero further networking changes, exactly as
  `AGENTS.md`'s "Named Texture Capture" section already promises for any
  texture any pass declares.
- This is a small, targeted, NEW pass — do not attempt to retrofit
  `RenderGraphDebugTextureRegistry` itself to understand 3D resources
  generically (Phase 2's own explicit, deliberate scope limit stands).

### 3.3 — GPU timing for the atmosphere sequence

- Confirm from the actual current "Render Graph" panel/`RenderGraphSnapshot`
  code whether EVERY graph pass already gets a name/draw-stat row "for free"
  simply by existing in the compiled graph (this is almost certainly true,
  per `AGENTS.md`'s own description of that panel) — if so, this phase's
  ENTIRE remaining job here is confirming the atmosphere passes show up
  correctly in that panel (names, resource read/write chips, correct
  ordering/culling display) with a screenshot/description in the completion
  report, NOT adding new `GpuTimingSlot` enumerators (that would require
  extending `GpuTimingService`'s fixed, hand-maintained slot set for every
  single new pass this campaign added, which is explicitly a broader,
  pre-existing, campaign-external gap — see `AGENTS.md`'s own note that
  real per-render-graph-pass GPU timing is still generally open work).

### 3.4 — Final full build + regression pass

- `cmake --build build` (full, not incremental-only) for both `gte_core` and
  `GreatTamanaEngineTests`.
- `cd /d <repo-root>\build && ctest -C Debug --output-on-failure` — every
  test must pass; investigate and fix any regression rather than loosening
  an assertion (per `AGENTS.md`'s "Testability & Regression Safety").
- A manual runtime smoke pass with a live window: launch the engine (via
  `run_app_background`), use `gte_send_request`/`gte_send_request` equivalent
  captures against `/get_game_view`, `/get_texture` for each of the four LUT
  names plus the new debug-slice texture, and `/list_textures` to confirm
  every expected name is present with a sane `frames_since_update`, then
  `stop_app_background` when done.

### 3.5 — Documentation updates (this phase ONLY)

- `README.md`, "Status": a new entry describing the feature exactly the way
  every prior campaign's own entry does (what changed, what file/class names
  matter, what's verified, what's explicitly deferred).
- `AGENTS.md`: a new top-level section (mirroring "GPU Vertex Skinning"'s own
  section shape) naming every load-bearing rule future contributors must
  follow when touching this code — the CPU-oracle rule, the volume-texture/
  RenderGraph 3rd-resource-kind rules from Phase 2, the "first active
  `DirectionalLight` wins" rule, the "aerial perspective is a post-process
  composite, never baked into mesh shaders" rule.
- `TODO.md`: add explicit, named follow-up items for everything Phase 0's
  "What We Will NOT Do" deferred (scene serialization for
  `DirectionalLight`/`AtmosphereSettings`, volumetric clouds/god-rays, a
  general lighting system, day-night animation, per-render-graph-pass GPU
  timing in general).

## Step 4: What We Will NOT Do

- No new general per-render-graph-pass GPU timing infrastructure — see 3.3;
  that is a pre-existing, campaign-external gap, not this phase's job to
  close.
- No automated Tier-2 GoogleTest requiring a live `VkDevice` for the
  Transmittance LUT parity check — the manual Editor tool (3.1) is the
  correct, sufficient level of rigor here, mirroring the GPU Skinning
  campaign's own explicit, considered decision not to attempt this (see
  `AGENTS.md`'s Job System section's own citation of that precedent).
- No scene-serialization work, no general lighting system, no volumetric
  clouds — these are `TODO.md` entries for a FUTURE campaign, not something
  to start building here just because they're now top-of-mind.

## Step 5: Their Role

- This is the phase that turns "a working feature nobody has numerically
  checked" into "a working feature with a real, repeatable correctness
  proof and clean debug tooling" — do not skip 3.1 even though the feature
  already visually works after Phase 7/8; a plausible-looking sky that is
  quietly wrong in its actual radiometric math is exactly the kind of bug
  this kind of manual numeric-parity tool exists to catch.
- After this phase, the campaign is considered COMPLETE — write a final
  `ATMOSPHERE_CAMPAIGN_COMPLETION_REPORT.md` (mirroring
  `RENDERGRAPH_CAMPAIGN_COMPLETION_REPORT.md`'s own shape) tying together
  every phase's individual completion report into one cohesive summary, and
  commit it alongside the doc updates from 3.5.
