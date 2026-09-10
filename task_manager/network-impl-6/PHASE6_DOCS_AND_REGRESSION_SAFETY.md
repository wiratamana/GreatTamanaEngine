# PHASE6_DOCS_AND_REGRESSION_SAFETY

Parent: `PHASE0_MASTER_STRATEGY.md` — **READ THAT FILE FIRST.**
Previous: `PHASE5_LIST_TEXTURES_VOLUME_SURFACING.md` — read its own
`PHASE5_COMPLETION_REPORT.md` before starting.

This is the LAST phase of the `network-impl-6` campaign. Unlike Phases
1-5, this phase's own Workflow Rule 1 exception applies: **a full clean
build and a full regression test run ARE required here.**

## Step 1 — The Goal

Close out the campaign: update this engine's own living documentation
(`AGENTS.md`, `README.md`) to describe the new capability exactly the way
every other completed campaign already documents itself, reverse the
now-stale "deliberate non-goal" note `AGENTS.md` currently carries, and
prove the WHOLE campaign (Phases 1-5 combined) builds clean and regresses
nothing across the engine's full automated test suite plus a final,
combined end-to-end runtime smoke pass.

## Step 2 — The Situation / The Problem

- **Correction (accuracy check against the real source): the stale sentence
  below actually lives in `AGENTS.md`'s "Atmosphere Scattering" section, NOT
  its "Named Texture Capture" subsection** (search `AGENTS.md` for "do NOT
  retrofit" to find its exact current location — it's the second bullet of
  "## Atmosphere Scattering", under the "Render Graph's resource vocabulary
  now has a genuine THIRD kind" bullet, not anywhere inside "### Named
  Texture Capture (`GET /get_texture`)"). Written by the `atmosphere-
  scattering-1` campaign, that sentence is now **factually wrong** once this
  campaign lands: *"`RenderGraphDebugTextureRegistry` deliberately still has
  NO 3D concept at all — a volume texture is never directly capturable that
  way... do NOT retrofit `RenderGraphDebugTextureRegistry` itself to
  understand 3D resources generically; that remains a deliberate,
  out-of-scope non-goal."* This phase must correct THAT sentence, in the
  "Atmosphere Scattering" section, not merely add new text alongside a
  now-false claim — and separately (see Step 3.1 below), ADD new
  documentation of this campaign's own capability into the "### Named
  Texture Capture (`GET /get_texture`)" subsection instead, which is a
  DIFFERENT location in the same file — do not conflate the two edits into
  a single spot.
- `README.md`'s "Status" section documents every past networking campaign
  (`network-impl-1` through `network-impl-5`) as its own dated bullet,
  each summarizing what shipped and pointing at that campaign's own
  `task_manager/network-impl-N/PHASE0_MASTER_STRATEGY.md`. This campaign
  needs the same treatment — a new bullet for `network-impl-6`.
- Every prior campaign's own final phase (e.g.
  `network-impl-4/PHASE0_MASTER_STRATEGY.md`'s Phase 6,
  `network-impl-5/PHASE5_TESTS_DOCS_AND_REGRESSION_SAFETY.md`) is the
  precedent to match in tone/depth for this phase's own doc updates — read
  at least one of them before writing this campaign's own equivalent
  prose, so the new `README.md`/`AGENTS.md` entries read consistently with
  everything around them rather than in a noticeably different voice.

## Step 3 — The Plan

### 3.1 — `AGENTS.md` updates (TWO separate locations — do not conflate them)

- **Location A — `AGENTS.md`'s "Atmosphere Scattering" section** (the
  "Render Graph's resource vocabulary now has a genuine THIRD kind" bullet —
  see Step 2's own correction above for exactly where): replace the
  now-false "deliberate, out-of-scope non-goal" sentence (quoted in Step 2
  above) with an accurate description of what this campaign actually built:
  `RenderGraphDebugVolumeTextureRegistry`
  (`src/Renderer/RenderGraph/RenderGraphDebugVolumeTextureRegistry.h/.cpp`)
  is the volume-texture sibling of `RenderGraphDebugTextureRegistry`,
  auto-populated by `RenderGraph::ExecuteCompiledGraph()` (Phase 2) exactly
  like the 2D registry, and `GET /get_texture` now transparently resolves
  EITHER kind by name (Phase 4) — a volume capture is rendered fresh
  on-demand via a raymarch (`VolumeTexturePreviewRenderer`, Phase 3),
  never a raw pixel copy, since a 3D voxel grid has no direct 2D pixel
  representation to copy. Keep this correction narrowly scoped to that one
  stale sentence — the rest of the "Atmosphere Scattering" section (e.g. the
  "no `CreateVolumeTexture()` pooled/transient counterpart yet" note right
  above it) is untouched by this campaign and must not be reworded.
- **Location B — `AGENTS.md`'s "Networking" section, "### Named Texture
  Capture (`GET /get_texture`)" subsection**: add a new bullet (mirroring
  this subsection's existing "The two new endpoints' exact contract:" bullet
  style) documenting:
  - `GET /get_texture?texture_name=<name>` now ALSO accepts a name
    registered as a volume texture — same query parameters, same
    `?format=png|base64|json` negotiation, same `Accept: application/json`
    honoring, with `channel=depth` always `409` for a volume name (no
    depth-companion concept exists for one).
  - The render itself is a single, FIXED-camera, front-to-back
    alpha-composite raymarch (Unity Texture3D "Volume" preview mode
    equivalent) into a persistent 256x256 RGBA8 thumbnail — no camera
    query parameters, no other preview modes (Slice/MIP), by deliberate
    design (see `task_manager/network-impl-6/PHASE0_MASTER_STRATEGY.md`'s
    Locked Design Decisions for the full reasoning) — this is a
    debugging/LLM-agent capability, not an Editor inspector feature.
  - `GET /list_textures` entries now carry a `"kind":"texture2d"|"texture3d"`
    field, and a `"texture3d"` entry additionally reports `"depth"` (its
    voxel-Z extent) — every pre-existing 2D entry's other fields are
    unchanged.
  - The generic mechanism works for ANY current or future
    `VolumeTextureHandle` a render-graph pass declares and keeps alive via
    `RenderGraphBuilder::KeepVolumeTextureOutput()` — not hardcoded to the
    Atmosphere feature's own two volumes specifically, mirroring the 2D
    registry's own "zero opt-in required" property.
  - Point at `task_manager/network-impl-6/PHASE0_MASTER_STRATEGY.md` for
    the full six-phase campaign writeup, matching every other campaign's
    own cross-reference convention in this file.
- Add a short new bullet to this same section (or a small new subsection,
  whichever reads more naturally alongside the existing structure)
  documenting `VolumeTexturePreviewRenderer`/`VolumeTexturePreviewMath.h`
  as the "self-contained, on-demand, no-RenderGraph-dependency" renderer
  class, explicitly cross-referencing the SAME established pattern
  `ComputeBlurValidation`/`AtmosphereTransmittanceLutValidation`/
  `GpuSkinningValidation` already established (per this file's own
  existing convention of naming precedent classes when a new one follows
  the same shape) — and note the CPU-oracle-vs-GLSL discipline
  `VolumeTexturePreviewMath.h`'s `IntersectRayBox()`/camera setup follow,
  mirroring `AtmosphereMath.h`'s own "if the GLSL and the CPU oracle ever
  disagree, the CPU oracle is right by definition" rule (see the
  "Atmosphere Scattering" section's own existing wording for the exact
  phrase to reuse).

### 3.2 — `README.md` updates ("Status" section)

Add one new bullet, following the exact structure/tone of the existing
`network-impl-5` bullet immediately above it in the file: what shipped
(`GET /get_texture`/`GET /list_textures` now understand live 3D volume
textures, rendered as a raymarched thumbnail, for the Atmosphere
aerial-perspective LUTs specifically as this campaign's own first real,
verified consumer), which new files/classes were added
(`RenderGraphDebugVolumeTextureRegistry`, `VolumeTexturePreviewRenderer`,
`VolumeTexturePreviewMath`), and a pointer at
`task_manager/network-impl-6/PHASE0_MASTER_STRATEGY.md` for the full
six-phase writeup — matching every other networking campaign bullet's own
closing convention exactly.

### 3.3 — `TODO.md` sanity check

Skim `TODO.md` for any existing forward-looking note about "3D texture
support"/"volume texture capture"/similar language that this campaign now
resolves — if one exists, remove or update it to point at this campaign's
own completion instead of leaving a stale "not yet done" note contradicted
by the engine's own new, real capability. If nothing there mentions this
at all, no change is needed (do not invent a new TODO entry just to
immediately close it in the same phase).

### 3.4 — Full clean build + full regression pass (THIS phase only — not before)

1. `cmake --build build` (or a fresh configure if warranted) — a full
   build of every target (`gte_core`, `GreatTamanaEngineTests`,
   `GreatTamanaEngine`), catching any residual issue across all five prior
   phases' combined changes.
2. `cd build && ctest -C Debug --output-on-failure` — the FULL suite, not a
   filtered subset (unlike Phases 1-5's own allowed "targeted filter"
   shortcut) — confirm the total test count only GREW relative to the
   count reported before this campaign began (Phase 1's new registry
   tests, Phase 3's new math tests, plus zero regressions/failures
   anywhere else), and that the one pre-existing machine-gated smoke test
   this repository's test suite already documents (see `TESTING.md`) is
   the only skip, exactly as it always is.
3. One final, combined, END-TO-END runtime smoke pass tying every phase
   together in a single session (`run_app_background` +
   `gte_send_request` + `stop_app_background`):
   - `GET /list_textures` — confirm both volume names AND every pre-existing
     2D name are present with correct `kind`/`depth` fields.
   - `GET /get_texture?texture_name=AtmosphereAerialPerspectiveVolume_GameView`
     (default `?format=png`) — confirm a real PNG comes back and visually
     inspect it via the image-viewing tool available in this environment.
   - The SAME request with `?format=json` — confirm the JSON envelope
     shape is correct (`width`/`height`/`format":"png"`/`data_base64`/
     `frames_since_update`) and that decoding `data_base64` produces the
     identical PNG bytes the plain `?format=png` request returned.
   - `?channel=depth` against the same volume name — confirm `409`.
   - A known 2D texture name — confirm byte-for-byte unchanged behavior
     from before this campaign (re-run whatever exact request Phase 4/5's
     own smoke tests already used and compare).
4. If ANY of the above surfaces a real regression, fix it as part of THIS
   phase (do not defer a real bug found here to a "future cleanup" note —
   this is the campaign's own final safety net).

### Verification

- Full build + full `ctest` pass (see 3.4) — this IS this phase's
  verification, not a separate step.
- Write `PHASE6_COMPLETION_REPORT.md` AND a top-level
  `NETWORK_IMPL_6_CAMPAIGN_COMPLETION_REPORT.md` (mirroring
  `network-impl-4`/`network-impl-5`'s own precedent of a campaign-level
  report tying all per-phase reports together) in this same
  `task_manager/network-impl-6/` folder, then commit everything (code +
  both reports) as this campaign's final commit.
