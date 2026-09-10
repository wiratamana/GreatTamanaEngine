# ATMOSPHERE_PHASE3_TRANSMITTANCE_LUT_v1.md

### Child document 3 of 9 — see `ATMOSPHERE_PHASE0_MASTER_STRATEGY_v1.md` for the full campaign map.
### Depends on: Phase 1 (`AtmosphereTypes.h`/`AtmosphereParameters.h`/`AtmosphereMath.h`, verified shared-`#include` support) and Phase 2 (not directly — this phase only needs 2D textures, but must not regress anything Phase 2 changed in `RenderGraphTypes.h`).
### Read `ATMOSPHERE_REFERENCE_NOTES.md` (written in Phase 1) for this LUT's exact resolution/formula before starting.

## Step 1: The Goal

Ship the FIRST real, permanent atmosphere compute pass: the **Transmittance
LUT** — a small 2D texture where texel `(u, v)` stores the RGB transmittance
(fraction of light NOT absorbed/scattered, per Rayleigh/Mie/ozone channel)
from a point at a given HEIGHT above the planet, looking in a given ZENITH
ANGLE, all the way out to the top of the atmosphere. Every later LUT in this
campaign (Multi-Scattering, Sky-View, the aerial-perspective volume) samples
this one — it is the foundation, and the one this campaign's CPU oracle
(`AtmosphereMath.h`, Phase 1) directly validates against (Phase 9).

## Step 2: The Situation

- `AtmosphereMath.h`'s `ComputeTransmittanceToTopOfAtmosphere()` (Phase 1)
  already implements this exact calculation on the CPU — this phase's GLSL
  shader must be a faithful, same-numerical-method transcription of it, not
  an independent reimplementation (see Phase 0's "permanent CPU oracle"
  rule).
