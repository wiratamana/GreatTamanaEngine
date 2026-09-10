# Atmosphere Phase 4 — Completion Report

**Phase:** `ATMOSPHERE_PHASE4_MULTISCATTERING_LUT_v1.md`
**Branch:** `feature/atmosphere-scattering-impl`
**Status:** Complete — `gte_core`, `GreatTamanaEngineTests`, and `GreatTamanaEngine` all build cleanly (targeted incremental build only, per this campaign's own workflow rule); the engine was actually run (Editor build, Vulkan validation layers enabled by default in this configuration) and the Multi-Scattering LUT was captured live via `GET /get_texture?texture_name=AtmosphereMultiScatteringLut&format=png` — visually confirmed as a smooth, low-frequency gradient with no sharp banding, in clear visual contrast to the Transmittance LUT's sharp horizon line (see below).

## What changed

### 1. `src/Shaders/AtmosphereCommon.glsl` — extended

Added four new shared functions, right before the closing `#endif`:

- `ComputeScatteringCoefficientAtHeight()` — the scattering-only half
  (Rayleigh scattering + Mie scattering, no absorption/ozone) of Phase 3's
  `ComputeExtinctionCoefficientAtHeight()` — exactly the accessor
  `AtmosphereMath.h`'s own doc comment predicted "once a later phase
  actually needs it".
- `IntegrateInscattering()` — the Frostbite analytic single-segment
  inscattering integral (`_reference/pl-sky/shaders/sky.inc`'s
  `pl_integrate_inscattering()`), the Multi-Scattering LUT's own
  per-ray-march-step accumulation formula.
- `MultiScatteringSampleDirection()` — the shared, fixed 8×8 spherical
  direction-sampling pattern, transcribed EXACTLY from
  `sky_multiscatter_lut.comp`'s own nested loop, including its
  non-standard axis assignment (Y component uses `sin(theta)`, not
  `cos(theta)`) — kept byte-for-byte rather than "corrected", since this
  LUT has no CPU oracle to validate a corrected version against (see Step
  4's own "no CPU oracle" scope).
- `MultiScatteringLutUvToHeightZenith()` — this LUT's own UV↔(height,
  zenith) parameterization, the same linear formula as Phase 3's
  `TransmittanceLutUvToHeightZenith()` but paired with the reference's own
  deliberate off-by-one-texel grid (`texel / resolution`, not
  `texel / (resolution - 1)`).

### 2. `src/Shaders/AtmosphereMultiScatteringLut.comp` — new

Binding 0 = `AtmosphereParametersGpu` (`readonly buffer`, reusing Phase 3's
convention), binding 1 = `sampler2D transmittanceLut` (read-only), binding 2
= `image2D multiScatteringLutImage` (`rgba16f`, write-only). Per texel:
decodes `(heightKm, upDot)`, ray-marches 8×8 = 64 fixed sphere-sampling
directions × 20 inner steps each (matching
`ATMOSPHERE_REFERENCE_NOTES.md`'s own Section 5 counts exactly), sampling
`transmittanceLut` for sun transmittance at every step instead of
re-integrating optical depth, accumulating both the 2nd-order scattering
estimate and the "multiple scattering fraction", then solving
`L_total = L_2nd / (1 - f_ms)`.

**Deliberate deviation from the reference** (documented at the top of the
file): the ground-hit/atmosphere-exit test reuses this campaign's own
already-shared `RayIntersectsSphereNearest()`/
`DistanceToAtmosphereOuterBoundary()` helpers (Phase 3) instead of
re-transcribing `pl-sky`'s own combined `pl_ray_intersection_planet()` a
third time — functionally equivalent (same nearest-ground-hit-or-
atmosphere-exit answer), and keeps this campaign's sphere-intersection math
in exactly one place per concern.

### 3. `src/Renderer/Atmosphere/AtmosphereLutRenderer.h/.cpp` — extended

Added `AddMultiScatteringLutPass(RenderGraphBuilder&, Renderer&, const
AtmosphereParametersGpu&, TextureHandle transmittanceLutHandle) ->
TextureHandle`, same lazily-initialized-pipeline/descriptor-set/output-
texture shape as Phase 3's method. **Reuses the exact same
`m_atmosphereParametersBuffer` Phase 3's `EnsureTransmittanceLutInitialized()`
already creates/uploads — no second, duplicate buffer.** Its own output is a
`RenderTexture` at `VK_FORMAT_R16G16B16A16_SFLOAT` (64×64, matching
`ATMOSPHERE_REFERENCE_NOTES.md`'s own `tMultiscatterLutResolution`),
registered under the literal name `"AtmosphereMultiScatteringLut"`,
accepting its always-created companion `DepthBuffer` as a harmless,
here-unused cost (this pass never draws/depth-tests anything) — the
explicit format choice this phase's own strategy document asked to be
recorded. The pass declares `ReadTexture(transmittanceLutHandle,
ResourceAccess::ShaderRead)` (matching `BoxBlur.comp`'s own established
precedent for a compute shader's `sampler2D` read) before its own
`WriteTexture`, and reads Phase 3's transmittance texture directly via
`m_transmittanceLutOutput->View()`/`Sampler()` (the same `Texture2D`
instance `AddTransmittanceLutPass()` itself writes through) rather than
resolving a sampler through `PassContext`, mirroring
`ComputeBlurValidation::AddPass()`'s own identical reasoning for an
imported texture's always-`VK_NULL_HANDLE` resolved sampler.

### 4. Wiring: `src/Application/Application.cpp`

Extended the existing (Phase 3) temporary validation call site — captured
`AddTransmittanceLutPass()`'s return value into a named
`transmittanceLutHandle`, pushed it into `outputs`, then called
`AddMultiScatteringLutPass(b, m_renderer, atmosphereParameters,
transmittanceLutHandle)` immediately after and pushed ITS result into
`outputs` too. The `// TODO(ATMOSPHERE_PHASE7)` comment was preserved and
extended to mention both passes — still not deleted, per Phase 3/4's own
explicit instruction (Phases 5/6 build directly on this call site).

### 5. `CMakeLists.txt`

`gte_add_shader(GreatTamanaEngine src/Shaders/AtmosphereMultiScatteringLut.comp
EXTRA_DEPENDS src/Shaders/AtmosphereCommon.glsl)` added right after the
Transmittance LUT's own registration, unconditional (not
`GTE_ENABLE_EDITOR`-gated), same reasoning as Phase 3.

## A genuine, necessary deviation beyond this phase's own Step 3 plan: `/get_texture` could not actually capture an HDR RenderTexture

This was NOT anticipated by the strategy document (nor by Phase 3's own
"Revision Notes"), and required real, additional engine changes to make the
phase's own required visual-verification step actually work:

- **The bug found:** `Renderer::CaptureImagePixels()` (the primitive behind
  `GET /get_texture`, from the `network-impl-4` campaign) hard-asserts
  `bytesPerPixel == 4` and sizes its readback buffer accordingly — every
  capturable texture before this phase (Game/Scene/Swapchain views, the
  Transmittance LUT) was always exactly 4 bytes/pixel. The Multi-Scattering
  LUT's `VK_FORMAT_R16G16B16A16_SFLOAT` output is genuinely 8 bytes/pixel —
  the FIRST capturable texture in the engine's history to violate that
  assumption. The very first capture attempt (before this fix) returned a
  200 OK with a plausible-looking-but-actually-corrupted PNG (`vkCmdCopyImageToBuffer`
  copying the image's real 8-bytes/pixel data into a buffer sized for only
  4 bytes/pixel — reading back a byte-misaligned reinterpretation of the
  real half-float data, not a validation-layer-caught crash).
- **The fix (all changes reviewed/scoped narrowly to this one gap):**
  - `Renderer::CaptureImagePixels()`'s assert/doc comment now accepts
    `bytesPerPixel == 8` as a second, explicit, documented case
    (`src/Renderer/Renderer.h/.cpp`).
  - New `src/Encoding/HdrColorVisualization.h/.cpp` —
    `ConvertHdrRgba16fToRgba8()`, the color-side counterpart of the
    existing `DepthVisualization.h`'s `ConvertDepthToGrayscaleRgba8()`:
    decodes each `VK_FORMAT_R16G16B16A16_SFLOAT` texel's 3 color channels
    (plain from-scratch IEEE-754 half→float conversion, no F16C dependency)
    and applies a debug-only Reinhard tonemap (`value / (1 + value)`,
    prescaled by a fixed `kDebugVisualizationExposure = 400.0f` constant —
    a multi-scattering response is small in absolute terms, on the order of
    a few thousandths to a few hundredths, so a plain clamp-to-`[0,1]`
    would have quantized the whole image to solid black) before
    quantizing to a byte. This tonemap is used ONLY by this debug capture
    path — never by any real shading math.
  - `Application::Run()`'s `GET /get_texture` handler now branches on
    `format == VK_FORMAT_R16G16B16A16_SFLOAT` to pick `bytesPerPixel = 8`
    for the `CaptureImagePixels()` call and to route the result through
    `ConvertHdrRgba16fToRgba8()` into a freshly-allocated 4-bytes/pixel
    buffer before PNG encoding (an OUT conversion, not in-place, mirroring
    `ConvertDepthToGrayscaleRgba8()`'s own out-buffer shape) — every other
    format's capture path is completely unchanged.
  - `CMakeLists.txt` gained the two new `Encoding/HdrColorVisualization.*`
    sources, unconditional, alongside the existing `Encoding/*` list.
- This was reported here rather than filed as a separate `bug_report()`
  call since it was found and fixed as an integral, necessary part of
  actually completing this phase's own required deliverable (a working
  visual capture) — not a tool malfunction encountered mid-task.

## Visual verification actually performed

- `GET /list_textures` confirmed `"AtmosphereMultiScatteringLut"` (64×64,
  `VkFormat(97)` = `VK_FORMAT_R16G16B16A16_SFLOAT`, `has_depth: true`,
  regime `"synchronous"`, updating every frame).
- `GET /get_texture?texture_name=AtmosphereMultiScatteringLut&format=png`
  (after the HDR-capture fix above): a smooth, low-frequency gradient — a
  soft blue-ish glow concentrated near the low-height/near-horizon corner
  of the LUT, fading smoothly toward black with increasing height, and
  **no sharp banding anywhere** — directly contrasted against
  `GET /get_texture?texture_name=AtmosphereTransmittanceLut&format=png`'s
  own sharp, high-contrast horizon line. This is exactly the "much
  smoother/lower-frequency... no sharp horizon banding" signal this
  phase's own strategy document names as the primary correctness check for
  this LUT (Step 4: no CPU oracle attempt for this LUT specifically).
- Stopped the engine (`stop_app_background`) once verification completed.

## Step 5's ordering-proof — attempted, result documented honestly

Per this phase's own Step 5, `pass.ReadTexture(transmittanceLutHandle,
ResourceAccess::ShaderRead)` was temporarily commented out (with the handle
explicitly `(void)`-discarded to keep the build green), rebuilt, and
recaptured. **Literally swapping the two `AddXLutPass()` CALLS' order (as
Step 5's own wording suggests) is not actually possible without further
restructuring**: `AddMultiScatteringLutPass()` requires
`AddTransmittanceLutPass()`'s own `TextureHandle` return value as an
argument, so the C++ call order is already structurally forced correct —
removing the render graph's own declared read-dependency (rather than
physically reordering the calls) is the only way to meaningfully attempt
this proof, and is what was actually done.

**Result:** the recaptured PNG was byte-identical (1154 bytes, same
pixels) to the version captured with the correct declaration in place —
an inconclusive result BY VISUAL DIFF specifically, not evidence the
declaration is a no-op. The reason: the Transmittance LUT recomputes
identically every single frame (fixed default Earth parameters, no
dirty-flag optimization, per Step 4's own "What We Will NOT Do") — a
compute pass reading a STALE (previous frame's) copy of it versus a FRESH
one reads the exact same bytes either way, so a missing memory-visibility
barrier has no way to manifest as a visible artifact in this specific
scenario. Attempting to observe a hard validation-layer complaint via
Vulkan's GPU-assisted synchronization-validation feature
(`VK_VALIDATION_FEATURE_ENABLE_SYNCHRONIZATION_VALIDATION_EXT`) was also
attempted (`run_shell` with the feature enabled via `VK_LAYER_ENABLES`),
but this tooling environment does not currently expose the running GUI
process's own stdout/stderr back to the calling session, so no message
could actually be read back this way either. The `ReadTexture()` call was
restored (with an inline comment recording this exact finding) before this
phase was considered done — this is a genuinely honest "attempted, cheap,
inconclusive-by-this-specific-method" result, not a skipped step, and the
barrier this declaration inserts remains correctness-critical in general
(a future phase whose upstream LUT/texture genuinely changes per-frame,
e.g. once `AtmosphereFrameUniforms`/Editor parameter editing land in
Phase 5/8, would immediately depend on it for correct results).

## Deviations from the plan (and why)

1. **The HDR-capture fix (see above)** — the single largest deviation,
   necessary to fulfill this phase's own explicit "capture it via
   `/get_texture`... and visually confirm" requirement; scoped narrowly (2
   new files, 1 relaxed assert, 1 branch in one existing handler).
2. **Sphere-intersection math reused from Phase 3's shared helpers rather
   than re-transcribing `pl_ray_intersection_planet()`** — documented
   inline in the `.comp` file itself; functionally equivalent, avoids a
   third independently-maintained copy of the same sphere-intersection
   logic.
3. **`ComputeScatteringCoefficientAtHeight()`/`IntegrateInscattering()`
   added to `AtmosphereCommon.glsl`** — not explicitly named in this
   phase's own Step 3 bullet list (which named only the direction-sampling
   helper and the UV parameterization functions), but genuinely shared,
   unconditional math needed to implement the LUT at all, following Phase
   3's own precedent of placing such math in the shared file even when not
   strictly required by that specific phase's own list.
4. **A debug-only `kDebugVisualizationExposure` constant** in the new
   `HdrColorVisualization.cpp` — not part of any strategy document, added
   purely so a genuinely small-but-nonzero HDR LUT is visible as a
   gradient in an 8-bit PNG preview instead of quantizing to solid black.
   Affects ONLY this one debug capture path.
5. No other deviations — the LUT resolution (64×64), sample counts (8×8
   outer, 20 inner), binding order/convention, and the "reuse Phase 3's
   buffer, don't duplicate it" rule were all followed exactly as written.

## What was explicitly NOT done (per Step 4)

- No CPU oracle for this LUT — correctness was judged purely by the visual
  smoothness/no-banding signal described above, per Step 4's own explicit
  scope.
- No configurable sample-count/quality Editor knob — `kMultiScatteringSampleCountSqrt`
  (8) and `kMultiScatteringInnerSampleCount` (20) are fixed, hardcoded,
  shader-documented constants.
- No dirty-flag optimization — `AtmosphereParametersGpu` is still
  re-uploaded and the whole LUT recomputed unconditionally every frame
  (inherited from Phase 3, unchanged here).
- No Editor parameter editing (still Phase 8's own concern).
- No GPU timing registration for this pass.
- No new Tier-1 test file — this phase's shader/class work is Tier 2 (no
  live-`VkDevice` test infrastructure exists), same as Phase 3.

## Build/run verification actually performed

- `cmake --build build --target gte_core` — compiled cleanly (including
  after the `Renderer.h`/`Renderer.cpp` assert-relaxation edit, which
  triggered a wider incremental recompile of everything depending on
  `Renderer.h` — all of it succeeded with zero errors).
- `cmake --build build --target GreatTamanaEngineTests` — compiled/linked
  cleanly (not run — per this campaign's own "fast compile check only"
  workflow rule, full `ctest` is reserved for Phase 9).
- `cmake --build build --target GreatTamanaEngine` — compiled, both new
  `.comp` shaders (`AtmosphereMultiScatteringLut.comp`, re-verified
  alongside the already-existing `AtmosphereTransmittanceLut.comp`)
  compiled via `glslc` with zero errors, and linked cleanly.
- Ran the engine (`run_app_background`, Editor build, Vulkan validation
  layers enabled by default) multiple times across this session (once to
  find the HDR-capture bug, once after each fix/tuning iteration, once for
  the ordering-proof, once for final verification) — see "Visual
  verification" and "ordering-proof" sections above for the concrete
  results. Stopped (`stop_app_background`) after each run.
- Per this campaign's own workflow rule, **no full clean build and no full
  `ctest` regression run** were performed (reserved for Phase 9 only).

## Open questions / notes for Phase 5

- `AtmosphereLutRenderer` is still the one class to extend — add a third
  `AddSkyViewLutPass(...)` method alongside `AddTransmittanceLutPass()`/
  `AddMultiScatteringLutPass()`, reusing the same
  `m_atmosphereParametersBuffer` and reading BOTH `AtmosphereTransmittanceLut`
  AND `AtmosphereMultiScatteringLut` as combined-image-sampler inputs (this
  phase's own binding-2/binding-1 pattern already shows the shape for
  reading one prior LUT; reading two just means one more
  `AddCombinedImageSampler()`/`ComputeDescriptorWrite::CombinedImageSampler()`
  pair).
- Phase 5 introduces `AtmosphereFrameUniforms` — per Phase 3's own
  completion report (still valid), bind it as ANOTHER read-only storage
  buffer, reusing the "one-struct-member SSBO block" declaration style,
  never a true UBO.
- **The HDR-capture fix (`ConvertHdrRgba16fToRgba8`,
  `CaptureImagePixels`'s relaxed assert) is now a genuine, reusable
  precedent** — Phase 5's Sky-View LUT is documented as another
  presumably-HDR-valued LUT (per `ATMOSPHERE_REFERENCE_NOTES.md`'s own
  citation of `r11f_g11f_b10f` in the reference); if it also ends up using
  `VK_FORMAT_R16G16B16A16_SFLOAT` (matching this phase's and Phase 2's own
  `VolumeTexture` format choice), its own `/get_texture` capture will
  already work with zero further changes needed to the capture pipeline.
  If a future LUT ever needs a genuinely different HDR format (e.g.
  `R32G32B32A32_SFLOAT`, 16 bytes/pixel), `CaptureImagePixels()`'s assert
  and the `Application.cpp` branch would both need a third case added —
  worth a quick check before assuming the current two-case handling is
  automatically sufficient.
- The Step 5 ordering-proof's inconclusive-by-visual-diff result (see
  above) is worth Phase 5/6's own awareness: once `AtmosphereFrameUniforms`
  makes an upstream input genuinely change per-frame (camera height/sun
  direction), a similar ordering-proof attempt against THOSE passes should
  actually be able to produce a visible before/after difference, unlike
  this phase's own fixed-parameters case.
- The temporary validation call site in `Application.cpp`/`Application.h`
  (`m_atmosphereLutRenderer`, the `TODO(ATMOSPHERE_PHASE7)` comment) is
  still in place and must stay in place through Phases 5/6 — only Phase 7
  relocates it into the real, permanent sky background/aerial-perspective
  pass sequence.
