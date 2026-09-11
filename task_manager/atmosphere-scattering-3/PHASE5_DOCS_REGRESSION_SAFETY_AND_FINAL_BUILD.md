# PHASE5 — Docs, Regression Safety, and Final Build

Parent: `PHASE0_MASTER_STRATEGY.md`. Last phase — depends on every previous
phase in this campaign. This is the ONE phase in this campaign allowed (and
expected) to run a full build and the full existing test suite (see
`PHASE0_MASTER_STRATEGY.md`'s own Locked Design Decision 7).

## Step 1 — The Goal (Where are we going?)

Close out the campaign: make the engine's own documentation
(`AGENTS.md`/`README.md`) accurately describe the new atmosphere-aware
preview camera/box AND frustum framing this campaign added, positively
confirm zero regression to every other volume-texture/2D-texture
code path, run one full incremental build plus the FULL existing automated
test suite as a final safety net, and write a single campaign-level
completion report tying every phase's own results together.

## Step 2 — The Situation (Where are we now?)

- `AGENTS.md`'s "Atmosphere Scattering" section already documents
  `atmosphere-scattering-2`'s Phase 4 color-interpretation fix (the
  `"AtmosphereAerialPerspectiveVolume"` prefix-detection auto-selecting
  `VolumeTexturePreviewInterpretation::AtmosphereAerialPerspective`) — this
  campaign's own camera/box (and possibly frustum) changes are a direct,
  natural continuation of that same paragraph and should be added right
  alongside it, not as a disconnected new section.
- `AGENTS.md`'s "Networking" -> "Named Texture Capture" section documents
  `VolumeTexturePreviewRenderer`/`VolumeTexturePreviewMath.h` in general
  terms — it should gain a short note that the Aerial Perspective volume
  specifically now also gets a dedicated camera/box(/frustum) framing, not
  just a dedicated color interpretation, cross-referencing this campaign by
  folder name (`atmosphere-scattering-3`) exactly like every other
  documented campaign addition in this file already does.
- `README.md`'s "Status" section (check its current content directly before
  editing — do not assume its exact existing wording) likely already
  mentions the Aerial Perspective volume preview from `network-impl-6`/
  `atmosphere-scattering-2` — add a short, one-or-two-sentence update there
  too if that section exists and covers this feature, following whatever
  level of detail its existing entries already use (this file is
  user-facing/summary-level, NOT as detailed as `AGENTS.md`).

## Step 3 — The Plan (Detailed Steps)

### 3.1 — `AGENTS.md` updates

In the "Atmosphere Scattering" section, immediately after the existing
bullet describing `atmosphere-scattering-2`'s Phase 4 interpretation-mode
fix (the one ending "...see 'Atmosphere Scattering' below for the full
writeup" or similar — locate the exact current bullet by searching this
file for `"AtmosphereAerialPerspective"` before editing), add a new bullet:

- Describe that `atmosphere-scattering-3` (this campaign) found and fixed a
  SECOND, complementary bug in the same preview path: the generic
  `ComputeVolumeCameraSetup()`'s texel-count-proportional box sizing/generic
  isometric camera angle, while correct for an actual spatial volume,
  produced a nearly-flat, unreadable preview for THIS LUT specifically,
  because its Z axis is a camera-distance SLICE index, not a comparable
  physical length to its X/Y screen-column/row indices.
- Name the new function(s) added:
  `ComputeAtmosphereAerialPerspectivePreviewCameraSetup()`
  (`VolumeTexturePreviewMath.h/.cpp`, Phase 2) and
  `FrustumProxy`/`IntersectRayFrustum()`/`MapFrustumLocalPositionToUvw()`/
  `ComputeAtmosphereAerialPerspectivePreviewFrustum()` (same files, Phase 3,
  mandatory)
  plus `VolumeTexturePreview.comp`'s new `shapeMode` push-constant branch.
- State explicitly, mirroring this file's own existing precedent bullets'
  tone: this is auto-selected by the SAME `texture_name`-prefix check
  `Application.cpp` already used for the color-interpretation mode — no new
  HTTP parameter, no `Application.cpp` changes at all — and every OTHER
  (non-Aerial-Perspective) volume texture is completely unaffected, still
  resolving through the original, untouched `ComputeVolumeCameraSetup()`/
  `IntersectRayBox()` path.
- Cross-reference `task_manager/atmosphere-scattering-3/PHASE0_MASTER_STRATEGY.md`
  by name, matching every other campaign's own self-citation convention in
  this file.

