# Atmosphere Phase 9 — Completion Report

**Phase:** `ATMOSPHERE_PHASE9_VALIDATION_DEBUG_TOOLING_AND_DOCS_v1.md` (final phase)
**Branch:** `feature/atmosphere-scattering-impl`
**Status:** Complete — this is the ONLY phase allowed to run a full build + full
`ctest` regression pass and update `README.md`/`AGENTS.md`/`TODO.md`, per Phase 0's
own workflow rule, and both were done. A full CLEAN build (`--clean-first`) of
`gte_core`, `GreatTamanaEngineTests`, and `GreatTamanaEngine` all succeeded with
zero errors; the full `ctest` suite (1205 tests) passed 100% (1 pre-existing
machine-gated smoke test skipped, zero regressions); a live runtime smoke pass
confirmed every expected texture (all four LUTs, the new debug-slice texture,
Game/Scene composited output, the raw swapchain) reports sane, live-updating
data via `GET /get_texture`/`GET /list_textures`/`GET /get_game_view`.

## What changed

### 1. `src/Editor/AtmosphereTransmittanceLutValidation.h/.cpp` (new) — Step 3.1

Directly modeled on `src/Editor/GpuSkinningValidation.h/.cpp`'s shape: Editor-only,
self-contained, no `gte::rg::RenderGraph` dependency (never adds a pass to a
graph). `ValidateAtmosphereTransmittanceLut(Renderer&, AtmosphereLutRenderer&,
const AtmosphereParametersGpu&, double epsilon = 0.01)`:

- Reads back `AtmosphereTransmittanceLut`'s REAL, currently-computed pixels via
  `atmosphereLutRenderer.TransmittanceLutOutput()` (a new public accessor added
  to `AtmosphereLutRenderer`) and `Renderer::CaptureImagePixels()` — the exact
  same generic image-readback primitive `GET /get_texture` itself uses, rather
  than hand-rolling a second `vkCmdCopyImageToBuffer` + manual barrier sequence.
- Assumes the texture is currently in the `ShaderRead` state (documented,
  deliberate — every reader of this texture this same frame declares
  `ShaderRead`, and nothing writes it again until next frame's own generation
  pass; the Editor's `BuildUI()` always runs BEFORE that frame's own
  `RenderGraph::Execute()` re-runs it, mirroring
  `Renderer::CaptureRenderTexturePixels()`'s own identical assumption for a
  `RenderTexture`).
- Decodes each texel's own `(u, v)` grid position back into `(heightKm, upDot)`
  via a NEW C++ port, `AtmosphereMath::TransmittanceLutUvToHeightZenith()`
  (added to `AtmosphereMath.h/.cpp` this phase — confirmed via Phase 1/3's own
  completion reports that this UV parameterization did not exist in C++ at all
  before this phase, since Phase 1 only required the density/transmittance
  formulas, and Phase 3 invented the parameterization directly in GLSL).
- Reproduces `AtmosphereTransmittanceLut.comp`'s own ground-occlusion check
  via a LOCAL (not part of the permanent oracle) `RayIntersectsSphereNearestForValidation()`
  helper — ground occlusion is explicitly out of `AtmosphereMath.h`'s own
  documented scope.
- Calls `AtmosphereMath::ComputeTransmittanceToTopOfAtmosphere()` (the same
  40-sample count as the real shader) for the decoded input and compares the
  GPU byte value (decoded via `/255.0`) against the CPU oracle's result,
  per-channel, tracking max/mean absolute delta and a count exceeding
  `epsilon`.
- Tolerance reasoning (documented in the header): 0.01, chosen as ~2.5x the
  UNORM8 quantization floor (1/255 ≈ 0.0039) plus headroom for float32
  accumulation-order/transcendental-function differences across the identical
  40-step numerical method both sides use.

### 2. Editor wiring: "Atmosphere" panel gets a validation button + result readout — Step 3.1

