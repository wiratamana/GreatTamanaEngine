# CAMPAIGN COMPLETION REPORT — `frame-debugger-5`: Compute Shader Dispatch as a Tier-1 Citizen of the Frame Debugger

Branch: `feature/frame-debugger-impl`. Five phases, all **DONE**.

## 1. Goal recap

The Editor's "Frame Debugger" window (built by `frame-debugger-2`/`-3`, fixed for atmosphere-composite
awareness by `frame-debugger-4`) had a second, separately-confirmed bug: only 3 of this engine's 8+ real
compute-shader dispatches ever appeared in its event tree — one hardcoded `"AtmosphereAerialPerspectiveCompositePass"`
special case, and whatever pass names happened to appear in a caller-supplied GPU-Skinning name list. Every
Atmosphere Transmittance/Multi-Scattering/Sky-View LUT pass, the Aerial Perspective Volume pass, the Aerial
Perspective Volume Debug-Slice pass, and Compute Blur Validation's own pass were completely invisible in the
tree, regardless of whether they ran that frame. This campaign's goal: make **every real compute-shader
dispatch that ran this frame** a first-class, automatically-discovered, individually-inspectable tree leaf —
with its own real read/write facts and its own real, correct, distinct output-image preview — with **zero
further hand-written `FrameDebuggerData.cpp` code ever required** for a future compute pass to show up.

## 2. Phase-by-phase summary

| Phase | What landed |
|---|---|
| **PHASE1** — RenderGraph Compute-Dispatch Choke-Point Infrastructure | `RenderGraphBuilder::AddComputePass()` (previously a "purely cosmetic" no-op alias of `AddPass()`) now stamps a real `PassRecord::isComputePass` flag, surviving into `RenderGraphPassSnapshot::isComputePass` for both surviving AND culled passes. Two new parallel vectors, `readKinds`/`writeKinds` (`std::vector<rg::ResourceKind>`), let any consumer label a read/write row as Texture/Buffer/VolumeTexture without guessing. A companion bug fix, `ResourceUsageName()` rewritten from a buggy two-way `if` into an exhaustive three-way `switch`, fixed a real, independently-shipping bug where every `VolumeTexture`-kind read/write silently resolved to an empty name (affecting the already-shipped "Render Graph" panel too). Zero behavior change to rendering itself. 7 new tests, 166/166 RenderGraph-scoped tests passing. |
| **PHASE2** — Generic Compute-Dispatch Event-Tree Discovery | `FrameDebuggerData.cpp`'s `BuildRealFrameDebuggerSnapshot()` no longer discovers compute passes by hardcoded name — it walks every real, surviving (`isComputePass == true`, `isCulled == false`) pass in the current frame's `RenderGraphSnapshot` and builds one uniform `BuildComputeDispatchLeaf()` per pass, split into `"Compute Dispatches (Pre-GameView)"`/`"Compute Dispatches (Post-GameView)"` groups positioned by each pass's own real execution-order index relative to `"GameView"` (Locked Design Decision #8, added during this campaign's own v2 review — a single unconditional post-`"GameView"` group would have misrepresented passes that really ran BEFORE it). The old `gpuSkinningPassNamesThisFrame` name-list mechanism and the old hardcoded `"AtmosphereAerialPerspectiveCompositePass"` special case were both REMOVED entirely (a deliberate, user-approved breaking change to the previously-shipped tree shape). Every read/write row is now labeled by its own real `ResourceKind`. 12 tests in the snapshot-builder file (was 10), 69/69 FrameDebugger-scoped tests passing. |
| **PHASE3** — Generic Per-Pass Retained Preview Capture (2D textures) | Selecting a compute-dispatch leaf now shows THAT PASS'S OWN real output image, not the whole Game View. `FrameDebuggerHistory::CaptureFrame()` eagerly discovers every surviving compute pass's own FIRST `Texture`-kind write (`CollectComputePassTextureWrites()`, a new pure, Tier-1-tested function) and makes one more retained GPU-to-GPU copy per pass, inside the SAME single `ImmediateSubmit()` call the existing pre/post-composite copies already used — never assuming `ShaderRead` for the source's state, always reading its real tracked state from the registry. A pass with only a `Buffer`/`VolumeTexture` write correctly gets no entry (honest fallback to the whole-frame image). `ChooseFrameDebuggerPreviewSource()` widened with a 5th boolean; every pre-existing input combination re-asserted unchanged. Live-verified: 9 real compute-dispatch leaves found this session, 7 with real distinct 2D previews. 80/80 FrameDebugger-scoped tests passing. |
| **PHASE4** — Volume-Texture Ray-March Preview Reuse | The one remaining gap: a pass whose only visual write is a 3D volume texture (the Aerial Perspective froxel volume). Reuses the ALREADY-SHIPPED `VolumeTexturePreviewRenderer::RenderPreview()` (the exact same code `GET /get_texture` already uses for a volume) via a new `CollectComputePassVolumeTextureWrites()` sibling function, uploading the resulting ray-marched RGBA8 pixels into a fresh `RenderTexture`. The volume-preview interpretation-selection rule was extracted into a new shared function, `SelectVolumeTexturePreviewInterpretation()`, so `Application.cpp`'s own `GET /get_texture` handler and this new call site can never silently diverge. Live-verified: selecting the Aerial Perspective Volume leaf shows a real, glowing, tapering-frustum ray-marched thumbnail, visually distinct from every 2D LUT preview. 9 new tests (6 snapshot-builder + 3 in a new `VolumeTexturePreviewRendererTests.cpp` file). |
| **PHASE5** — Tests/Docs/Full Build/Live Verification (this phase) | Confirmed PHASE1-4's own tests needed zero further changes. Swept every stale doc claim (`AGENTS.md`, `docs/conventions/frame-debugger.md`, `TODO.md`, `README.md`). Ran a full clean build of BOTH `GTE_ENABLE_EDITOR` configurations (zero errors) and the FULL `ctest` suite (100% pass, 1429/1430, 1 pre-existing machine-gated skip) — TWICE, once before and once after a real bug found during the live smoke test. **Found and fixed a real, confirmed gap the live pass revealed**: 3 of the 9 real compute-dispatch leaves write to this engine's one genuinely HDR 2D-texture format (`VK_FORMAT_R16G16B16A16_SFLOAT`), and PHASE3's straight GPU-to-GPU copy — while byte-correct — displayed as visually indistinguishable from solid black in the Inspector, since nothing in that path applied the same debug exposure/tonemap `GET /get_texture` already does for this exact format. Fixed by reusing that EXACT SAME already-shipped `Encoding::ConvertHdrRgba16fToRgba8()` function via a CPU round-trip (mirroring PHASE4's own volume-preview upload sequence), applied ONLY to the HDR-format writes — every existing LDR compute-pass preview stays on the exact same zero-risk direct-copy path, byte-for-byte unchanged. |