Separately, in the same "Atmosphere Scattering" section, locate the existing
bullet describing `src/Editor/AtmosphereAerialPerspectiveLutInspection.h/.cpp`
(the "Inspect Aerial Perspective LUT" numeric readback tool — search this
file for `"reports live min/max/mean transmittance"`) and add a short
clause noting that Phase 1 of THIS campaign (`atmosphere-scattering-3`)
extended it to also report Near/Mid/Far per-BAND transmittance/in-scattering
means (`AerialPerspectiveBandSummary`), not just the whole-volume min/max/
mean — this is what let this campaign confirm, as a permanent checked fact,
that the LUT's own near/far gradient is real data, not just a preview-side
illusion (see `PHASE1_ROOT_CAUSE_INSTRUMENTATION_AND_REGRESSION_TESTS.md`).

In "Networking" -> "Named Texture Capture", add a short cross-reference
sentence to the `VolumeTexturePreviewRenderer`/`VolumeTexturePreviewMath.h`
bullet, pointing at the new `atmosphere-scattering-3` campaign for the
Aerial-Perspective-specific camera/shape framing addition, mirroring how
that same bullet already cross-references `atmosphere-scattering-2`'s
Phase 4 color-interpretation addition.

### 3.2 — `README.md` updates

Read the current `README.md` "Status" section first. If it already lists
the Aerial Perspective volume HTTP-preview capability, append a short,
user-facing note (one to two sentences, matching this section's existing
level of detail — no code snippets, no file paths) that its visual framing
was corrected in a follow-up pass so it now actually shows the volume's
near/far haze gradient. If no existing entry covers this feature at all,
do not invent a large new section — a minimal, consistent-with-neighboring-
entries addition is enough.

### 3.3 — Zero-regression confirmation (code-level, since no second real volume texture exists to test against live)

Since this engine has exactly ONE real, live `VolumeTextureHandle` producer
today (the Aerial Perspective volume, both Game/Scene view instances) there
is no SECOND, generic volume texture available to send a live
zero-regression HTTP request against. Confirm zero regression instead via:
- Direct inspection: every `interpretation ==
  VolumeTexturePreviewInterpretation::GenericDensityInAlpha` branch added by
  Phases 2/3 must read, byte-for-byte, the exact pre-campaign code
  (`ComputeVolumeCameraSetup()`, `IntersectRayBox()`, the original
  `uvw = (localPos / pc.boxHalfExtents) * 0.5 + 0.5` formula, `shapeMode`
  left at its own default `0` — Phase 3's frustum path is only ever reached
  when `shapeMode == 1`, which only ever happens for the Aerial Perspective
  interpretation).
- The Tier-1 tests already added in Phases 1-3
  (`VolumeTexturePreviewMathTests.cpp`) already assert this indirectly (the
  generic function's own existing tests are all left unmodified and still
  pass) — re-run them one final time as part of this phase's own full test
  pass (3.4 below) rather than re-deriving new tests here.
- A live HTTP re-confirmation IS still possible and should still be
  performed: `GET /get_texture?texture_name=Swapchain` (a 2D texture,
  completely outside this campaign's own volume-texture code path) must
  still return its normal capture, unaffected — this is the same
  "known-good 2D texture as a sentinel" check every earlier phase in this
  campaign already performed.

### 3.4 — Full build and full regression test (the one phase allowed to do this)

- `cmake --build build` (or the project's equivalent full incremental build
  command) for BOTH the `GreatTamanaEngine` executable AND
  `GreatTamanaEngineTests`.
- `cd /d <repo>\build && ctest -C Debug --output-on-failure` — the FULL
  suite this time, not a filtered subset (unlike every previous phase in
  this campaign). Treat any newly-failing test as a real regression to
  fix, not something to loosen — matches `AGENTS.md`'s own "Testability &
  Regression Safety" section's standing rule.
- One final live smoke check: `run_app_background`, `gte_send_request`
  `GET /get_texture?texture_name=AtmosphereAerialPerspectiveVolume_GameView`
  for the campaign's own final "after" screenshot, plus `GET /get_swapchain`
  and `GET /list_textures` as general engine-health sentinels, then
  `stop_app_background`.

### 3.5 — Campaign completion report

Write `CAMPAIGN_COMPLETION_REPORT.md` (in this same folder,
`task_manager/atmosphere-scattering-3/`) summarizing, in order: the original
problem (with the actual "before" screenshot description from this
strategy's own investigation), the root cause found, what each phase
actually did (cross-referencing each `PHASE<n>_COMPLETION_REPORT.md`), the
final visual result (the "after" screenshot description), and the final
full build/test pass result from 3.4.

## Verification

- This phase's own verification IS its Step 3.4 above (the one deliberate
  exception to every other phase's "fast, targeted compile only" rule in
  this campaign).
- Write `PHASE5_COMPLETION_REPORT.md` AND `CAMPAIGN_COMPLETION_REPORT.md`
  (both, per 3.5 above), then commit — this final commit closes out the
  `atmosphere-scattering-3` campaign.