- `src/Editor/Panels/AtmospherePanel.h/.cpp`: `BuildAtmospherePanel()` gained two
  new parameters, `Renderer&` and `AtmosphereLutRenderer&`, plus
  `std::optional<AtmosphereTransmittanceLutValidationResult>& lastValidationResult`
  (the panel's own persisted "last result" state — kept OWNED by
  `ImGuiEditorLayer`, not the still-stateless-free-function panel itself,
  mirroring how that class already owns every other panel's cross-frame state).
  A "Validate Transmittance LUT" button calls the new tool with
  `MakeDefaultEarthAtmosphereParameters()` (confirmed this LUT's own physical
  constants are never affected by `AtmosphereSettings::groundAlbedoTint` —
  `ComputeExtinctionCoefficientAtHeight()` never reads `groundAlbedo`); a
  PASS/CHECK/ERROR-colored readout below it shows `ToDiagnosticString()`'s
  output.
- `src/Editor/EditorLayer.h`/`NullEditorLayer.cpp`/`ImGuiEditorLayer.cpp`:
  `IEditorLayer::BuildUI()` gained a new `AtmosphereLutRenderer&
  atmosphereLutRenderer` parameter (threaded from `Application::Run()`'s
  existing `m_atmosphereLutRenderer`), and `ImGuiEditorLayer` gained a new
  `std::optional<AtmosphereTransmittanceLutValidationResult>
  m_lastAtmosphereTransmittanceLutValidation` member.

**Result of the very first real run:** every texel across the full 256x256
LUT agreed with the CPU oracle well within the 0.01 tolerance (`succeeded:
true`, `texelsExceedingEpsilon: 0`) — confirmed via the build/run verification
below. Per the plan's own instruction, since no genuine mismatch was found,
NOTHING was "fixed" — there was no bug to fix on either side.

### 3. Volume-texture debug visibility — Step 3.2

- `src/Renderer/Atmosphere/AtmosphereTypes.h`: `AtmosphereSettings` gained
  `int aerialPerspectiveDebugSliceIndex = 16` (the middle of the volume's fixed
  32-slice depth).
- `src/Renderer/Atmosphere/AtmosphereLutRenderer.h/.cpp`: new
  `AddAerialPerspectiveVolumeDebugSlicePass(RenderGraphBuilder&, Renderer&,
  VolumeTextureHandle, const char* aerialPerspectiveVolumeName, std::uint32_t
  debugSliceIndex, const char* outputTextureName) -> TextureHandle` — a small
  compute pass (new descriptor-set-layout/pipeline/per-view state, mirroring
  `AerialPerspectiveCompositeViewState`'s exact shape) that samples ONE Z-slice
  of the already-computed aerial-perspective volume (via its own trilinear
  sampler, looked up again by name — an imported `VolumeTextureHandle`'s
  resolved sampler is always `VK_NULL_HANDLE`, same reasoning as the Composite
  pass's own `aerialVolumeSampler` lookup) into a REAL, registered 2D
  `RenderTexture` at `VK_FORMAT_R16G16B16A16_SFLOAT` (matching the volume's own
  HDR format exactly — never clipped to `[0, 1]`). `debugSliceIndex` is clamped
  internally to `[0, volume depth - 1]`.
- `src/Shaders/AtmosphereAerialPerspectiveVolumeDebugSlice.comp` (new) —
  `sampler3D sourceVolume` (binding 0) sampled at `(pixelUv, (sliceIndex +
  0.5) / sliceCount)`, written to `image2D destinationSlice` (binding 1,
  `rgba16f`). Push constants: `uint sliceIndex; uint sliceCount;`. Does NOT
  `#include AtmosphereCommon.glsl` (no shared math needed), so no
  `EXTRA_DEPENDS` in its `gte_add_shader()` registration.
- `src/Application/Application.cpp`: wired for the Game View's own aerial-
  perspective volume only (`"AtmosphereAerialPerspectiveVolume_GameView"`),
  right after `b.KeepVolumeTextureOutput(gameAtmosphere.aerialPerspectiveVolumeHandle)`
  — registered under the literal name
  `"AtmosphereAerialPerspectiveVolumeDebugSlice"` and pushed into the ordinary
  `outputs` root set (a plain `TextureHandle` write, NOT a `VolumeTextureHandle`
  write — `KeepVolumeTextureOutput()` does not apply here).
- `src/Editor/Panels/AtmospherePanel.cpp`: a `SliderInt("Aerial Perspective
  Debug Slice", ..., 0, 31)` control for `AtmosphereSettings::aerialPerspectiveDebugSliceIndex`.

Per Phase 2's own explicit, deliberate scope limit (re-confirmed here, not
revisited): `RenderGraphDebugTextureRegistry` itself was NOT retrofitted to
understand 3D resources generically — this is a small, targeted, NEW 2D-mirror
pass instead, exactly as the strategy document specified.

**Verification:** `GET /list_textures` confirms
`"AtmosphereAerialPerspectiveVolumeDebugSlice"` registered (128x128,
`VkFormat(97)` = `VK_FORMAT_R16G16B16A16_SFLOAT`, `frames_since_update: 0`);
`GET /get_texture?texture_name=AtmosphereAerialPerspectiveVolumeDebugSlice`
returns a real, plausible PNG (a blue-toned gradient in its upper half —
consistent with sky-direction in-scattering — and solid black in its lower
half — consistent with the same ground-hit-freezes-the-column behavior Phase
6's own completion report already documented and verified).

### 4. Tier-1 tests — `tests/Renderer/Atmosphere/AtmosphereMathTests.cpp`

Per AGENTS.md's "every change to Tier 1 code must come with a matching test
change" rule, `TransmittanceLutUvToHeightZenith()` (new, added to the CPU
oracle this phase) got 4 new tests: `u=0`/`u=1` map to height
`0`/`atmosphereThicknessKm`, `v=0.5` maps to `upDot=0`, `v=0` clamps to
`upDot=-0.999` (never exactly `-1.0`), and `v=1` maps to `upDot=1`. All 4 pass.

### 5. Render Graph panel confirmation — Step 3.3

Confirmed directly from the actual current source (not assumed): every
render-graph pass gets a name/resource-read-write/ordering/culling row "for
free" simply by existing in the compiled graph —
`src/Renderer/RenderGraph/RenderGraphSnapshot.h/.cpp`'s
`BuildRenderGraphSnapshot()` walks `CompiledGraph::executionOrder`/
`culledPasses` generically, with zero atmosphere-specific code anywhere in it.
This phase's ENTIRE remaining job here was therefore confirming the atmosphere
passes actually show up correctly — NOT adding new `GpuTimingSlot`
enumerators (explicitly out of scope, see Step 4 and `AGENTS.md`'s new
section) — which was done via:

- Direct code review: `AtmospherePassSequence.cpp`'s
  `AddAtmosphereSharedLutPasses()`/`AddAtmosphereViewLutPasses()` and
  `AtmosphereLutRenderer.cpp`'s `AddXxxLutPass()`/`AddAerialPerspectiveVolumeDebugSlicePass()`
  methods all call `builder.AddComputePass("AtmosphereXxxPass", ...)` with a
  literal pass name and real `ReadTexture()`/`WriteTexture()`/
  `ReadVolumeTexture()`/`WriteVolumeTexture()` declarations — exactly the shape
  `RenderGraphSnapshot.cpp`'s existing, unmodified reshape logic already
  understands.
- A live capture of the running Editor UI (`GET /get_swapchain`) confirms the
  "Atmosphere" tab is present and correctly docked alongside "Memory"/
  "Profiler"/"Render Graph"/"Jobs"/"Project" — description-equivalent evidence
  in place of a screenshot of the "Render Graph" tab's own content, since this
  tooling environment has no way to switch which ImGui dock tab is active
  remotely (the same limitation every prior phase's own completion report
  already recorded for interactive UI verification).
- `RenderGraphSnapshot.cpp`'s `ResourceUsageName()` (Phase 2's own explicit,
  accepted gap for volume-texture usages) was re-confirmed still degrading
  safely (an empty/wrong-but-bounds-checked name for a `VolumeTexture` usage,
  never a crash) — unaffected by anything this phase changed, and not revisited
  here, matching Phase 2's own explicit scope limit.

### 6. `AtmosphereLutRenderer::TransmittanceLutOutput()` (new public accessor)

`Texture2D* TransmittanceLutOutput() noexcept` — exposes the persistent
`Texture2D` behind `AddTransmittanceLutPass()`'s own output (returns `nullptr`
if that pass has never run yet this session), so the validation tool can read
its real pixels without `AtmosphereLutRenderer` needing any dependency on the
validation tool itself.