## 3. Final architecture (after this campaign)

```
RenderGraphBuilder::AddComputePass("AnyComputePassName", setup, execute)   <- THE CHOKE POINT (PHASE1)
    | stamps PassRecord::isComputePass = true - the ONE new fact recorded here,
    | forever, for every future compute pass too
    v
RenderGraphCompiler::Compile() -> CompiledGraph        (culling logic UNCHANGED)
    v
RenderGraphSnapshot::BuildRenderGraphSnapshot()        <- copies isComputePass/readKinds/writeKinds
    |                                                     through, for surviving AND culled passes (PHASE1)
    v
RenderGraphSnapshot::passesInExecutionOrder[]          <- every surviving compute pass is SELF-DESCRIBING
    v
FrameDebuggerData.cpp::BuildRealFrameDebuggerSnapshot() <- ONE generic loop over every isComputePass==true,
    |                                                       non-culled pass -> BuildComputeDispatchLeaf() (PHASE2)
    v
root "Game View"
  |-- "Compute Dispatches (Pre-GameView)"   (present only if >=1 real child, in real execution order)
  |     |-- <every real compute pass whose own index precedes "GameView"'s own index>
  |-- "GameView" leaf                        (unchanged - the one real graphics/draw pass)
  |-- "Compute Dispatches (Post-GameView)"  (present only if >=1 real child, in real execution order)
        |-- <every real compute pass whose own index follows "GameView"'s own index>
        v
FrameDebuggerHistory::CaptureFrame()   <- discovers every real compute pass's own first Texture-kind write
    |                                     (CollectComputePassTextureWrites(), PHASE3) AND first
    |                                     VolumeTexture-kind write (CollectComputePassVolumeTextureWrites(), PHASE4)
    v
   for each Texture-kind write:
     - LDR (R8G8B8A8_UNORM/BGRA8_UNORM): straight vkCmdCopyImage inside the shared ImmediateSubmit() (PHASE3)
     - HDR (R16G16B16A16_SFLOAT): CaptureImagePixels() + Encoding::ConvertHdrRgba16fToRgba8() (the SAME
       function GET /get_texture already uses) + staging-buffer upload (PHASE5 bug fix)
   for each VolumeTexture-kind write:
     - VolumeTexturePreviewRenderer::RenderPreview() (the SAME ray-march GET /get_texture already uses,
       PHASE4) + staging-buffer upload
    v
FrameDebuggerHistoryEntry { snapshot, preview, compositedPreview, computePassPreviews[] }
    v
ChooseFrameDebuggerPreviewSource()   <- a selected compute leaf's own retained preview wins over the
    |                                   whole-frame CompositedPreview (PHASE3)
    v
Selecting ANY compute-dispatch leaf shows THAT PASS'S OWN real, correct, DISTINCT output image
```

