# PHASE5 — Aerial Perspective LUT Numeric Inspection Tool

Parent: `PHASE0_MASTER_STRATEGY.md`. Depends on Phase 1 (the tunables must
exist so the tool's own diagnostic output can meaningfully reference/be
re-run after changing them) and benefits from Phase 3 already having
shipped (so there's an actually-interesting before/after to report), but has
no hard code dependency on Phase 3/4's own files — this phase only reads
back the volume's live GPU contents, it does not touch the generation/
composite shaders or the HTTP preview path at all.

## Step 1 — The Goal (Where are we going?)

Give the Editor (and, through it, an AI agent driving the Editor/engine) a
**numeric, non-visual, objective** way to confirm the Aerial Perspective
volume's real contents changed meaningfully after Phase 3's rebalancing —
without needing to eyeball a screenshot. Mirrors the exact, already-proven
`ValidateAtmosphereTransmittanceLut`/`AtmosphereTransmittanceLutValidation.h/.cpp`
pattern (`src/Editor/`, Editor-only, `Renderer::CaptureImagePixels()`-based
CPU readback, one button + a printed diagnostic string in the "Atmosphere"
panel) — but this tool is NOT a GPU-vs-CPU-oracle PARITY check like that one
(the aerial volume's own ray-march has no independent CPU oracle in this
campaign's scope, and Locked Design Decision 5 forbids touching
`AtmosphereMath.h`); it is a **plain descriptive-statistics readback**: what
are the real min/max/mean transmittance and in-scattering values this volume
currently holds, and does that add up to "big enough to plausibly be
visible"?

## Step 2 — The Situation (Where are we now?)

- The proven pattern to mirror:
  `src/Editor/AtmosphereTransmittanceLutValidation.h/.cpp` (a `Renderer&`
  + `AtmosphereLutRenderer&` + params in, a result struct + diagnostic
  string out, called from a button in `AtmospherePanel.cpp`).
- There is no existing "read back an entire live VolumeTexture to the CPU"
  primitive in this engine — `Renderer::CaptureImagePixels()` is 2D-only.
  Rather than adding a brand-new 3D-image-readback Vulkan primitive (a
  bigger, riskier addition), REUSE the ALREADY-EXISTING per-slice debug
  mirror pass this engine already ships:
  `AtmosphereLutRenderer::AddAerialPerspectiveVolumeDebugSlicePass()` (Phase
  9 of the ORIGINAL `atmosphere-scattering-1` campaign) copies exactly ONE Z
  slice of a named aerial volume into a real, registered 2D `RenderTexture`
  (`"AtmosphereAerialPerspectiveVolumeDebugSlice"`, `rgba16f`), which
  `Renderer::CaptureImagePixels()` can already read back to the CPU (it's
  how `GET /get_texture` already serves that exact texture name today).
  This phase's own inspection tool reads the WHOLE volume by looping over
  all `volumeDepth` (32) slices — but NOT by calling
  `AddAerialPerspectiveVolumeDebugSlicePass()` itself in a loop (that method
  only actually dispatches work when invoked through a real, currently-
  executing `RenderGraph` pass — see Step 3.3's own "Which approach, and
  why" for the verified reasoning). Instead, this phase adds ONE small new
  ad-hoc-dispatch method reusing that method's already-built pipeline/
  descriptor-set-layout (mirroring `VolumeTexturePreviewRenderer`'s own
  already-shipped "immediate, non-per-frame dispatch" pattern — see Step
  3.3) — a small, well-precedented amount of genuinely new plumbing, not
  "zero", but no new Vulkan PRIMITIVE (pipeline/descriptor layout/shader) is
  needed, only a new orchestration method around what already exists.
- `VolumeTexture::Depth()` (already exists, used elsewhere e.g. in
  `AddAerialPerspectiveVolumeDebugSlicePass()` itself) gives the real slice
  count to loop over, rather than hardcoding `32`.

## Step 3 — The Plan (Detailed Steps)

### 3.1 — Pure, Tier-1-testable aggregation helper (write this FIRST)

New file `src/Editor/AtmosphereAerialPerspectiveLutInspection.h` (+ `.cpp`),
`GTE_ENABLE_EDITOR`-gated (mirrors `AtmosphereTransmittanceLutValidation.h`'s
own gating exactly). Start with a small, PURE, no-GPU-dependency function
(this is what gets a real Tier-1 test):

```cpp
// Pure aggregation over one already-captured rgba16f slice's raw pixel
// data. IMPORTANT, VERIFIED-AGAINST-REAL-CODE CORRECTION vs. an earlier
// draft of this phase: Renderer::CaptureImagePixels() for a
// VK_FORMAT_R16G16B16A16_SFLOAT source does NOT hand back 4 full 32-bit
// floats per texel - it hands back the GPU's own raw bytes, which for this
// format are 4 IEEE-754 HALF floats (2 bytes/channel, 8 bytes/texel total),
// exactly the same raw layout src/Encoding/HdrColorVisualization.cpp's own
// ConvertHdrRgba16fToRgba8() already decodes via its local HalfToFloat()
// helper (see that file - confirmed by direct inspection). Treating the
// captured buffer as `const float*` directly (an earlier draft's mistake)
// would silently misinterpret every texel's bytes (reading 2 unrelated
// half-float channels as one bogus 32-bit float, quietly producing garbage
// statistics with no crash to flag it - the single most dangerous kind of
// bug for a tool whose entire job is "produce a trustworthy number").
//
// FIX: expose HdrColorVisualization.cpp's existing, already-correct
// half->float conversion as a new PUBLIC function - move it out of that
// file's anonymous namespace into `Encoding::DecodeHalfFloat(std::uint16_t)
// noexcept`, declared in `src/Encoding/HdrColorVisualization.h` - and have
// `ConvertHdrRgba16fToRgba8()` call the newly-public function (a pure
// visibility change; its own behavior/tests must stay unchanged). This
// phase's own new file then `#include`s that header and calls
// `Encoding::DecodeHalfFloat()` to decode each of a texel's 4 half-float
// channels before ever computing a min/max/mean - never a second,
// independently-maintained copy of the IEEE-754 half->float bit-twiddling
// logic (this codebase's own "one implementation, no duplication"
// discipline - see AGENTS.md).
//
// `rawRgba16fBytes` is exactly `texelCount * 8` bytes, tightly packed,
// row-major, R-G-B-A channel order (matches
// Renderer::CapturedRawPixels::pixels for this format exactly - the SAME
// raw layout HdrColorVisualization.cpp's own doc comment already documents
// for VK_FORMAT_R16G16B16A16_SFLOAT). Never touches a VkDevice - a real
// Tier-1-testable function (the half->float decode itself is pure integer/
// float bit manipulation, no GPU dependency), mirroring this codebase's own
// "pure math helper first, GPU wrapper second" convention (e.g.
// VolumeTexturePreviewMath.h/.cpp's own split).
struct AerialPerspectiveSliceStats {
    float minTransmittance = 1.0f;
    float maxTransmittance = 0.0f;
    double sumTransmittance = 0.0;

    float minInScatteringMagnitude = 0.0f; // length(rgb)
    float maxInScatteringMagnitude = 0.0f;
    double sumInScatteringMagnitude = 0.0;

    std::size_t texelCount = 0;
};

AerialPerspectiveSliceStats AccumulateAerialPerspectiveSliceStats(
    const std::uint8_t* rawRgba16fBytes, std::size_t texelCount, AerialPerspectiveSliceStats accumulateInto = {});

// Combines per-slice stats (accumulated across every one of the volume's
// depth slices) into one final, whole-volume summary result - see
// AtmosphereAerialPerspectiveLutInspectionResult below. A pure function of
// its own input struct's accumulated sums/counts - no floating-point
// re-derivation from raw pixels needed here.
struct AtmosphereAerialPerspectiveLutInspectionResult; // fwd-declared here, defined below (3.2).
AtmosphereAerialPerspectiveLutInspectionResult FinalizeAerialPerspectiveLutInspection(
    const AerialPerspectiveSliceStats& totalStats, int width, int height, int depth);
```

Write `tests/Editor/AtmosphereAerialPerspectiveLutInspectionTests.cpp` (new,
added inside `tests/CMakeLists.txt`'s existing `if(GTE_ENABLE_EDITOR)` block -
see the same block `Editor/SceneGridMathTests.cpp`/`Editor/SelectionTests.cpp`
already live in) covering: an all-`(0,0,0,1)` volume (transmittance 1.0
everywhere, zero in-scattering — the "far/nothing to see" degenerate case,
expressed in the test as real encoded half-float byte patterns via
`Encoding::DecodeHalfFloat()`'s own inverse - or a small test-local
float-to-half encoder, whichever is the smaller/clearer diff once actually
writing the test), an all-`(1,1,1,
0.5)` volume, and a mixed-value case with a hand-computed expected min/max/
mean — mirrors `VolumeTexturePreviewMathTests.cpp`'s own "several small,
hand-computed cases" style exactly.

### 3.2 — The result struct + diagnostic string

```cpp
struct AtmosphereAerialPerspectiveLutInspectionResult {
    bool succeeded = false;
    std::string failureReason;

    int width = 0;
    int height = 0;
    int depth = 0;
    std::size_t texelCount = 0;

    float minTransmittance = 1.0f;
    float maxTransmittance = 1.0f;
    float meanTransmittance = 1.0f;

    float minInScatteringMagnitude = 0.0f;
    float maxInScatteringMagnitude = 0.0f;
    float meanInScatteringMagnitude = 0.0f;

    // A documented, tunable HEURISTIC (NOT a physical-correctness check
    // like AtmosphereTransmittanceLutValidationResult's own epsilon
    // comparison) - true when the farthest-slice numbers look "big enough
    // to plausibly be visible" once composited into an 8-bit sceneColor.
    // Threshold chosen loosely: an 8-bit channel's own smallest visible
    // step is 1/255 ~= 0.0039 - this heuristic requires at least 5x that
    // much combined haze/in-scattering "budget" before calling it
    // plausible, to allow comfortable margin over pure quantization noise.
    bool likelyVisibleAtDefaultExposure = false;
};

std::string ToDiagnosticString(const AtmosphereAerialPerspectiveLutInspectionResult& result);
```

### 3.3 — The GPU-touching orchestration function

```cpp
// Loops over every Z slice of the named aerial-perspective volume, using a
// NEW, ad-hoc immediate-dispatch method added to AtmosphereLutRenderer in
// this same step (3.3a below) - NEVER a throwaway RenderGraph build +
// RenderGraph::Execute() call (see "Which approach, and why" below for the
// verified reasoning this doc's own earlier draft left unresolved),
// accumulating AerialPerspectiveSliceStats across all of them, then
// finalizes and returns one whole-volume result. Tier-2 (GPU-touching, no
// automated test) - mirrors ValidateAtmosphereTransmittanceLut()'s own
// exact shape/caveats (graceful failure via `succeeded=false` if the named
// volume has never been generated this session, never a crash/assert).
//
// NOTE: this function issues ONE extra ad-hoc compute dispatch + one CPU
// readback PER SLICE (32 round-trips at today's fixed volume depth) -
// deliberately acceptable ONLY because this is a rare, human/LLM-triggered
// Editor button click, never a per-frame cost, mirroring
// VolumeTexturePreviewRenderer::RenderPreview()'s own identical
// "at most once per request" cost-tier reasoning.
AtmosphereAerialPerspectiveLutInspectionResult InspectAerialPerspectiveVolume(
    Renderer& renderer, AtmosphereLutRenderer& atmosphereLutRenderer, const char* aerialPerspectiveVolumeName);
```

#### Which approach, and why (this resolves the ambiguity an earlier draft of this phase document left open)

Two candidate ways to actually trigger the per-slice debug-copy compute
shader on demand (outside the engine's normal per-frame render loop) were
considered — real function signatures cited below, read directly from this
repository's own current source:

1. **Build a small, throwaway `rg::RenderGraph`, add
   `AddAerialPerspectiveVolumeDebugSlicePass()` to it, and call
   `RenderGraph::Execute()`.** **REJECTED — not a safe/definitely-feasible
   path.** `RenderGraph::Execute()`'s own header comment
   (`src/Renderer/RenderGraph/RenderGraph.h`) states plainly: *"RenderGraph::Execute()
   is called exactly TWICE per frame, once per regime... never once, never
   more than twice"* (`SynchronousImmediateReadback` for Game/Scene View,
   `PipelinedDeferredReadback` for Present) — and the FIRST of those two
   calls each frame is the ONE call that triggers
   `RenderGraphResourcePool::BeginFrame()`. A THIRD, ad-hoc `Execute()` call
   made from an Editor button's click handler (which runs from
   `ImGuiEditorLayer::BuildUI()`, i.e. BEFORE either of this frame's two
   real `Execute()` calls have even happened yet) would violate that
   documented invariant and risk corrupting `RenderGraphResourcePool`'s
   per-frame pooled-resource bookkeeping for every OTHER resource in the
   same frame — a real, engine-wide correctness risk, not a narrow one.
   Even setting that aside, `AddAerialPerspectiveVolumeDebugSlicePass()`'s
   own `execute` lambda calls `renderer.Dispatch(...)`
   (`AtmosphereLutRenderer.cpp`), and `Renderer::Dispatch()`'s own doc
   comment (`Renderer.h`) says: *"Calling this OUTSIDE of a
   BeginGraphPassRecording()/EndGraphPassRecording() bracket has nothing
   sensible to do at all: asserts in debug builds, and is a safe no-op in
   release"* — so simply looping `AddAerialPerspectiveVolumeDebugSlicePass()`
   without ANY real `RenderGraph::Execute()` around it (the literal reading
   of this doc's own earlier draft) would silently do NOTHING in a release
   build and assert in a debug one. This option is a dead end either way.
2. **Ad-hoc immediate dispatch, mirroring
   `VolumeTexturePreviewRenderer::RenderPreview()`'s already-proven,
   already-shipped pattern (`src/Renderer/VolumeTexturePreviewRenderer.cpp`).**
   **CHOSEN.** That method already solves the EXACT same problem this tool
   has — "a fresh, extra, on-demand GPU dispatch, outside any per-frame
   RenderGraph pass, triggered by a rare, human/LLM-facing request" — by
   owning its own `ComputePipeline`/`ComputeDescriptorSet` (built via
   `Renderer::CreateComputePipeline()`/`Renderer::AllocateComputeDescriptorSet()`)
   and, inside a single `renderer.ImmediateSubmit([&](VkCommandBuffer cmd)
   { ... })` callback (`void ImmediateSubmit(const
   std::function<void(VkCommandBuffer)>& recordFn) const;` — `Renderer.h`),
   manually emitting `rg::EmitImageBarrier()` transitions, calling
   `vkCmdBindPipeline()`/`vkCmdBindDescriptorSets()`/`vkCmdPushConstants()`/
   `vkCmdDispatch()` directly (never `renderer.Dispatch()` — see point 1),
   then restoring every touched image's barrier state, before finally
   calling `renderer.CaptureImagePixels()` on the result. This is
   DEFINITELY feasible — it is exactly what already ships and works today
   for the generic volume-texture HTTP preview — and needs zero change to
   `RenderGraph`/`RenderGraphResourcePool`'s own per-frame contract.

**Concretely — add ONE new method to `AtmosphereLutRenderer`**, alongside
`AddAerialPerspectiveVolumeDebugSlicePass()` (reusing that method's own
ALREADY-EXISTING lazy-init helpers, `EnsureAerialPerspectiveVolumeDebugSliceInitialized()`/
`EnsureAerialPerspectiveVolumeDebugSliceViewInitialized()` — both currently
`private`, which is fine since this new method lives on the same class, so
no access-level change is needed):

```cpp
// 3.3a - the ad-hoc-dispatch counterpart of
// AddAerialPerspectiveVolumeDebugSlicePass() above, for exactly the "copy
// this one slice and read it back RIGHT NOW, outside any RenderGraph pass"
// case this phase's inspection tool needs. Reuses the SAME lazily-
// initialized pipeline/descriptor-set-layout/per-name output RenderTexture
// EnsureAerialPerspectiveVolumeDebugSliceInitialized()/
// EnsureAerialPerspectiveVolumeDebugSliceViewInitialized() already build for
// AddAerialPerspectiveVolumeDebugSlicePass() - only the RECORDING differs
// (a renderer.ImmediateSubmit() callback issuing raw vkCmd* calls directly,
// mirroring VolumeTexturePreviewRenderer::RenderPreview() exactly - never
// renderer.Dispatch()/builder.AddComputePass()).
//
// `outputTextureName` MUST be a DIFFERENT literal than
// "AtmosphereAerialPerspectiveVolumeDebugSlice" - e.g.
// "AtmosphereAerialPerspectiveVolumeInspectionSlice" - so this tool's own
// 32-slice sweep never clobbers the separately-live, per-frame,
// Editor-slider-driven, GET /get_texture-capturable debug-slice texture a
// user/AI agent may currently be relying on (Master Strategy rule 5's
// "never regress the existing... generic volume-preview interpretation"
// spirit, applied here one level deeper: never regress the existing named
// debug-slice texture either).
//
// Returns the RAW captured bytes (`Renderer::CapturedRawPixels::pixels`) -
// exactly `width*height*8` bytes, tightly packed row-major, 4 IEEE-754 HALF
// floats (2 bytes/channel) per texel, matching `VK_FORMAT_R16G16B16A16_SFLOAT`'s
// own real memory layout (see this file's own 3.1 correction above - this is
// NOT 4 full 32-bit floats/texel; the caller, `InspectAerialPerspectiveVolume()`
// below, passes these bytes straight into `AccumulateAerialPerspectiveSliceStats()`,
// which itself decodes each half-float channel via `Encoding::DecodeHalfFloat()`),
// or an empty CapturedRawPixels if
// `aerialPerspectiveVolumeName` has never been generated this session
// (mirrors AddAerialPerspectiveVolumeDebugSlicePass()'s own graceful-
// failure contract - a programmer/caller error, not a crash).
Renderer::CapturedRawPixels CaptureAerialPerspectiveVolumeSliceImmediate(Renderer& renderer,
    const char* aerialPerspectiveVolumeName, std::uint32_t sliceIndex, const char* outputTextureName);
```

Implementation of `CaptureAerialPerspectiveVolumeSliceImmediate()`: look up
`m_aerialPerspectiveVolumeViewStates[aerialPerspectiveVolumeName]` for the
source volume's `View()`/`Sampler()`/`Depth()` (the SAME lookup
`AddAerialPerspectiveVolumeDebugSlicePass()` itself already performs — fail
gracefully if not found, exactly like that method does); call
`EnsureAerialPerspectiveVolumeDebugSliceInitialized(renderer)` then
`EnsureAerialPerspectiveVolumeDebugSliceViewInitialized(renderer,
outputTextureName, volumeWidth, volumeHeight)` to get/create this tool's own
dedicated output `RenderTexture`; inside one `renderer.ImmediateSubmit(...)`
callback: barrier the source volume from
`rg::RequiredStateFor(rg::ResourceAccess::ShaderRead, false)` (the state
`AddAerialPerspectiveCompositePass()`'s own `ReadVolumeTexture(...,
ShaderRead)` call already leaves it in by the end of this session's most
recently completed real frame — mirrors `ValidateAtmosphereTransmittanceLut()`'s
own identical "assume the state the last real, already-GPU-completed frame
left it in" reasoning, `AtmosphereTransmittanceLutValidation.cpp`) to
`SHADER_READ_ONLY_OPTIMAL`, barrier the output texture to `GENERAL`, rewrite
the descriptor set (`ComputeDescriptorSet::Rewrite()`, exactly as
`AddAerialPerspectiveVolumeDebugSlicePass()` itself does), push the SAME
`{sliceIndex, sliceCount}` push-constant pair that method already defines
(`AerialPerspectiveVolumeDebugSlicePushConstants`, `AtmosphereLutRenderer.cpp`),
`vkCmdDispatch()` directly, then barrier the source volume back to its
original state (never leave it in a state the render graph doesn't expect
the NEXT real frame to find it in — mirrors
`VolumeTexturePreviewRenderer::RenderPreview()`'s own identical "restore the
volume image to its real previous state" comment); finally call
`renderer.CaptureImagePixels(outputImage, VK_IMAGE_ASPECT_COLOR_BIT,
VK_FORMAT_R16G16B16A16_SFLOAT, extent, outputPreviousState, /*bytesPerPixel=*/8)`
on the output texture and return the raw captured bytes (still half-float
encoded - do NOT decode to float here; decoding happens exactly once, inside
`AccumulateAerialPerspectiveSliceStats()`, per 3.1's own correction).

`InspectAerialPerspectiveVolume()` itself then becomes a simple loop: for
`sliceIndex` in `0 .. sourceVolumeDepth-1`, call
`CaptureAerialPerspectiveVolumeSliceImmediate()`, feed the returned raw
BYTES (`CapturedRawPixels::pixels.data()`, NOT reinterpreted as `float*`)
into `AccumulateAerialPerspectiveSliceStats()`, and after the loop
call `FinalizeAerialPerspectiveLutInspection()` once.

### 3.4 — `AtmospherePanel.cpp` wiring

New button + result member, mirroring the existing "Validate Transmittance
LUT" button exactly:

```cpp
ImGui::Separator();
if (ImGui::Button("Inspect Aerial Perspective LUT")) {
    lastAerialInspectionResult = InspectAerialPerspectiveVolume(
        renderer, atmosphereLutRenderer, "AtmosphereAerialPerspectiveVolume_GameView");
}
if (lastAerialInspectionResult.has_value()) {
    const auto& r = *lastAerialInspectionResult;
    const bool ok = r.succeeded;
    ImGui::TextColored(ok ? (r.likelyVisibleAtDefaultExposure ? ImVec4(0.3f,1.0f,0.3f,1.0f) : ImVec4(1.0f,0.8f,0.3f,1.0f)) : ImVec4(1.0f,0.4f,0.3f,1.0f),
        "%s", !ok ? "ERROR" : (r.likelyVisibleAtDefaultExposure ? "LIKELY VISIBLE" : "LIKELY TOO FAINT"));
    ImGui::TextWrapped("%s", ToDiagnosticString(r).c_str());
}
```
`BuildAtmospherePanel()`'s own parameter list gains a new
`std::optional<AtmosphereAerialPerspectiveLutInspectionResult>&
lastAerialInspectionResult` out-parameter, mirroring
`lastValidationResult`'s own existing shape exactly — update
`AtmospherePanel.h`'s declaration, and, in `ImGuiEditorLayer.cpp`: add a new
member `std::optional<AtmosphereAerialPerspectiveLutInspectionResult>
m_lastAerialPerspectiveLutInspection;` right alongside the existing
`std::optional<AtmosphereTransmittanceLutValidationResult>
m_lastAtmosphereTransmittanceLutValidation;` member (confirmed by direct
inspection to be a private member of `ImGuiEditorLayer`), and pass it as the
new trailing argument at the existing `BuildAtmospherePanel(m_ctx,
atmosphereSettings, renderer, atmosphereLutRenderer,
m_lastAtmosphereTransmittanceLutValidation);` call site (confirmed inside
`BuildUI()`, immediately after the `m_renderGraphPanel.Build(...)` call) —
append the new argument after `m_lastAtmosphereTransmittanceLutValidation`.

### 3.5 — `CMakeLists.txt`/`tests/CMakeLists.txt`

Register the two new `.cpp` files exactly like every other new-file phase in
this repository's own history does — CONFIRMED exact locations (both files
already gate `AtmosphereTransmittanceLutValidation.h/.cpp`/
`Editor/SceneGridMathTests.cpp` the identical way, so there is no ambiguity
left to resolve at implementation time):
- `CMakeLists.txt`: add `src/Editor/AtmosphereAerialPerspectiveLutInspection.h`
  and `.cpp` to the SAME `target_sources(gte_core PRIVATE ...)` list, inside
  the SAME `if(GTE_ENABLE_EDITOR)` block, that already lists
  `src/Editor/AtmosphereTransmittanceLutValidation.h`/`.cpp` (both
  `src/Editor/` and `src/Renderer/Atmosphere/` compile into the same
  `gte_core` target — there is no cross-target ambiguity to worry about
  here, unlike this document's own earlier, more hedged wording implied).
- `tests/CMakeLists.txt`: add `Editor/AtmosphereAerialPerspectiveLutInspectionTests.cpp`
  to the SAME `if(GTE_ENABLE_EDITOR)` ... `list(APPEND GTE_TEST_SOURCES ...)`
  block that already lists `Editor/EditorCameraTests.cpp`/
  `Editor/SceneGridMathTests.cpp`/`Editor/SelectionTests.cpp`.

## Verification

- Fast, targeted compile: build `gte_core` (CONFIRMED — both
  `src/Editor/AtmosphereAerialPerspectiveLutInspection.cpp` and
  `src/Renderer/Atmosphere/AtmosphereLutRenderer.cpp` compile into the same
  `gte_core` target, so there is no cross-target question to resolve here)
  plus `GreatTamanaEngineTests` (for the new Tier-1 test file).
- Run the NEW test file specifically (`ctest -R
  AtmosphereAerialPerspectiveLutInspection` or equivalent filter) — do not
  yet run the full suite (Master Strategy rule 1).
- `run_app_background` the Editor build, click "Inspect Aerial Perspective
  LUT" in the "Atmosphere" panel (or drive it via whatever the Editor's own
  automation surface allows), confirm it prints a real, non-crashing,
  plausible-looking result — ideally captured BEFORE Phase 3's rebalancing
  (if Phase 3 hasn't landed yet when this phase runs) and again AFTER, to
  produce a genuine, objective before/after number pair worth quoting in
  Phase 6's own completion report. `stop_app_background` when done.
- Write `PHASE5_COMPLETION_REPORT.md`, then commit.
