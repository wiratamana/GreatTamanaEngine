# PHASE5 COMPLETION REPORT — Migrate the Remaining Passes

_Child of `PHASE0_MASTER_STRATEGY.md`. Campaign: `render-pass-1`, branch
`feature/render-pass-impl`._

## Summary

Implemented `PHASE5_REMAINING_PASSES_MIGRATION.md` exactly per its own Step 3
plan (read in full, along with `PHASE0_MASTER_STRATEGY.md` and
`PHASE4_COMPLETION_REPORT.md`, before any code was touched). Finished the
"full migration" scope: every remaining production `builder.AddPass()`/
`builder.AddComputePass()` call site in the engine (GPU Skinning, `"Present"`,
Compute Blur Validation) now goes through the `AddRenderPass()` chokepoint,
correctly tagged. `AddFrameDebuggerReplayPasses()` (item 3 in the phase's own
scope) was confirmed ALREADY migrated by PHASE4's own Step 3.3b, exactly as
that phase's completion report and this phase's own document both predicted —
no regression found, nothing to redo. No genuine design ambiguity was hit
during implementation — every judgment call this phase needed was already
resolved by the phase file itself or by `PHASE0_MASTER_STRATEGY.md`, so
`ask_questions` was never needed.

## What Changed

### 1. `src/Application/RenderPasses.cpp`

- **`AddGpuSkinningPasses()`** (Step 3.1): `builder.AddComputePass(request.name,
  setup, execute)` → `builder.AddRenderPass(request.name, rg::PassKind::Compute,
  rg::ViewScope::Shared, rg::RenderPassCategory::GpuSkinning, setup, execute)`.
  `ViewScope::Shared` stated explicitly (matches what the old 3-arg
  `AddComputePass` already defaulted to). No behavior change beyond the new
  stamped `kind`/`viewScope`/`category` metadata.
- **`AddPresentPass()`** (Step 3.2): `builder.AddPass("Present", setup,
  execute)` → `builder.AddRenderPass("Present", rg::PassKind::Graphics,
  rg::ViewScope::Shared, rg::RenderPassCategory::General, setup, execute)`. No
  behavior change — `"Present"` remains correctly excluded from the Frame
  Debugger's own Game-View-only tree (a completely separate
  `RenderGraph::Execute()` call from the offscreen one the Frame Debugger's
  snapshot is built from — confirmed unchanged).

### 2. `src/Editor/ComputeBlurValidation.cpp`

- **`ComputeBlurValidation::AddPass()`** (Step 3.4): its own internal
  `builder.AddComputePass("ComputeBlurValidation", rg::ViewScope::SceneView,
  setup, execute)` call → `builder.AddRenderPass("ComputeBlurValidation",
  rg::PassKind::Compute, rg::ViewScope::SceneView, rg::RenderPassCategory::Debug,
  setup, execute)`. Kept the exact same `"ComputeBlurValidation"` name literal
  (metadata-only change, per the phase document's explicit instruction not to
  rename it). This pass was already excluded from the Game-View-only Frame
  Debugger tree by the pre-existing `ViewScope::SceneView` filter; the new
  `Debug` category tag is a second, independent safety net, exactly as the
  phase document specifies.

### 3. `AddFrameDebuggerReplayPasses()` (Step 3.3 — CONFIRMATION ONLY)

Re-read the function in `src/Application/RenderPasses.cpp` directly: it
already calls `builder.AddRenderPass(passName, rg::PassKind::Graphics,
rg::ViewScope::GameView, rg::RenderPassCategory::Debug, setup, execute)` (its
own PHASE4 Step 3.3b migration), and `BuildRealFrameDebuggerSnapshot()`'s "view
region" walk (`FrameDebuggerData.cpp`, also from PHASE4) already excludes
`category == RenderPassCategory::Debug` passes. **No PHASE4 regression found —
this migration landed correctly exactly as PHASE4's own completion report
claimed.** Nothing further to do here.

## Verification

### 3.5 — Final grep audit (whole `src/` tree)

- `search_in_dir` for `.AddPass(` across `src/**/*.cpp`: exactly 2 hits remain —
  `Application/RenderPasses.cpp:352` (`AddSceneViewPass()`'s own `"SceneView"`
  pass — the deliberate, permanent PHASE0/PHASE2-locked carve-out, 3.5(c)) and
  `Editor/ImGuiEditorLayer.cpp:458` (`m_blurValidation.AddPass(builder, ...)` —
  the documented false-positive grep hit on `ComputeBlurValidation::AddPass()`'s
  own unrelated class-method name, 3.5(d)). Both are the exact two carve-outs
  this phase's own document names — confirmed expected, not gaps.
- `search_in_dir` for `.AddComputePass(` across `src/**/*.cpp`: **zero hits**
  (both former production call sites — GPU Skinning and Compute Blur
  Validation — are now migrated).
- `search_in_dir` for `.AddPass(`/`.AddComputePass(` across `src/**/*.h`: zero
  hits (the low-level primitives themselves are defined without a leading
  `.` prefix inside `RenderGraphBuilder.h`, so they never match this grep
  pattern in the first place — consistent with carve-out 3.5(a)).
- `search_in_dir` for both patterns across `tests/`: all remaining hits live
  under `tests/Renderer/RenderGraph/` (`RenderGraphBuilderTests.cpp`,
  `RenderGraphCompilerTests.cpp`, `RenderGraphSnapshotTests.cpp`) —
  intentional, direct low-level-primitive exercises, consistent with carve-out
  3.5(b) (the phase document names one file as an example; these sibling
  files in the same folder testing the same primitives are the same category
  of expected survivor, not a gap).
- **No genuine gap found beyond the plan's own four named call sites** — the
  audit confirms full production migration.

### Compile