## 4. Full file-change inventory (whole campaign)

- `src/Renderer/RenderGraph/RenderGraphTypes.h` — `PassRecord::isComputePass` (PHASE1)
- `src/Renderer/RenderGraph/RenderGraphBuilder.h` — `AddComputePass()` sets the new flag (PHASE1)
- `src/Renderer/RenderGraph/RenderGraphSnapshot.h`/`.cpp` — `RenderGraphPassSnapshot::isComputePass`/
  `readKinds`/`writeKinds`; `ResourceUsageName()` rewritten to an exhaustive switch (PHASE1)
- `src/Editor/FrameDebuggerData.h`/`.cpp` — generic `BuildComputeDispatchLeaf()` (PHASE2);
  `CollectComputePassTextureWrites()` (PHASE3); `CollectComputePassVolumeTextureWrites()` (PHASE4); widened
  `ChooseFrameDebuggerPreviewSource()` (PHASE3)
- `src/Editor/FrameDebuggerHistory.h`/`.cpp` — new `computePassPreviews` field + widened `CaptureFrame()`
  (PHASE3); the volume-texture ray-march branch (PHASE4); the HDR-format CPU-round-trip branch (PHASE5 bug fix)
- `src/Editor/Panels/FrameDebuggerPanel.h`/`.cpp` — drops the dead `gpuSkinningPassNamesThisFrame` plumbing
  (PHASE2); widened `EnsurePreviewDescriptor()` picking call (PHASE3)
- `src/Editor/ImGuiEditorLayer.cpp` — `Build()` call-site simplification once the GPU-skinning name list was
  no longer needed (PHASE2)
- `src/Renderer/VolumeTexturePreviewRenderer.h`/`.cpp` — extracted `SelectVolumeTexturePreviewInterpretation()`
  (PHASE4)
- `src/Application/Application.cpp` — `GET /get_texture`'s volume branch now calls the extracted shared
  function instead of re-deriving the rule inline (PHASE4)
- `tests/Renderer/RenderGraph/RenderGraphTypesTests.cpp`/`RenderGraphBuilderTests.cpp`/`RenderGraphSnapshotTests.cpp`
  (PHASE1)
- `tests/Editor/FrameDebuggerSnapshotBuilderTests.cpp` (PHASE2 rewrites + new tests; PHASE3/PHASE4 new tests)
- `tests/Editor/FrameDebuggerDataTests.cpp` (PHASE3 — widened `ChooseFrameDebuggerPreviewSourceTest`)
- `tests/Renderer/VolumeTexturePreviewRendererTests.cpp` — new file (PHASE4)
- `AGENTS.md`, `docs/conventions/frame-debugger.md`, `README.md`, `TODO.md` — doc corrections (PHASE5)
- `task_manager/frame-debugger-5/PHASE1_COMPLETION_REPORT.md` .. `PHASE5_COMPLETION_REPORT.md`,
  `CAMPAIGN_COMPLETION_REPORT.md` (this file)

## 5. Final verification evidence