- `ComputeBlurValidation`/`BoxBlur.comp` (see Phase 0's own citation) is the
  shape every compute-pass-with-a-texture-output in this campaign mirrors:
  lazily-initialized `ComputePipeline`+`ComputeDescriptorSet`+output texture,
  an `AddPass(RenderGraphBuilder&, Renderer&, ...) -> TextureHandle` method,
  called once per frame from wherever this campaign's atmosphere passes get
  wired into the real render-graph build sequence (Phase 7 is what actually
  wires the FULL chain into `Application.cpp`'s real per-frame sequence —
  this phase builds the class/shader and proves it standalone via a
  throwaway call site, exactly like Phase 2's own disposable validation, if
  Phase 7 has not landed yet at the time this phase is implemented).
- Unlike `BoxBlur.comp` (which reads an existing texture another pass wrote),
  the Transmittance LUT has **no texture input at all** — its only inputs are
  the `AtmosphereParametersGpu` constants (Phase 1), passed as a uniform
  buffer or push constant (a Transmittance LUT is tiny — e.g. 256×64 texels
  per Phase 1's reference notes — so it never needs to be recomputed more
  than once per session unless the atmosphere parameters themselves change;
  decide and document in this phase whether it recomputes every frame
  unconditionally, the simplest option and the one to default to, or only
  when parameters are dirty — the simplest option is explicitly preferred
  here per Step 4 below).
- `DescriptorSetLayoutBuilder`'s documented binding-order convention
  (read-only buffers, read-write buffers, read-only textures, storage images
  last — see `BoxBlur.comp`'s own header comment) applies here too: binding 0
  should be the `AtmosphereParametersGpu` uniform buffer, binding 1 the
  `image2D` output.

## Step 3: The Plan

- `src/Shaders/AtmosphereCommon.glsl` gains its first real content: the
  density-profile functions, `ComputeOpticalDepthToTopOfAtmosphere()`, and
  `ComputeTransmittanceToTopOfAtmosphere()` — GLSL transcriptions of Phase
  1's `AtmosphereMath.h` functions of the exact same names (keep the NAMES
  identical between the two languages wherever possible — this materially
  helps Phase 9's reviewer/validator reason about parity). Also add the
  Transmittance LUT's specific UV<->(height, zenith angle) parameterization
  functions here (`TransmittanceLutUvToHeightZenith()`/the inverse), exactly
  as pinned down in `ATMOSPHERE_REFERENCE_NOTES.md` — this parameterization
  (typically a `sqrt`/non-linear remap so more texels are spent near the
  horizon, where transmittance changes fastest) is the single easiest place
  to introduce a subtle bug that "looks plausible" but is numerically wrong;
  transcribe it exactly, do not approximate.
- `src/Shaders/AtmosphereTransmittanceLut.comp` — `#include
  "AtmosphereCommon.glsl"`; for each texel, decode its `(height, zenithAngle)`
  via the shared UV parameterization, then call the shared
  `ComputeTransmittanceToTopOfAtmosphere()` and `imageStore()` the RGB result
  (alpha = 1.0). `local_size_x`/`local_size_y` mirrors `BoxBlur.comp`'s own
  16×16 convention unless the LUT's exact resolution (per the reference
  notes) makes a different local size more natural — document the chosen
  size and its matching C++-side constant explicitly (same hand-maintained
  pairing discipline `BoxBlur.comp`'s own header comment already calls out).
- `src/Renderer/Atmosphere/AtmosphereLutRenderer.h/.cpp` — a new class,
  modeled on `ComputeBlurValidation`'s shape, owning: the Transmittance LUT's
  `ComputePipeline`/`ComputeDescriptorSetLayout`/`ComputeDescriptorSet`, a
  persistent output `RenderTexture` (or plain storage-capable `Texture2D` if
  a companion depth buffer is not needed — it is not, for a 2D LUT; prefer
  `Texture2D` with `allowStorageImageAccess=true` over `RenderTexture` here,
  since this is not a "camera view" target and doesn't need `RenderTexture`'s
  resize-with-panel behavior at all), and one method,
  `TextureHandle AddTransmittanceLutPass(RenderGraphBuilder&, Renderer&,
  const AtmosphereParametersGpu&)`. This class is the SINGLE home for every
  atmosphere LUT pass added in Phases 3-6 — do not create four independent,
  unrelated classes; grow this one class's public surface one method per
  phase, mirroring how `GpuSkinningRigCache` accumulated per-model
  responsibility over several phases in its own campaign.
- Register the output texture under the literal name
  `"AtmosphereTransmittanceLut"` via `CreateTexture()`/`ImportTexture()` (per
  Phase 0's cross-cutting rule) — this is what makes it immediately visible
  via `GET /get_texture?texture_name=AtmosphereTransmittanceLut` with zero
  extra plumbing.
- Add the new uniform buffer for `AtmosphereParametersGpu` via
  `Renderer::CreateBuffer()`(`BufferMemoryUsage::CpuToGpu`, matching how any
  other small, host-writable uniform buffer in this engine is already
  created) — written once (or once per change) from the CPU side, never
  per-texel.
- Disposable proof, mirroring Phase 2's own discipline: a temporary call site
  (clearly commented `// TEMPORARY - ATMOSPHERE_PHASE3 VALIDATION`) that adds
  this pass into one of the two real `RenderGraph::Execute()` regimes (the
  Offscreen regime is the natural, lower-risk choice — see
  `Application::Run()`), confirms it compiles/runs with validation layers
  clean, and captures it once via `gte_send_request` against
  `/get_texture?texture_name=AtmosphereTransmittanceLut&format=png` to
  visually sanity-check it (expect a smooth blue-to-orange-ish gradient
  banding by height/angle — compare the captured image by eye against any
  reference screenshot/description found in `ATMOSPHERE_REFERENCE_NOTES.md`).
  **Unlike Phase 2's throwaway probe, DO NOT delete this call site** — Phase
  7 will properly relocate/wire it into the real, permanent per-frame
  sequence; leave a clear `// TODO(ATMOSPHERE_PHASE7): relocate into the real
  atmosphere pass sequence` comment instead of deleting working code, since
  Phase 4/5/6 all build directly on this same pass existing and running every
  frame.
- A Tier-1 test is NOT applicable to the shader itself (Tier 2, no live
  `VkDevice` test infra exists — see `AGENTS.md`, "Testability & Regression
  Safety") — correctness proof for the actual `.comp` output is Phase 9's
  job (parity against the CPU oracle). This phase's own automated coverage
  is whatever Phase 1 already added for `AtmosphereMath.h` — no NEW Tier-1
  test is expected from this phase specifically, beyond re-running the
  existing suite to confirm no regression.

## Step 4: What We Will NOT Do

- No "only recompute when dirty" optimization yet — recompute the
  Transmittance LUT unconditionally, every frame, for now (it is tiny — a few
  thousand texels — and this keeps the first working version simple). A
  future optimization phase (not part of this campaign) could add a dirty
  flag keyed on `AtmosphereParametersGpu`/`DirectionalLight` changes if
  profiling ever shows this matters — do not build that speculatively here.
- No support for editing `AtmosphereParametersGpu` from the Editor in this
  phase — it is still `MakeDefaultEarthAtmosphereParameters()`'s fixed
  output; Phase 8 is what exposes any of this to the Editor.
- No GPU timing registration for this pass yet — Phase 9 wires up
  `GpuTimingService`/the "Render Graph" panel's per-pass timing for the whole
  chain at once, not pass-by-pass across Phases 3-6.

## Step 5: Their Role

- Get this ONE pass fully correct and visually sane (via the
  `/get_texture` capture) before Phase 4 builds on top of it — Multi-
  Scattering LUT correctness is much harder to eyeball directly, so a wrong
  Transmittance LUT propagating silently into it would be a much more
  confusing bug to track down two phases later.
- `AtmosphereLutRenderer` is the one class every remaining LUT phase (4, 5,
  6) extends — read this phase's own implementation of it before starting
  Phase 4, so the new method you add matches its established internal
  conventions (how it stores/reuses `ComputeDescriptorSet`s, how it names
  its output textures, how it threads `AtmosphereParametersGpu` through).