## Deviations from the plan (and why)

1. **The debug-slice mirror only covers the GAME VIEW's own aerial-perspective
   volume** (`"AtmosphereAerialPerspectiveVolume_GameView"`), not a second one
   for Scene View. The strategy document's own literal example name,
   `"AtmosphereAerialPerspectiveVolumeDebugSlice"` (singular, no `_GameView`/
   `_SceneView` suffix), and Step 3.4's own smoke-test wording ("the new
   debug-slice texture", singular) both read as a single mirror being the
   intended scope — kept exactly that literal name rather than inventing a
   `_GameView` suffix that would contradict the document's own worked example.
2. **The "last validation result" state is owned by `ImGuiEditorLayer`, not
   folded into `EditorContext`.** `BuildAtmospherePanel()` stays a stateless
   free function (per its own header's existing convention); its one new piece
   of genuinely cross-frame state is threaded in by reference from
   `ImGuiEditorLayer`'s own member, the same "small stateful class owns it,
   panel itself stays a free function" split already used for
   `m_blurValidation`'s `OutputTexture()`/etc.
3. **`RayIntersectsSphereNearestForValidation()` is a local, duplicated copy**
   of the GLSL ground-hit check, not a call into `AtmosphereMath.h` — per that
   file's own explicit doc-comment scope (ground occlusion is a LUT-shape-
   specific concern, not the general-purpose oracle's job), adding it there
   would have been a scope violation, not a cleanup.
4. **`Renderer::CaptureImagePixels()` was reused as-is (no changes needed)** —
   the "Revision Notes" at this phase's own strategy document's top predicted
   this ("this phase's own readback approach... is unaffected by the binding-
   type/depth-sampling corrections made to Phases 3/7"), confirmed true: the
   existing `bytesPerPixel=4`/`ResourceState`-based API already covered this
   exact use case with zero modification.
5. No other deviations — the tolerance value/reasoning, the debug-slice
   texture's HDR format choice, the "no `RenderGraphDebugTextureRegistry`
   retrofit" rule, and the "no new `GpuTimingSlot`" rule were all followed
   exactly as the strategy document specified.

## What was explicitly NOT done (per Step 4)

- No new general per-render-graph-pass GPU timing infrastructure — confirmed,
  out of scope, and now documented as a permanent, tracked `TODO.md` item.
- No automated Tier-2 GoogleTest requiring a live `VkDevice` for the parity
  check — the manual Editor tool is the correct, sufficient rigor level here,
  mirroring the GPU Skinning campaign's own explicit precedent.
- No scene-serialization work, no general lighting system, no volumetric
  clouds — all left as `TODO.md` entries for a future campaign, not started.

## Build/test/run verification actually performed

- `cmake --build build --target gte_core` (incremental, first pass) — compiled
  cleanly.
- `cmake --build build --target GreatTamanaEngineTests` (incremental, first
  pass) — compiled/linked cleanly;
  `GreatTamanaEngineTests.exe --gtest_filter=AtmosphereMathTest.*:AtmosphereParametersTest.*`
  — 29/29 passed (4 newly added).
- `cmake --build build --target GreatTamanaEngine` (incremental, first pass) —
  compiled, the new `AtmosphereAerialPerspectiveVolumeDebugSlice.comp` shader
  compiled via `glslc` with zero errors, linked cleanly.
- Ran the engine (`run_app_background`, Editor build, Vulkan validation layers
  enabled by default) and confirmed via the embedded HTTP server:
  `GET /list_textures` shows every atmosphere texture (including the new
  debug-slice texture) registered with `frames_since_update: 0`;
  `GET /get_texture` for `AtmosphereTransmittanceLut`/`AtmosphereMultiScatteringLut`/
  `AtmosphereSkyViewLut_GameView`/`AtmosphereSkyViewLut_SceneView`/the new
  debug-slice texture all return real, visually plausible PNGs (unchanged sky
  gradients for the pre-existing LUTs, a new blue-toned/black-bottomed
  gradient for the debug slice); `GET /get_game_view`/`GET /get_swapchain` both
  show the expected composited sky, and the "Atmosphere" dock tab is present.
  Stopped (`stop_app_background`).
- **Per this phase's own — and ONLY this phase's own — workflow rule:** a
  FULL, CLEAN build (`cmake --build build --target gte_core --clean-first`,
  which cleaned and rebuilt all 410 previously-built files including every
  third-party dependency) succeeded with zero errors; a full, clean-following
  build of `GreatTamanaEngineTests` (136 objects) succeeded; a full, clean-
  following build of `GreatTamanaEngine` (23 shader-compile + link steps)
  succeeded. `ctest -C Debug --output-on-failure` from the build directory:
  **1205/1205 tests passed** (1 pre-existing machine-gated smoke test,
  `PmxLoaderRealModelSmokeTest.LoadsAnMmdModelIfPresentOnThisMachine`,
  skipped — same as every prior campaign's own full-suite runs), zero
  regressions. Re-ran the manual runtime smoke pass (launch, `/list_textures`,
  `/get_game_view`, stop) once more against this final clean build to confirm
  it behaves identically — it does.

## Open questions / notes for the campaign as a whole

- The release-build/both-panels-hidden direct-to-swapchain gap (flagged by
  Phases 7/8) remains open — recorded as a permanent `TODO.md` item rather than
  attempted here, since closing it is a real, separate `AddPresentPass()`
  change with its own design questions, not a natural fit for this
  validation/docs/build-verification phase.
- Per-render-graph-pass GPU timing (the pre-existing, campaign-external gap)
  also remains open — recorded in `TODO.md`, explicitly not this phase's job.
- The campaign is now considered COMPLETE — see
  `ATMOSPHERE_CAMPAIGN_COMPLETION_REPORT.md` for the full nine-phase tie-together.