- **Full clean build, `GTE_ENABLE_EDITOR=ON`**: 442/442 steps, zero errors (run twice — before and after
  PHASE5's own HDR bug fix; both green).
- **Full clean build, `GTE_ENABLE_EDITOR=OFF`**: 367/367 steps, zero errors (run twice, same reason). Confirmed
  every Frame-Debugger-specific file is absent from this build's compile log, while PHASE1's core
  `RenderGraphTypes.cpp`/`RenderGraphBuilder.cpp`/`RenderGraphSnapshot.cpp` changes compile in both
  configurations, unchanged.
- **Full `ctest` regression**: 100% pass, 1429/1430 (1 pre-existing, machine-gated skip, unrelated to this
  campaign) — run twice, both green, no new failures or loosened assertions at any point in this campaign.
- **Live, HTTP-automation-driven, screenshot-verified smoke test**: `GET /frame_debugger/state` confirmed the
  exact predicted split-group tree shape (`totalEventCount: 10` — 9 compute-dispatch leaves + `"GameView"`,
  5 Pre-GameView / 4 Post-GameView). Every one of the 5 explicitly-required leaf kinds (Transmittance LUT,
  Multi-Scattering LUT, Sky-View LUT, Aerial Perspective Volume, Aerial Perspective Composite) was individually
  selected and screenshotted, each showing a real, correct, DISTINCT output image — not a copy of the whole
  Game View, not a copy of a sibling leaf, and (after PHASE5's own bug fix) not a blank/black rectangle. The
  Aerial Perspective Composite leaf was re-confirmed to pixel-match `GET /get_game_view` exactly, matching
  `frame-debugger-4`'s own already-proven behavior. GPU Skinning's own leaf presence was not independently
  re-verified live this campaign (no HTTP-spawnable skinned model existed in this session — see PHASE5's own
  completion report for the full, explicit reasoning), but its discovery code path is identical,
  non-special-cased, and already exhaustively Tier-1-tested.

## 6. Outstanding / deferred items

- **True per-pass "stop"/breakpoint execution control** (pausing the GPU mid-frame at a specific compute
  dispatch boundary for live step-through inspection) — explicitly named and deferred by this campaign's own
  `PHASE0_MASTER_STRATEGY.md` Non-Goals from the very start. This campaign delivered "trackable" in full
  (every real compute dispatch is now a real, inspectable tree leaf with its own real output preview) and
  "stoppable" only in the sense that the EXISTING Enable/Step/Capture frame-level controls (`frame-debugger-3`'s
  own PHASE3) already let an engineer freeze a specific frame and walk its compute passes one at a time in the
  tree. A genuine mid-command-buffer GPU pause/breakpoint would need this engine's
  `Renderer::ImmediateSubmit()`/render-graph model to support a partial, resumable command-buffer submission,
  which it does not today — carried forward as a named, still-deferred future item in `TODO.md`'s "Frame
  Debugger" section, exactly as `PHASE0_MASTER_STRATEGY.md` requires.
- **GPU-Skinning leaf presence was not independently re-verified against a real running skinned model this
  campaign** — no HTTP endpoint exists today to spawn a GPU-skinned rigged model on demand (only
  `/instantiate_primitive`/`/instantiate_light` exist), and no skinned model was loaded in the default scene
  this session. GPU Skinning passes are discovered via the exact same generic, non-special-cased `isComputePass`
  mechanism every other verified leaf uses, and this exact scenario (a `Buffer`-kind-write compute pass
  modeling GPU Skinning's own shape) is already exhaustively covered by PHASE2's own Tier-1 tests
  (`TwoPreGameViewComputePassesProduceOnePreGameViewGroup`,
  `PreAndPostGameViewGroupsAppearTogetherAsSiblingsInRealExecutionOrder`) — a future session with a real
  GPU-skinned model loaded could close this specific live-verification gap with no code changes needed.
- **Compute Blur Validation's own `"ComputeBlurValidation"` pass** was not exercised live this campaign either
  (it is Scene-View/debug-toggle-gated, off by default, and explicitly out of this phase's own REQUIRED leaf
  list per `PHASE5_TESTS_DOCS_FULL_BUILD_AND_LIVE_VERIFICATION.md`'s own Step 3.5) — its discovery/preview path
  is identical to every other verified compute pass, so this is a documentation/verification-coverage gap only,
  not a suspected functional one.
- Every other Non-Goal from `PHASE0_MASTER_STRATEGY.md` (no change to actual atmosphere-scattering math/shader
  files, no change to `RenderGraphCompiler`'s culling/dependency-ordering logic, no change to `GET /get_texture`/
  `GET /list_textures`/`RenderGraphDebugTextureRegistry`/`VolumeTexturePreviewRenderer`'s own public contracts,
  no Scene-View Frame Debugger capture, still pass-level not per-draw-call granularity) remains exactly as
  scoped from the start — none of these were touched by this campaign, confirmed by the file-change inventory
  above.
