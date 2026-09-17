# PHASE1 COMPLETION REPORT — RenderGraph ViewScope Choke-Point Infrastructure

Campaign: `frame-debugger-6`. Phase: PHASE1 (Workstream A foundation). Branch:
`feature/frame-debugger-impl` (unchanged, per prerequisites — no branch switch
performed).

## Summary

Implemented `PHASE1_RENDERGRAPH_VIEWSCOPE_CHOKEPOINT_INFRASTRUCTURE.md` exactly
as specified, Steps 1–5, with no deviation from the strategy document. A new,
structurally-tracked `gte::rg::ViewScope` enum (`Shared`/`GameView`/
`SceneView`) now flows through `PassRecord`/`RenderGraphPassSnapshot` via two
new, purely-additive 4-argument `RenderGraphBuilder::AddPass()`/
`AddComputePass()` overloads, and every genuinely per-view pass in the engine
(the Sky-View LUT, Aerial Perspective Volume, Aerial Perspective Volume Debug
Slice, Aerial Perspective Composite, `GameView`/`SceneView` themselves, and
`ComputeBlurValidation`) now stamps the correct, explicit `ViewScope` at its
real call site. This phase intentionally changes **zero** Frame Debugger
display behavior — that is PHASE2's job — and this was explicitly verified
live (see "Manual/Live Verification" below): the tree shape (duplicate-pass
bug included) is byte-for-byte identical to before this phase.

## Files Changed

### Core render graph infrastructure
- `src/Renderer/RenderGraph/RenderGraphTypes.h` — added `enum class ViewScope
  { Shared, GameView, SceneView };` immediately after `ResourceKind`, and
  `PassRecord::viewScope` (default `ViewScope::Shared`), immediately after
  `PassRecord::isComputePass`.
- `src/Renderer/RenderGraph/RenderGraphSnapshot.h` — added
  `RenderGraphPassSnapshot::viewScope` (default `ViewScope::Shared`), parallel
  to `isComputePass`.
- `src/Renderer/RenderGraph/RenderGraphSnapshot.cpp` — `BuildPassSnapshot()`
  (the one shared helper both the surviving-passes loop and the culled-passes
  loop call) now also stamps `snapshot.viewScope = pass.viewScope;`,
  unconditionally (same placement/rule as `isComputePass` itself) — covers
  both loops with a single line, exactly as the phase document specifies.
- `src/Renderer/RenderGraph/RenderGraphBuilder.h` — added the two new
  4-argument `AddPass(name, viewScope, setup, execute)` /
  `AddComputePass(name, viewScope, setup, execute)` template overloads,
  immediately after their existing 3-argument counterparts. Both simply
  delegate to the existing overload then stamp `m_passes.back().viewScope =
  viewScope;`. Every pre-existing 3-argument call site across the whole
  codebase continues to compile completely unchanged (confirmed by the
  incremental build below), still implicitly defaulting to `ViewScope::Shared`.

### Application-layer threading (Step 3.4/3.5)
- `src/Renderer/Atmosphere/AtmosphereLutRenderer.h/.cpp` — added a new,
  required, trailing `rg::ViewScope viewScope` parameter to:
  - `AddSkyViewLutPass(...)`
  - `AddAerialPerspectiveVolumePass(...)`
  - `AddAerialPerspectiveVolumeDebugSlicePass(...)` (per the doc's explicit
    instruction, this method still never hardcodes an assumption about which
    view it's for internally, even though its one real call site today always
    passes `GameView`)
  - `AddAerialPerspectiveCompositePass(...)`

  Each forwards its own `viewScope` straight into its own internal
  `builder.AddComputePass(name, viewScope, setup, execute)` call (the
  4-argument overload). `AddTransmittanceLutPass()`/`AddMultiScatteringLutPass()`
  were deliberately left UNCHANGED (still 3-argument `AddComputePass()` calls,
  implicitly `Shared`) — these two passes are genuinely computed once per
  frame, not once per view, per Locked Design Decision #5.
- `src/Application/AtmospherePassSequence.h/.cpp` — `AddAtmosphereViewLutPasses(...)`
  and `AddAtmosphereCompositePass(...)` each gained a new, required, trailing
  `rg::ViewScope viewScope` parameter, forwarded straight through to the
  `AtmosphereLutRenderer` calls they wrap. `AddAtmosphereSharedLutPasses(...)`
  was deliberately left UNCHANGED (no `ViewScope` parameter at all) — per the
  strategy document's explicit instruction, since its own two passes
  (Transmittance/Multi-Scattering) are genuinely `Shared`.