- `cmake --build build --target gte_core` — clean, zero errors/warnings (only
  `ComputeBlurValidation.cpp`/`RenderPasses.cpp` recompiled).
- `cmake --build build --target GreatTamanaEngineTests` — clean link.
- `cmake --build build --target GreatTamanaEngine` — clean link.

### Tests

Ran the directly-relevant suites (`GreatTamanaEngineTests.exe
--gtest_filter=RenderGraphBuilderTest.*:RenderGraphCompilerTest.*:RenderGraphSnapshotTest.*:FrameDebuggerSnapshotBuilderTest.*:FrameDebuggerDataTest.*`)
— **119/119 passed**, zero regressions. No test file needed a change: this
phase is a pure metadata migration on three call sites whose observable
behavior (resource declarations, execution order, Frame Debugger tree
visibility) is unchanged by design, and the one file with real coverage of
`AddRenderPass()`'s category/kind/viewScope stamping behavior
(`RenderGraphBuilderTests.cpp`) already exercises the exact code paths this
phase's three edits now also go through.

### Live runtime smoke test (`run_app_background` + `gte_send_request`)

- Launched `GreatTamanaEngine.exe`, `POST /instantiate_primitive`
  (`{"shape":"cube","name":"SmokeTestCube"}`) spawned a real entity.
- `GET /frame_debugger/open` → `GET /frame_debugger/enable?value=true` →
  `GET /frame_debugger/state` confirmed a real capture (`hasCapturedFrame:
  true`, `totalEventCount: 9` — identical count to PHASE4's own baseline run,
  confirming zero behavior change).
- `GET /get_swapchain` screenshot confirmed the exact same tree shape PHASE4
  verified: `"Compute LUT"` (5 real Atmosphere LUT passes) → `"RenderOpaque"`
  (with its own `"SmokeTestCube (Entity 1)"` child) → `"DrawSkyBackground"` →
  `"Compute Dispatches (Post-GameView)"` (`AtmosphereAerialPerspectiveCompositePass`)
  — **no `FrameDebuggerReplayStepN` leaves anywhere** (even though this exact
  `enable` + implicit-capture sequence is precisely what triggers those N
  replay passes to be declared), and **no `ComputeBlurValidation` leaf**
  (correctly absent — it's a `SceneView`-only debug tool, not part of this
  Game-View tree at all, and wasn't engaged this run). This is the concrete,
  live confirmation that this phase's `Debug`-category migration for Compute
  Blur Validation didn't change anything observable, and that PHASE4's own
  replay-pass exclusion still holds after this phase's unrelated edits.
- Attempted cleanup (`GET /delete_entity?index=1&generation=1`) returned 404 —
  same harmless, already-documented (PHASE4 report) non-issue: the test
  entity was never persisted and the process was terminated immediately
  after via `stop_app_background`, so no state was left behind.

## Deviations From The Phase Document

None. Every call site named in Step 3 was migrated exactly as specified; the
one "confirmation only" item (3.3, the Frame Debugger replay passes) was
confirmed already correctly done by PHASE4, with no discrepancy found. The
3.5 grep audit found no additional production call sites beyond the two named
permanent carve-outs.

## Definition of Done — Checklist

- [x] `AddGpuSkinningPasses()` now goes through `AddRenderPass()`, tagged
      `PassKind::Compute` / `ViewScope::Shared` / `RenderPassCategory::GpuSkinning`.
- [x] `AddPresentPass()` now goes through `AddRenderPass()`, tagged
      `PassKind::Graphics` / `ViewScope::Shared` / `RenderPassCategory::General`.
- [x] `ComputeBlurValidation::AddPass()`'s own internal call now goes through
      `AddRenderPass()`, tagged `PassKind::Compute` / `ViewScope::SceneView` /
      `RenderPassCategory::Debug`.
- [x] `AddFrameDebuggerReplayPasses()` confirmed already migrated by PHASE4 —
      no regression found, nothing to redo.
- [x] The `search_in_dir` audit (3.5) confirms no remaining production
      `AddPass()`/`AddComputePass()` call sites outside `RenderGraphBuilder`
      itself and its own tests, except the two known, permanent carve-outs
      (`AddSceneViewPass()`'s `"SceneView"` pass, `ImGuiEditorLayer.cpp`'s
      `m_blurValidation.AddPass()` false-positive grep hit).
- [x] The Frame Debugger's tree (re-verified live via HTTP) still shows no
      leaves for the N Frame Debugger replay passes or for
      `ComputeBlurValidation` — confirming the `Debug` category's exclusion
      guard, and this phase's edits, introduce zero regressions.
- [x] Incremental compile succeeds (`gte_core`, `GreatTamanaEngineTests`,
      `GreatTamanaEngine` all built clean); 119/119 directly-relevant tests
      pass; completion report + git commit follow.

## Handoff To PHASE6

Every real render/compute/blit pass declaration in the engine — Atmosphere
LUTs/composite (PHASE3), Opaque/Sky/Transparent (PHASE2), GPU Skinning,
Present, Frame Debugger Replay, Compute Blur Validation (this phase) — now
goes through the single `AddRenderPass()` chokepoint, correctly tagged with
`PassKind`/`ViewScope`/`RenderPassCategory`. The only two `builder.AddPass()`
survivors are the deliberate, permanent `AddSceneViewPass()` carve-out and one
unrelated-method-name false-positive grep hit — both expected, not gaps.
PHASE6 (`PHASE6_APPLICATION_ORCHESTRATION_CLEANUP_AND_DOCS.md`) can now
proceed with updating `Application::Run()`'s call sites to any renamed
functions, deleting now-dead code, and updating `AGENTS.md`/
`docs/conventions/frame-debugger.md` to describe this campaign's final shape.
