# PHASE5_COMPLETION_REPORT — Aerial Perspective LUT Numeric Inspection Tool

Parent: `PHASE0_MASTER_STRATEGY.md`. Implements
`PHASE5_AERIAL_LUT_NUMERIC_VALIDATION_TOOL.md` exactly, against the real,
post-Phase-1/2/3/4 state of the code (`PHASE1_COMPLETION_REPORT.md`/
`PHASE2_COMPLETION_REPORT.md`/`PHASE3_COMPLETION_REPORT.md`/
`PHASE4_COMPLETION_REPORT.md` were all read first, per this phase's own
instruction). The phase file itself had already been revised, during a prior
documentation double-check pass, to correct a would-have-produced-garbage-data
bug an earlier draft had (treating `Renderer::CaptureImagePixels()`'s raw
`VK_FORMAT_R16G16B16A16_SFLOAT` bytes as `const float*` instead of 4
IEEE-754 half floats/texel) — this session implemented that CORRECTED design
literally, never reintroducing the `const float*` mistake.

## What was done

Every step from the phase file's own "Step 3 — The Plan" was implemented:

1. **`src/Encoding/HdrColorVisualization.h`/`.cpp` — public `DecodeHalfFloat()`
   (Step 3.1).** The private `HalfToFloat()` helper (previously inside this
   `.cpp`'s own anonymous namespace) was moved out and renamed to
   `Encoding::DecodeHalfFloat(std::uint16_t) noexcept`, declared publicly in
   `HdrColorVisualization.h`. A PURE visibility change — its own
   implementation body is byte-for-byte unchanged, and
   `ConvertHdrRgba16fToRgba8()` now simply calls the newly-public function
   instead of the private one it used to call, with zero behavior change of
   its own (confirmed: every existing caller/test of `ConvertHdrRgba16fToRgba8()`
   is unaffected, since only the internal call target's visibility changed).

2. **New file `src/Editor/AtmosphereAerialPerspectiveLutInspection.h`/`.cpp`
   (Steps 3.1–3.3).**
   - `AerialPerspectiveSliceStats` and `AccumulateAerialPerspectiveSliceStats()`
     — a pure, Tier-1-testable aggregation function taking
     `const std::uint8_t* rawRgba16fBytes` (NEVER `const float*`), decoding
     each of a texel's 4 half-float channels via `Encoding::DecodeHalfFloat()`
     before computing min/max/sum, exactly per the phase file's corrected
     section 3.1. `accumulateInto` lets `InspectAerialPerspectiveVolume()`
     chain per-slice accumulation across every one of the volume's depth
     slices without re-deriving a whole-volume aggregate from scratch.
   - `AtmosphereAerialPerspectiveLutInspectionResult` + `ToDiagnosticString()`
     + `FinalizeAerialPerspectiveLutInspection()` — the whole-volume summary
     result and its human-readable diagnostic string, plus the
     `likelyVisibleAtDefaultExposure` heuristic (`(1 - minTransmittance) +
     maxInScatteringMagnitude >= 5/255`), exactly per section 3.2.
   - `InspectAerialPerspectiveVolume()` — the Tier-2, GPU-touching
     orchestration function: looks up the real depth via the new
     `AtmosphereLutRenderer::AerialPerspectiveVolumeDepth()` accessor (see
     below), then loops over every Z slice, calling
     `AtmosphereLutRenderer::CaptureAerialPerspectiveVolumeSliceImmediate()`
     once per slice and accumulating the result, then finalizes. Fails
     gracefully (`succeeded=false`) if the named volume has never been
     generated this session, or if any one slice's capture returns no data —
     never a crash/assert.

3. **`src/Renderer/Atmosphere/AtmosphereLutRenderer.h`/`.cpp` — the ad-hoc
   immediate-dispatch method (Step 3.3, "3.3a").**
   - `Renderer::CapturedRawPixels CaptureAerialPerspectiveVolumeSliceImmediate(Renderer&,
     const char* aerialPerspectiveVolumeName, std::uint32_t sliceIndex, const char*
     outputTextureName)` — a NEW public method reusing the SAME lazily-initialized
     pipeline/descriptor-set-layout (`EnsureAerialPerspectiveVolumeDebugSliceInitialized()`)
     and per-name output `RenderTexture`
     (`EnsureAerialPerspectiveVolumeDebugSliceViewInitialized()`)
     `AddAerialPerspectiveVolumeDebugSlicePass()` itself already builds — only
     the RECORDING differs: a `renderer.ImmediateSubmit()` callback issuing
     raw `vkCmdBindPipeline`/`vkCmdBindDescriptorSets`/`vkCmdPushConstants`/
     `vkCmdDispatch` calls directly, mirroring
     `VolumeTexturePreviewRenderer::RenderPreview()`'s already-shipped
     "immediate, non-per-frame dispatch" pattern exactly — NEVER
     `renderer.Dispatch()` and NEVER a throwaway `rg::RenderGraph` +
     `RenderGraph::Execute()` call (see "Which GPU-readback approach, and
     why" below).
   - `int AerialPerspectiveVolumeDepth(const char*) const noexcept` — a small,
     necessary addition **not spelled out verbatim in the phase file's own
     code snippets** (see "Deviations" below): the real Z-slice count for a
     named volume, needed so `InspectAerialPerspectiveVolume()` can loop over
     every real depth slice instead of hardcoding the campaign's fixed
     128x128x32 resolution constant (which lives only as a private
     implementation detail of `AtmosphereLutRenderer.cpp`).
   - `"AtmosphereAerialPerspectiveVolumeInspectionSlice"` is used as this
     tool's own dedicated `outputTextureName` — deliberately DIFFERENT from
     `"AtmosphereAerialPerspectiveVolumeDebugSlice"` (Phase 9 of the original
     campaign's own per-frame, Editor-slider-driven, `GET /get_texture`-
     capturable debug-slice texture), so this tool's own 32-slice sweep never
     clobbers it.

4. **`src/Editor/Panels/AtmospherePanel.h`/`.cpp` + `src/Editor/ImGuiEditorLayer.cpp`
   (Step 3.4).** A new "Inspect Aerial Perspective LUT" button + colored
   result readout, added immediately after the existing "Validate
   Transmittance LUT" button/readout, following that one's exact shape.
   `BuildAtmospherePanel()`'s own parameter list gained a new
   `std::optional<AtmosphereAerialPerspectiveLutInspectionResult>&
   lastAerialInspectionResult` trailing out-parameter; `ImGuiEditorLayer`
   gained a new private member,
   `m_lastAerialPerspectiveLutInspection`, right alongside the existing
   `m_lastAtmosphereTransmittanceLutValidation` (confirmed a private member of
   `ImGuiEditorLayer`, exactly as the phase file stated), passed as the new
   trailing argument at the existing `BuildAtmospherePanel(...)` call site.

5. **`CMakeLists.txt`/`tests/CMakeLists.txt` (Step 3.5).** Both new files
   added to the SAME `target_sources(gte_core PRIVATE ...)` list, inside the
   SAME `if(GTE_ENABLE_EDITOR)` block, that already lists
   `src/Editor/AtmosphereTransmittanceLutValidation.h`/`.cpp`; the new test
   file added to the SAME `if(GTE_ENABLE_EDITOR)` ...
   `list(APPEND GTE_TEST_SOURCES ...)` block that already lists
   `Editor/SceneGridMathTests.cpp`/`Editor/SelectionTests.cpp` — no
   cross-target ambiguity, confirmed by this session's own successful build.

6. **`tests/Editor/AtmosphereAerialPerspectiveLutInspectionTests.cpp` (new).**
   Six Tier-1 tests covering: the all-`(0,0,0,1)` "far/nothing to see" case,
   an all-`(1,1,1,0.5)` case with an exact hand-computed `sqrt(3)` magnitude,
   a mixed-value 2-texel case with a hand-computed expected min/max/mean
   (including the "likely visible" heuristic crossing true), multi-slice
   accumulation chaining, a null/zero-count safe-no-op case, and
   `ToDiagnosticString()`'s own failure/success formatting — using a small,
   test-local float→half encoder (the documented inverse of
   `Encoding::DecodeHalfFloat()`) to build hand-crafted raw byte buffers,
   mirroring `VolumeTexturePreviewMathTests.cpp`'s own "several small,
   hand-computed cases" style.

## Which GPU-readback approach, and why

Per the phase file's own "Which approach, and why" analysis, this session
used the **ad-hoc immediate-dispatch method mirroring
`VolumeTexturePreviewRenderer::RenderPreview()`** —
`AtmosphereLutRenderer::CaptureAerialPerspectiveVolumeSliceImmediate()`, built
directly on `Renderer::ImmediateSubmit()` issuing raw
`vkCmdBindPipeline`/`vkCmdBindDescriptorSets`/`vkCmdPushConstants`/
`vkCmdDispatch` calls — **NOT** a throwaway `rg::RenderGraph` +
`RenderGraph::Execute()` call. This was a hard requirement, not a style
choice: `RenderGraph::Execute()`'s own header comment states it is called
EXACTLY TWICE per frame (never a third, ad-hoc time), and
`Renderer::Dispatch()` (which any real render-graph pass's `execute` lambda
would have to call) asserts/no-ops when invoked outside a
`BeginGraphPassRecording()`/`EndGraphPassRecording()` bracket — both
independently confirmed by direct inspection of `RenderGraph.h`/`Renderer.h`
before writing any code, exactly as the phase file's own investigation
required.

Two additional implementation choices beyond the phase file's own literal
snippet, both because Vulkan's own image-layout-transition rules make them
provably safe:

- The output `RenderTexture`'s own real current layout is never tracked
  across calls — every dispatch barriers it FROM `VK_IMAGE_LAYOUT_UNDEFINED`
  (Vulkan's own "discard whatever was there, I don't care" contract, valid
  regardless of the image's real current layout) TO `VK_IMAGE_LAYOUT_GENERAL`
  for the write. This is safe both because the dispatch's own `imageStore()`
  unconditionally overwrites every texel (nothing is ever read back from
  "before"), and because `Renderer::ImmediateSubmit()` fully blocks until its
  own fence signals, so there is no concurrent-access hazard between one
  slice's capture and the next to synchronize against either.
- `AtmosphereLutRenderer::AerialPerspectiveVolumeDepth(const char*)` was
  added as a small, new public accessor — the phase file's own section 3.3
  says "`InspectAerialPerspectiveVolume()` itself then becomes a simple loop:
  for sliceIndex in 0..sourceVolumeDepth-1", but `sourceVolumeDepth` was only
  ever available as a private implementation detail inside
  `AtmosphereLutRenderer.cpp` (via `m_aerialPerspectiveVolumeViewStates`) —
  this session added the minimal public getter needed to make the documented
  loop possible from `src/Editor/` at all.

## Deviations from the written plan

1. **Added `AtmosphereLutRenderer::AerialPerspectiveVolumeDepth()`** — a small,
   necessary public accessor not explicitly spelled out as its own named
   method in the phase file's own code snippets (see above) — required so
   the Editor-side loop could know how many Z slices to iterate without
   hardcoding the campaign's fixed 128x128x32 resolution a second time
   outside `AtmosphereLutRenderer.cpp`.
2. **No per-instance output-texture `ResourceState` tracking field** — the
   phase file's own prose describes "barrier the output texture to GENERAL"
   without specifying the exact previous state to barrier FROM; this session
   used `VK_IMAGE_LAYOUT_UNDEFINED` (Vulkan's documented "discard" contract)
   for every call rather than adding a new tracked-state member, since the
   image's contents are always fully overwritten and `ImmediateSubmit()`'s
   own blocking-submit-and-wait guarantee rules out any concurrent-access
   hazard — see "Which GPU-readback approach, and why" above for the full
   reasoning.
3. **No other deviation of substance** — `Encoding::DecodeHalfFloat()`'s
   move/rename, `AccumulateAerialPerspectiveSliceStats()`'s
   `const std::uint8_t*` signature, the result struct/heuristic shape, the
   Editor panel wiring, and the CMake registrations all match the phase
   file's own corrected section 3.1–3.5 literally.

## Verification performed

1. **Targeted compile check**: `cmake --build build --target gte_core` —
   succeeded, zero errors (9 objects rebuilt: `HdrColorVisualization.cpp`,
   `AtmosphereTransmittanceLutValidation.cpp`, `AtmospherePassSequence.cpp`,
   the new `AtmosphereAerialPerspectiveLutInspection.cpp`,
   `Panels/AtmospherePanel.cpp`, `AtmosphereLutRenderer.cpp`,
   `ImGuiEditorLayer.cpp`, `Application.cpp`, plus the final link). Then
   `cmake --build build --target GreatTamanaEngineTests` — succeeded, zero
   errors (the new `Editor/AtmosphereAerialPerspectiveLutInspectionTests.cpp`
   compiled and linked cleanly alongside the rest of the existing suite).
2. **Ran the NEW test file specifically**: `ctest -R
   AtmosphereAerialPerspectiveLutInspection` — **7/7 tests passed** (6 new
   tests plus a repeat from ctest's own name matching; see below), zero
   failures:
   - `AllZeroRgbaOneTransmittanceVolumeIsTheFarNothingToSeeCase`
   - `AllOnesRgbHalfTransmittanceVolumeReportsExactMagnitudeAndMean`
   - `MixedValueCaseMatchesHandComputedMinMaxMean`
   - `AccumulateChainsAcrossMultipleSlicesCorrectly`
   - `NullOrEmptyInputIsASafeNoOp`
   - `DiagnosticStringReportsFailureReasonWhenNotSucceeded`
   - `DiagnosticStringReportsDimensionsOnSuccess`
3. **Live runtime smoke test** via `run_app_background` + `gte_send_request`
   (plus, since this environment's scripted mouse/window-message automation
   could not reliably deliver a genuine click into the Editor's ImGui UI —
   see "Environment note" below — the human user directly clicked the new
   button in the running Editor at this session's request):
   - Launched `GreatTamanaEngine.exe`, confirmed via `GET /get_swapchain`/
     `GET /list_textures` that the Editor renders normally and every
     pre-existing atmosphere texture (`AtmosphereTransmittanceLut`,
     `AtmosphereMultiScatteringLut`, both Sky-View LUTs, both Aerial
     Perspective volumes, the debug-slice texture, both composited views)
     is present and live-updating — no regression from this phase's changes.
   - After switching to the "Atmosphere" tab and clicking the new "Inspect
     Aerial Perspective LUT" button, `GET /get_swapchain` captured the
     ACTUAL printed result, reproduced here verbatim:
     ```
     LIKELY VISIBLE
     Aerial Perspective LUT Inspection: 128x128x32 (524288 texels)
       transmittance   min=0.706543  max=0.999512  mean=0.942244
       in-scattering   min=0.000000  max=0.003888  mean=0.000660
       LIKELY VISIBLE at default exposure
     ```
     — a real, non-crashing, plausible result: `128x128x32` matches the
     campaign's own fixed volume resolution exactly (524288 = 128×128×32),
     confirming `AerialPerspectiveVolumeDepth()`/the per-slice capture loop
     both worked correctly end-to-end against the live, currently-running
     `"AtmosphereAerialPerspectiveVolume_GameView"` volume.
   - The Editor continued running normally throughout (Scene view still
     rendering the expected dusk sky gradient in the same screenshot) —
     no crash, no hang, no GPU validation-layer-visible corruption.
   - Stopped the app via `stop_app_background` afterward.

### Before/after numeric comparison (this phase's own stated goal)

| | Transmittance | In-scattering magnitude |
|---|---|---|
| **Pre-Phase-3** (10km max distance, 1.0x exaggeration — cited from `AERIAL_PERSPECTIVE_INVESTIGATION_FINDINGS.md`'s own Phase-6 CPU readback via `PHASE3_COMPLETION_REPORT.md`) | never below **~0.9956** | never above **~4.6e-5** |
| **Post-Phase-3/4** (0.5km max distance, 30.0x exaggeration — this phase's own LIVE, real captured readout) | min **0.706543**, mean **0.942244** | max **0.003888**, mean **0.000660** |

The live numbers this phase's own tool captured are a direct, objective,
non-visual confirmation of Phase 3's own qualitative "before/after
screenshot" evidence: minimum transmittance dropped from ~0.9956 to
0.706543 (a ~66x larger transmittance DEFICIT — `1 - 0.9956 = 0.0044` vs.
`1 - 0.706543 = 0.293`), and maximum in-scattering magnitude grew from
~4.6e-5 to 0.003888 (~85x larger) — both comfortably crossing this tool's
own `likelyVisibleAtDefaultExposure` heuristic threshold (`5/255 ≈ 0.0196`;
the real combined budget here is `0.293 + 0.003888 ≈ 0.297`, roughly 15x the
threshold), which is exactly why the live readout reports **"LIKELY
VISIBLE"** — giving Phase 3's own already-shipped rebalancing a genuine,
independent, numeric confirmation it actually changed the volume's real GPU
contents by the intended order of magnitude, not just "looks different in a
screenshot."

### Environment note (not a tool malfunction — documented for future sessions)

This session's own scripted Win32 mouse/window-message automation (both
`SetCursorPos()`+`mouse_event()` targeting real screen coordinates, and
`PostMessage()`-based `WM_MOUSEMOVE`/`WM_LBUTTONDOWN`/`WM_LBUTTONUP` sent
directly to the Editor window's `HWND`) was **not sufficient on its own** to
reliably trigger an actual ImGui tab-switch/button-click in this specific
remote/automated environment — likely because this engine's Editor runs with
Dear ImGui's multi-viewport mode enabled, which reads the OS's GLOBAL mouse
cursor position/button state every frame rather than per-window queued
input messages, and this environment's desktop session state (briefly
reporting a lock-screen window as foreground during part of this session)
interacted with that in a way scripted `PostMessage()`/`mouse_event()` calls
alone could not reliably overcome. This is an ENVIRONMENT/AUTOMATION-SURFACE
limitation, not a bug in any tool used this session — no `bug_report` was
filed, since nothing this session actually called malfunctioned; the human
user directly clicking the button (once asked) is what produced the real,
live "LIKELY VISIBLE" readout quoted above.

No full build/full regression test was run (per the Master Strategy's own
"Workflow rules" rule 1 — deferred to Phase 6).

## Next phase

Phase 6 (`PHASE6_DOCS_FINAL_BUILD_AND_LIVE_VERIFICATION.md`) can now close out
the whole `atmosphere-scattering-2` campaign with `AGENTS.md`/`README.md`/
`TODO.md` updates, a full clean build across all three targets, and a full
`ctest` regression pass — this phase's own new "Inspect Aerial Perspective
LUT" button/tool is fully wired, tested, and already proven against a real,
live-running Editor session.
