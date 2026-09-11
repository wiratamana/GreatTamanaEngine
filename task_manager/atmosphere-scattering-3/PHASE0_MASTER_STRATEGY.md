# PHASE0_MASTER_STRATEGY — "Fix the Aerial Perspective 3D LUT preview" (atmosphere-scattering-3 campaign)

Orchestrator document. Every other `PHASE<n>_*.md` file in this same folder
(`task_manager/atmosphere-scattering-3/`) is a child task of this one. Read
this file FIRST, then read the current `PHASE<n>` file the "Task Status"
list below points you at. Always read the previous phase's own
`PHASE<n>_COMPLETION_REPORT.md` (once it exists) before starting the next
phase — it may contain corrections/clarifications that supersede a stale
assumption in this master file.

Read `AGENTS.md` and `README.md` at the project root first (they document
this engine's own coding conventions and the full "Atmosphere Scattering"/
"Networking" feature history this campaign builds directly on top of), then
skim `task_manager/atmosphere-scattering-1/` (the original 9-phase campaign
that built the Aerial Perspective froxel volume itself),
`task_manager/atmosphere-scattering-2/` (the campaign that rebalanced its
physical visibility AND shipped the "atmosphere-aware" color interpretation
for its HTTP preview — `AERIAL_PERSPECTIVE_INVESTIGATION_FINDINGS.md` and
`PHASE4_ATMOSPHERE_AWARE_VOLUME_DEBUG_PREVIEW.md` are the two most load-
bearing prior documents for THIS campaign), and
`task_manager/network-impl-6/` (the campaign that built the generic
"fetch a 3D texture's raymarch thumbnail over HTTP" capability this whole
feature is built on).

## Task Status

| Phase | File | One-line goal |
|---|---|---|
| 0 | `PHASE0_MASTER_STRATEGY.md` | This file — orchestrator. |
| 1 | `PHASE1_ROOT_CAUSE_INSTRUMENTATION_AND_REGRESSION_TESTS.md` | Codify the root cause as permanent, automated, Tier-1 evidence (a characterization test on the current geometry bug, plus a numeric per-band data-inspection upgrade) — no visual/behavior change yet. |
| 2 | `PHASE2_ATMOSPHERE_AWARE_PREVIEW_CAMERA_FRAMING.md` | **THE primary fix.** Give the Aerial Perspective volume's HTTP preview its own dedicated camera + proxy-box shape (no shader changes) so the depth axis is no longer squashed into invisibility. |
| 3 | `PHASE3_FRUSTUM_SHAPED_RAYMARCH_PROXY.md` | **MANDATORY** (confirmed with the project owner). Replace the axis-aligned box proxy with a literal tapering FRUSTUM proxy, matching the reference image's widening-funnel shape exactly. |
| 4 | `PHASE4_VISUAL_TUNING_AGAINST_REFERENCE_IMAGE.md` | Iteratively tune the new constants (exposure/density/step-count/camera angle/fan factor) against the reference image via real rebuild+screenshot+compare loops. |
| 5 | `PHASE5_DOCS_REGRESSION_SAFETY_AND_FINAL_BUILD.md` | `AGENTS.md`/`README.md` updates, zero-regression confirmation, one full incremental build + full test run, campaign completion report. |

## Step 1 — The Goal (Where are we going?)