- `src/Application/RenderPasses.cpp` — `AddGameViewPass()`'s own
  `builder.AddPass("GameView", ...)` call now passes `rg::ViewScope::GameView`
  explicitly; `AddSceneViewPass()`'s own `builder.AddPass("SceneView", ...)`
  call now passes `rg::ViewScope::SceneView` explicitly. `AddPresentPass()`
  was deliberately left on the 3-argument overload (implicit `Shared`) — the
  phase document explicitly calls this the "simpler choice", noting it does
  not affect PHASE2's correctness either way.
- `src/Editor/ComputeBlurValidation.cpp` — its own internal
  `builder.AddComputePass("ComputeBlurValidation", ...)` call now passes
  `rg::ViewScope::SceneView` explicitly — re-confirmed, exactly as the
  strategy document instructed, by re-reading `Application.cpp`'s own real
  call site (`m_editorLayer->AddBlurValidationPass(b, m_renderer, h, extent)`,
  called only inside the `if (sceneTarget != nullptr)` block, reading the
  Scene View's own pre-composite texture `h`) — this assumption still holds.
- `src/Application/Application.cpp` — supplied the real `ViewScope` value at
  every one of the 5 real per-view call sites (all re-located by searching for
  the function names, per the doc's own instruction to never trust stale line
  numbers, since the file had already grown since PHASE0 was written):
  - Both `AddAtmosphereViewLutPasses(...)` calls (`gameTarget`/`sceneTarget`
    blocks) — `rg::ViewScope::GameView` / `rg::ViewScope::SceneView`
    respectively.
  - Both `AddAtmosphereCompositePass(...)` calls — same split.
  - The one `AddAerialPerspectiveVolumeDebugSlicePass(...)` call (inside the
    `gameTarget` block only) — `rg::ViewScope::GameView`.

  Every new argument is spelled `rg::ViewScope::...` (short, unqualified `rg::`
  prefix), matching every other `rg::`-qualified symbol already used at these
  exact call sites in this file (`Application.cpp` is already entirely inside
  `namespace gte { ... }`) — no redundant `gte::` prefix introduced.

### Tests (Step 4 — Testability & Regression Safety)
- `tests/Renderer/RenderGraph/RenderGraphBuilderTests.cpp` — 4 new cases:
  - `AddPassThreeArgumentOverloadLeavesViewScopeShared`
  - `AddComputePassThreeArgumentOverloadLeavesViewScopeShared`
  - `AddPassFourArgumentOverloadStampsViewScope`
  - `AddComputePassFourArgumentOverloadStampsViewScopeAndIsComputePass`
- `tests/Renderer/RenderGraph/RenderGraphSnapshotTests.cpp` — 1 new case,
  `ViewScopeIsCopiedThroughForSurvivingAndCulledPasses`, covering a plain
  `Shared`-defaulted surviving pass, an explicitly `GameView`-tagged surviving
  compute pass, and an explicitly `SceneView`-tagged **culled** compute pass —
  proving `viewScope` is copied through correctly (and truthfully, even for a
  pass that never ran) via the one shared `BuildPassSnapshot()` helper.
- No Frame-Debugger test file was touched in this phase (that is PHASE2's
  ownership), per the strategy document's own Step 4 instruction.

## Deviations From The Strategy Document

None. Every instruction in Steps 1–5 was followed as written; every
double-checked assumption (the `ComputeBlurValidation` Scene-View-only claim,
the `Application.cpp` call-site locations, the `rg::` vs `gte::rg::` spelling
convention) was independently re-verified against the current source rather
than trusted from the document's own planning-time notes, and all held true.

## Evidence

### Incremental build
`cmake --build build` (working directory
`C:\Users\F5954\Documents\TAMANA\GreatTamanaEngine`) completed successfully —
`gte_core`, `GreatTamanaEngine.exe`, and `GreatTamanaEngineTests.exe` all
built and linked cleanly with `GTE_ENABLE_EDITOR=ON` (the default dev
configuration already present in `build/`). No compile errors or warnings
related to this phase's changes.

### Tests
Ran `build/tests/GreatTamanaEngineTests.exe` directly (not a full `ctest`
run, per this campaign's own "no full build/ctest except PHASE5" rule) with
`--gtest_filter=*RenderGraph*:*FrameDebugger*` — **251 tests, 251 passed, 0
failed**, including:
- The 4 new `RenderGraphBuilderTest.*ViewScope*` cases — all pass.
- The 1 new `RenderGraphSnapshotTest.ViewScopeIsCopiedThroughForSurvivingAndCulledPasses`
  case — passes.
- Every pre-existing `RenderGraphBuilderTest`/`RenderGraphSnapshotTest`/
  `RenderGraphCompilerTest`/`RenderGraphBarrierPlannerTest`/
  `FrameDebugger*Test` case — still passes unchanged, confirming no
  regression.

### Manual/Live Verification (Step 5's own Definition of Done)
1. Launched `build/GreatTamanaEngine.exe` via `run_app_background`.
2. `POST /load_scene` with body `{}` → `{"resolved_path":"...\\Project\\TestScene.gtscene","success":true}`.
3. `GET /frame_debugger/open` → `windowOpen:true`.
4. `GET /frame_debugger/enable?value=true` → `enabled:true, historyCount:1,
   totalEventCount:10`.
5. `GET /get_swapchain` — screenshot confirms the Frame Debugger window shows:
   - `"Game View"` group → `"Compute Dispatches (Pre-GameView)"` containing
     `AtmosphereTransmittanceLutPass`, `AtmosphereMultiScatteringLutPass`,
     `AtmosphereSkyViewLutPass`, `AtmosphereAerialPerspectiveVolumePass`,
     `AtmosphereAerialPerspectiveVolumeDebugSlicePass` (5 leaves).
   - `"GameView"` leaf itself.
   - `"Compute Dispatches (Post-GameView)"` containing
     `AtmosphereAerialPerspectiveCompositePass`, `AtmosphereSkyViewLutPass`
     (duplicate), `AtmosphereAerialPerspectiveVolumePass` (duplicate),
     `AtmosphereAerialPerspectiveCompositePass` (duplicate) — 4 leaves.
   - Total: 5 + 1 + 4 = 10, matching `totalEventCount:10` from step 4.

   This is **exactly** the known, pre-existing duplicate/mis-scoped-pass bug
   described in `PHASE0_MASTER_STRATEGY.md` Section 0 — unfixed, unchanged,
   byte-for-byte the same tree shape as before this phase. This is the
   CORRECT and EXPECTED outcome for PHASE1 (per its own Definition of Done):
   the new `ViewScope` data is now flowing correctly through the render graph,
   but nothing in `FrameDebuggerData.cpp` reads it yet — that is PHASE2's job.
6. Stopped the background process via `stop_app_background`.

## Definition of Done — Checklist

- [x] `ViewScope` enum added to `RenderGraphTypes.h`, `PassRecord::viewScope`
      field added (default `Shared`).
- [x] `RenderGraphPassSnapshot::viewScope` added, stamped through
      `BuildRenderGraphSnapshot()` for both surviving and culled passes.
- [x] `RenderGraphBuilder::AddPass()`/`AddComputePass()` 4-argument overloads
      added; every pre-existing call site across the whole codebase still
      compiles UNCHANGED.
- [x] Every genuinely per-view pass identified in Step 3.4 now calls the new
      4-argument overload with the correct, explicit `ViewScope` — re-verified
      each one against `Application.cpp`'s real call sites.
- [x] `AddGpuSkinningPasses()` and `AddAtmosphereSharedLutPasses()`'s own
      internal passes are UNCHANGED (still implicitly `Shared` via the
      3-argument overload) — confirmed by reading the final diff.
- [x] New/updated Tier-1 tests per Step 4 pass.
- [x] Incremental build succeeds (`cmake --build build`).
- [x] Manual sanity check: tree shape is byte-for-byte the same as before
      this phase (duplicate bug still present, unfixed — correct for PHASE1).
- [x] Phase completion report written (this file) and committed.

## Next Phase

PHASE2 (`PHASE2_FRAME_DEBUGGER_VIEWSCOPE_FILTERED_DISCOVERY.md`) consumes the
new `ViewScope` data in `FrameDebuggerData.cpp` to actually fix the
duplicate/mis-scoped-pass bug this phase's live verification just
re-confirmed is still present.