Make `GET /get_texture?texture_name=AtmosphereAerialPerspectiveVolume_GameView`
(and `..._SceneView`) — the engine's existing, already-shipped "fetch a live
3D texture's raymarch thumbnail over HTTP" capability, purpose-built for an
LLM/AI agent doing live visual debugging (`network-impl-6` campaign,
extended by `atmosphere-scattering-2`'s Phase 4) — actually **look like
what an Aerial Perspective LUT is supposed to look like**: a camera-frustum-
shaped volume that visibly recedes away from the camera, with color/haze
density genuinely changing from "clear" near the camera to "hazy" far away,
the same concept the reference paper diagram
(`task_manager/atmosphere-scattering-3/aerial-persepective-lut-3d-texture.png`
— a schematic of a camera on the left with a fan of increasingly-hazy,
increasingly-large quads receding to the right) illustrates.

Concretely, once this campaign is done, fetching that endpoint must produce
an image that a human or LLM agent looking at it, side-by-side with the
reference PNG, would recognize as "the same KIND of thing" — a receding,
widening, progressively-hazier volume — not the flat, nearly-featureless,
thin blue rectangle this session's own investigation (see Step 2 below)
found it actually produces today.

## Step 2 — The Situation / The Problem (Where are we now?)

### 2.1 — What this session actually did, and actually found (live investigation, not guesswork)

This session ran the real, currently-committed engine
(`build/GreatTamanaEngine.exe`, `run_app_background`) and queried its
already-running embedded HTTP server (`GET /list_textures`,
`GET /get_texture`, `GET /get_swapchain`) to see the CURRENT, real behavior
first-hand, before writing this strategy:

- `GET /list_textures` confirmed both
  `"AtmosphereAerialPerspectiveVolume_GameView"`/`"..._SceneView"` are live,
  registered `"texture3d"` entries, `128x128x32`, exactly as documented.
- `GET /get_texture?texture_name=AtmosphereAerialPerspectiveVolume_GameView`
  returned a real, valid PNG — but visually, it is a small, thin, nearly
  uniform DARK-BLUE PARALLELOGRAM floating in the preview's dark-gray
  background, with **no visible internal gradient, no visible "receding"
  structure, and no resemblance at all** to the reference diagram's widening,
  progressively-hazier fan of quads. It looks like a flat, edge-on slab, not
  a volume.
- For comparison, `GET /get_texture?texture_name=AtmosphereSkyViewLut_GameView`
  (a 2D LUT, unrelated to this bug) rendered a perfectly normal-looking sky
  gradient (blue sky fading to a warm horizon glow, black ground below) —
  confirming the ENGINE's rendering/HTTP/PNG pipeline in general is healthy;
  the problem is narrowly scoped to the 3D volume preview's own raymarch
  camera/shape.
- `GET /get_texture?texture_name=AtmosphereAerialPerspectiveVolumeDebugSlice`
  (the OLDER, unrelated, single-Z-slice-only debug mirror from the original
  `atmosphere-scattering-1` campaign's Phase 9) showed a plausible sky-blue
  top half / solid-black bottom half split — consistent with a real,
  non-degenerate, non-broken underlying LUT (this is a SEPARATE, narrower
  debug feature from the one this campaign is fixing — see `AGENTS.md`,
  "Named Texture Capture", for why the two are complementary, not the same
  thing).

### 2.2 — Root cause, found by reading the real, current source (not speculation)

The bug is **entirely in the generic HTTP volume-preview RENDERER's fixed
camera/proxy-box math — never in the Aerial Perspective LUT's own generation
shader, never in a data/physics bug.** Specifically:

1. `src/Renderer/VolumeTexturePreviewMath.h`'s `ComputeVolumeCameraSetup(int
   width, int height, int depth)` computes the raymarch proxy box's own
   half-extents as:
   ```cpp
   setup.boxHalfExtents = Vec3(w, h, d) * (0.5f / maxDim);
   ```
   i.e. **directly proportional to the volume's own raw TEXEL counts.** This
   is the textbook-correct thing to do for a genuine spatial volume (a smoke/
   cloud `Texture3D`, where X/Y/Z all measure the SAME kind of physical
   length) — but the Aerial Perspective volume is `128x128x32`
   (`AtmosphereLutRenderer.cpp`'s `kAerialPerspectiveVolumeWidth/Height/Depth`),
   and its three axes are **not comparable units at all**:
   `AtmosphereAerialPerspectiveVolume.comp`'s `main()` makes unambiguously
   clear that X/Y are **screen-space froxel COLUMN/ROW indices** (angular,
   one thread per `(x,y)` column) while Z is the **camera-relative DISTANCE
   slice index** (looped `for (int slice = 0; slice < volumeSize.z;
   ++slice)`, non-linearly spaced via `FroxelSliceToViewDepth()`'s own
   quadratic `depthExponent`). Feeding `(128, 128, 32)` into a formula that
   assumes "these are three comparable physical lengths" produces
   `boxHalfExtents = (0.5, 0.5, 0.125)` — **the ONE axis that carries this
   LUT's entire near/far story (Z) is squashed to exactly 1/4 the size of
   the other two.**
2. `ComputeVolumeCameraSetup()` then places a single, FIXED, generic
   "true isometric" camera (45° azimuth, ~35.26° elevation) around this box
   — an angle that was never chosen with an extremely anisotropic,
   already-paper-thin box in mind. The combination of (a) an already-4x-
   flattened box and (b) a camera angle not specifically chosen to look
   ACROSS that flattened axis is what produces the "thin, nearly flat,
   nearly featureless rectangle" this session's own screenshot shows: the
   camera ends up looking almost face-on at the box's large `(X,Y)` faces,
   with the ENTIRE depth (near/far haze) story compressed into a sliver too
   thin to read visually.
3. This is confirmed to be a **preview/visualization-only** defect, not a
   data defect: `AGENTS.md`'s own "Atmosphere Scattering" section already
   cites a live, numeric readback (via `src/Editor/
   AtmosphereAerialPerspectiveLutInspection.h/.cpp`'s whole-volume min/max/
   mean tool) confirming the CURRENT, post-rebalance LUT genuinely has a
   real, non-trivial spatial gradient (minimum transmittance ~0.71 i.e. up to
   ~29% haze at the far end, vs. ~1.0/no haze near the camera; maximum
   in-scattering magnitude ~0.0039, comfortably above the tool's own
   "likely visible" threshold). The data is fine. The picture drawn from it
   is not.
4. `atmosphere-scattering-2`'s own Phase 4
   (`PHASE4_ATMOSPHERE_AWARE_VOLUME_DEBUG_PREVIEW.md`) already correctly
   diagnosed and fixed a DIFFERENT, real problem in this same code path — the
   raw texel `alpha`/`rgb` channels being mis-interpreted as a generic
   density/color pair (transmittance is the OPPOSITE polarity of a density,
   and raw HDR in-scattering is too dim to see without exposure/tonemapping)
   — and that fix (`interpretationMode`/`aerialPreviewExposure` in
   `VolumeTexturePreviewRenderer.cpp`/`VolumeTexturePreview.comp`) is
   confirmed, by this session's own direct code reading, to still be present
   and correctly wired (`Application.cpp`'s `isAerialPerspectiveVolume`
   check, line ~962). **That fix and this campaign's fix are two genuinely
   different, complementary bugs** — Phase 4 fixed WHAT COLOR each voxel
   reads as; this campaign fixes WHAT SHAPE/ANGLE the box carrying those
   voxels is drawn as. Neither one alone was ever going to be enough.

### 2.3 — Why this matters for the fix's design

Because the defect is 100% inside `VolumeTexturePreviewMath.h/.cpp` +
`VolumeTexturePreviewRenderer.cpp`'s own CPU-side camera/box setup, **the
Aerial Perspective LUT's own generation/composite shaders
(`AtmosphereAerialPerspectiveVolume.comp`,
`AtmosphereAerialPerspectiveComposite.comp`) and the physical model
(`AtmosphereMath.h`) must NEVER be touched by this campaign** — touching them
would violate `AGENTS.md`'s own "CPU oracle is right by definition" rule for
zero benefit, since they are not where the bug lives. Every fix in this
campaign lives in the PREVIEW/VISUALIZATION path only:
`VolumeTexturePreviewMath.h/.cpp`, `VolumeTexturePreviewRenderer.h/.cpp`,
`VolumeTexturePreview.comp`, and (Phase 1 only) `src/Editor/
AtmosphereAerialPerspectiveLutInspection.h/.cpp` for extra numeric
confirmation tooling.

## Step 3 — The Plan (How do we get there?)

Five phases, in this strict order (every phase is a required dependency of
the next one — Phase 3, in particular, is a MANDATORY deliverable of this
campaign, confirmed with the project owner, NOT an optional stretch goal;
Phase 4 assumes Phase 3's frustum shape is the FINAL production shape):

1. **`PHASE1_ROOT_CAUSE_INSTRUMENTATION_AND_REGRESSION_TESTS.md`** — write
   permanent, automated, Tier-1 tests that codify the exact geometry defect
   described in 2.2 above (so it is a checked fact, not just this
   document's prose), plus extend the existing whole-volume-only numeric
   inspection tool to report PER-BAND (near/mid/far third) statistics —
   strengthening the "this is a preview bug, not a data bug" conclusion with
   permanent, reusable tooling future phases (and future regressions) can
   both lean on.
2. **`PHASE2_ATMOSPHERE_AWARE_PREVIEW_CAMERA_FRAMING.md`** — add a SECOND,
   dedicated camera + proxy-box setup function used ONLY for the Aerial
   Perspective volume (auto-selected the exact same way
   `atmosphere-scattering-2`'s Phase 4 already auto-selects its color-
   interpretation mode — by `texture_name` prefix, zero new HTTP parameter),
   which gives the depth axis real, dominant visual weight and views it from
   an angle that actually reveals the near/far gradient. **Zero shader
   changes.** This is the primary fix — after this phase, the preview should
   already look meaningfully closer to the reference image.
3. **`PHASE3_FRUSTUM_SHAPED_RAYMARCH_PROXY.md`** (**MANDATORY**) — replace the
   still-axis-aligned (even if now depth-elongated) box from Phase 2 with a
   literal tapering FRUSTUM proxy (new ray/half-space intersection math,
   still a fixed-shape CPU oracle mirrored by hand into
   `VolumeTexturePreview.comp`, exactly like every other shader in this
   engine), so the rendered preview actually WIDENS away from the camera,
   matching the reference image's fan-of-quads look as closely as a single
   continuous raymarch reasonably can.
4. **`PHASE4_VISUAL_TUNING_AGAINST_REFERENCE_IMAGE.md`** — a real, iterative
   "change a constant, rebuild, screenshot, compare against the reference
   PNG, adjust" tuning loop over the new camera angle/box or frustum
   proportions/exposure/density-scale/step-count, ending with the final
   chosen values written down together with WHY (mirroring
   `atmosphere-scattering-2`'s own `aerialPerspectiveScatteringExaggeration`
   tuning precedent).
5. **`PHASE5_DOCS_REGRESSION_SAFETY_AND_FINAL_BUILD.md`** — `AGENTS.md`/
   `README.md` updates, an explicit zero-regression confirmation (every
   OTHER/future generic volume texture must still resolve through the
   ORIGINAL, unchanged `ComputeVolumeCameraSetup()`/box-intersection path,
   byte-for-byte identical to before this campaign), one full incremental
   build + full existing test run (the one phase in this campaign allowed to
   do so), and a campaign completion report.

### Locked design decisions (do not re-litigate these while implementing)

1. **No new HTTP endpoint, no new query parameter.** Exactly like
   `atmosphere-scattering-2`'s Phase 4, every new behavior in this campaign
   is auto-selected server-side by matching `texture_name`'s prefix against
   the literal string `"AtmosphereAerialPerspectiveVolume"` — the exact same
   check already sitting in `Application.cpp` (`isAerialPerspectiveVolume`,
   line ~962) that Phase 4 of `atmosphere-scattering-2` added for the color-
   interpretation switch. Reuse it; do not add a second, slightly-different
   copy of this same string check.
2. **Every OTHER (hypothetical future, non-Aerial-Perspective) volume
   texture must keep using the ORIGINAL, untouched `ComputeVolumeCameraSetup()`
   + `IntersectRayBox()` path, with byte-for-byte identical output to before
   this campaign.** This campaign only ever ADDS a second, parallel code
   path selected by name — it never modifies the generic one's own behavior.
3. **The Aerial Perspective LUT's own generation (`AtmosphereAerialPerspectiveVolume.comp`)
   and composite (`AtmosphereAerialPerspectiveComposite.comp`) shaders, and
   `AtmosphereMath.h`/`AtmosphereCommon.glsl`, are OUT OF SCOPE and must not
   be modified by this campaign** — see Step 2.3 above for why.
4. **This is still a rare, human/LLM-triggered debug-tooling feature, not a
   per-frame render path.** Every existing performance/complexity trade-off
   already accepted for this endpoint (`Renderer::WaitForGpuIdle()` once per
   request, an extra `ImmediateSubmit()` dispatch, etc. — see `AGENTS.md`'s
   "Named Texture Capture" section) is unaffected and still fully accepted;
   this campaign does not need to, and must not try to, make this path
   cheaper/faster.
5. **No Editor UI panel/inspector work of any kind.** Exactly like
   `network-impl-6`'s own Locked Design Decision 2 — this remains a pure
   HTTP/LLM-agent debugging capability.
6. **Phase 3 (the frustum-shaped proxy) is a MANDATORY deliverable of this
   campaign — confirmed directly with the project owner.** It must not be
   skipped, deferred, or silently downgraded back to Phase 2's simpler
   elongated box. `PHASE3_FRUSTUM_SHAPED_RAYMARCH_PROXY.md` is written with
   full implementation detail specifically because it is required, not a
   "nice to have" — its own completion report must confirm the frustum
   shape actually shipped.
7. **Follow this codebase's existing "no full build/full regression test
   until the last phase" convention** (see `AGENTS.md`'s own campaign
   precedents) — Phases 1-4 each only need a fast, targeted compile +
   targeted test/manual smoke check; Phase 5 is the one phase allowed (and
   expected) to run a full build and the full existing test suite.
8. **Every phase writes its own `PHASE<n>_COMPLETION_REPORT.md`** in this
   same folder once its own compile check (and, where applicable, targeted
   test run) passes, then commits (code + report) to git — never bundle two
   phases into one commit.

### Cross-phase file map (every file this campaign is expected to touch)

**Modified files (Phase 1 only) — NOTE: despite this campaign's file map
being organized by "which phase(s) touch it", NONE of the files below are
literally new — every one already exists from an earlier campaign
(`network-impl-6`/`atmosphere-scattering-2`); this phase only adds new
functions/fields/test cases inside them:**
- `tests/Renderer/VolumeTexturePreviewMathTests.cpp` (new test cases added;
  file already exists from `network-impl-6`).
- `src/Editor/AtmosphereAerialPerspectiveLutInspection.h/.cpp` (files already
  exist from `atmosphere-scattering-2`).
- `tests/Editor/AtmosphereAerialPerspectiveLutInspectionTests.cpp` (file
  already exists from `atmosphere-scattering-2`).

**Modified files (Phases 2-4):**
- `src/Renderer/VolumeTexturePreviewMath.h`
- `src/Renderer/VolumeTexturePreviewMath.cpp`
- `src/Renderer/VolumeTexturePreviewRenderer.h`
- `src/Renderer/VolumeTexturePreviewRenderer.cpp`
- `src/Shaders/VolumeTexturePreview.comp` (Phase 3 only)

**Modified files (Phase 5):**
- `AGENTS.md`, `README.md`

**Never touched by this campaign, any phase:**
- `src/Shaders/AtmosphereAerialPerspectiveVolume.comp`
- `src/Shaders/AtmosphereAerialPerspectiveComposite.comp`
- `src/Renderer/Atmosphere/AtmosphereMath.h/.cpp`
- `src/Renderer/Atmosphere/AtmosphereCommon.glsl`
- `src/Renderer/Atmosphere/AtmosphereLutRenderer.h/.cpp` (Phase 1 only reads
  from it via the already-existing `AtmosphereAerialPerspectiveLutInspection`
  tool — it does not modify `AtmosphereLutRenderer` itself)
- `src/Application/Application.cpp` (the existing `isAerialPerspectiveVolume`
  detection + `RenderPreview()` call site is already correct and complete —
  no phase in this campaign needs to change it; Phase 2/3 only change what
  `RenderPreview()` does INTERNALLY based on the `interpretation` value it
  already receives)

### Reference material this campaign should keep open while implementing

- `task_manager/atmosphere-scattering-3/aerial-persepective-lut-3d-texture.png`
  — the target look (a receding, widening, progressively-hazier fan of
  quads/frustum, seen from the side).
- `task_manager/atmosphere-scattering-2/AERIAL_PERSPECTIVE_INVESTIGATION_FINDINGS.md`
  — the prior campaign's own "why is there no effect at all" investigation
  (a DIFFERENT, already-fixed problem — physical visibility — but essential
  background for why the LUT's real numbers are what they are today).
  `task_manager/atmosphere-scattering-2/PHASE4_ATMOSPHERE_AWARE_VOLUME_DEBUG_PREVIEW.md`
  — the prior, already-shipped, complementary color-interpretation fix this
  campaign builds directly on top of (never re-implement any part of it).
- `task_manager/network-impl-6/PHASE0_MASTER_STRATEGY.md` and
  `PHASE3_VOLUME_RAYMARCH_PREVIEW_RENDERER.md` — the original design of the
  generic raymarch preview renderer/math this campaign extends.
- `AGENTS.md`'s "Atmosphere Scattering" and "Networking" -> "Named Texture
  Capture" sections — the authoritative, currently-accurate description of
  every piece of machinery this campaign touches.
