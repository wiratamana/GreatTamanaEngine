# PHASE4 COMPLETION REPORT — Frame Debugger Generic Tree Rework

_Child of `PHASE0_MASTER_STRATEGY.md`. Campaign: `render-pass-1`, branch
`feature/render-pass-impl`._

## Summary

Implemented PHASE4 exactly per `PHASE4_FRAME_DEBUGGER_GENERIC_TREE_REWORK.md`
(read twice, plus `PHASE4_PRECHECK_REPORT.md` and `docs/conventions/
frame-debugger.md` in full, before any code was touched, per this phase's own
elevated-risk instructions). `BuildRealFrameDebuggerSnapshot()`
(`src/Editor/FrameDebuggerData.cpp`) now builds the whole Frame Debugger event
tree GENERICALLY from real `PassKind`/`RenderPassCategory`/`ViewScope`/
execution-order metadata — the hardcoded `"GameView"`-literal pivot search is
gone (replaced by a `"RenderOpaque"` pivot lookup — the one and only pass-name
string literal this function still contains), and the `isSkyBackgroundDraw`/
`RecordSkyBackgroundDraw()` hack is removed entirely. No genuine design
ambiguity was hit during implementation — every judgment call this phase
needed was already resolved by the phase file itself (including its own
pre-check fix) or by `PHASE0_MASTER_STRATEGY.md`, so `ask_questions` was never
needed.

## What Changed

### 1. `src/Editor/FrameDebuggerCapture.h` / `.cpp`

- Removed `FrameDebuggerDrawRecord::isSkyBackgroundDraw` and
  `FrameDebuggerCaptureContext::RecordSkyBackgroundDraw()` entirely (Step
  3.4) — replaced with doc comments explaining why/where the mechanism moved
  (the Sky Background draw is now a real, separate, generically-discovered
  `"DrawSkyBackground"` Render Graph pass).

### 2. `src/Application/RenderPasses.cpp` / `.h`

- `AddDrawSkyBackgroundPass()`'s `execute` lambda no longer calls
  `frameDebuggerCapture->RecordSkyBackgroundDraw(...)` — the temporary bridge
  call PHASE2 left behind is deleted. The `frameDebuggerCapture` parameter is
  kept (for signature symmetry with `AddRenderOpaquePass()`) but is now
  `(void)`-cast and unused in the body — documented as deliberate.
- Removed the now-unused `#include "../Renderer/Atmosphere/
  AtmosphereSkyBackgroundRenderer.h"` (its only use, `ShaderDebugName()`, was
  inside the deleted call).
- `AddFrameDebuggerReplayPasses()`'s own pass declaration (Step 3.3b — the
  pre-check's own pulled-forward fix) now goes through
  `builder.AddRenderPass(passName, rg::PassKind::Graphics,
  rg::ViewScope::GameView, rg::RenderPassCategory::Debug, setup, execute)`
  instead of plain `builder.AddPass(passName, rg::ViewScope::GameView, ...)` —
  same `name`/`setup`/`execute`, zero behavior change beyond the new stamped
  `Debug` category, which is exactly what makes the tree-rework's own
  exclusion guard (below) actually effective.

### 3. `src/Editor/FrameDebuggerData.cpp` (the heart of this phase)

- `BuildRealFrameDebuggerSnapshot()`'s pivot lookup: `FindPassByName(...,
  "GameView")` → `FindPassByName(..., "RenderOpaque")` (Step 3.1).
- The old single pre-view compute loop is now ONE forward loop over `[0,
  pivotIndex)` that routes each surviving, non-SceneView compute pass into
  either `"Compute LUT"` (`category == AtmosphereLut`) or `"Compute
  Dispatches (Pre-GameView)"` (every other category), in real execution
  order for `eventIndex` purposes, but presented in the tree with `"Compute
  LUT"` always FIRST regardless of real interleaving order (Step 3.2).
- A brand-new "view region" walk (Step 3.3) replaces the single `"GameView"`
  leaf: starting at the `"RenderOpaque"` pivot, it walks forward, stopping at
  the first surviving `Compute`-kind pass; every surviving, non-SceneView,
  non-`Debug`-category `Graphics`-kind pass it visits becomes a real sibling
  leaf — the first one (`"RenderOpaque"` itself) keeps its existing per-entity
  children mechanism (`BuildRenderOpaqueLeaf()`/
  `BuildRenderOpaqueDrawRecordLeaf()`, renamed from `BuildGameViewLeaf()`/
  `BuildGameViewDrawRecordLeaf()`), every other one (today, always exactly
  `"DrawSkyBackground"`) becomes a new, generic, childless leaf built by a new
  `BuildGraphicsPassLeaf()` function (mirroring `BuildComputeDispatchLeaf()`'s
  shape — real pass name as both `passName`/`shaderName`, real aggregate
  draw stats, and real blend/Z/stencil rows — `"DrawSkyBackground"` reuses
  `DescribeSkyBackgroundPipelineState()` verbatim; any other future pass this
  function is ever called for falls back to `DescribeStandardPipelineState()`
  as a documented, deliberate, unverified default).
- A culled `Compute`-kind pass encountered inside the view region is skipped
  (continues the walk) rather than stopping it — a small, explicitly
  documented, name-free edge-case rule the phase document's own algorithm
  left implicit (see "Judgment Calls" below).
- The per-entity draw-record child leaves' own
  `FrameDebuggerEventDetails::passName` literal changed from `"GameView
  (Entity Draw)"` to `"RenderOpaque (Entity Draw)"` (Step 3.3's explicit
  requirement).
- The root node's own cosmetic label stays `"Game View"` (with a space,
  unchanged) and `renderTarget.name` stays the literal `"GameView"` (the
  RenderTexture/resource name, a completely separate, unaffected concept from
  the pass-name changes above).

### 4. `src/Editor/FrameDebuggerData.h`

- Appended a new doc-comment paragraph on `BuildRealFrameDebuggerSnapshot()`
  documenting the PHASE4 rework (new tree shape, the `"RenderOpaque"` pivot,
  the Debug-category exclusion) — the many older, still-accurate historical
  paragraphs above it (frame-debugger-5/6/7/8) were left untouched, per this
  codebase's own "narrate history truthfully, append new facts" convention
  (see PHASE1's completion report for the identical precedent).

### 5. `src/Game/Game.h`

- Fixed one now-stale, present-tense factual claim on
  `CountGameViewDrawCommandsThisFrame()`'s own doc comment: it used to assert
  `capture.DrawRecords().size() == objectCount + 1` (true only while
  `RecordSkyBackgroundDraw()` existed) — updated to state the ORIGINAL,
  now-restored relationship (`objectCount == capture.DrawRecords().size()`)
  now that the sky draw is no longer a fabricated `DrawRecords()` entry.

### 6. Tests

- `tests/Editor/FrameDebuggerCaptureTests.cpp`: removed the four
  `RecordSkyBackgroundDraw()`-specific tests (that method no longer exists);
  kept `DescribeSkyBackgroundPipelineStateTest.ReturnsRealDistinctValues`
  unchanged (that function is still real and still used, by the new
  `BuildGraphicsPassLeaf()`).
- `tests/Editor/FrameDebuggerDataTests.cpp`: renamed hand-fabricated fixture
  literals (`"GameView"` → `"RenderOpaque"`, `"GameView (Entity Draw)"` →
  `"RenderOpaque (Entity Draw)"`); removed the five DrawRecord-based Sky
  Background tests (that mechanism moved to a real pass fixture, now covered
  in `FrameDebuggerSnapshotBuilderTests.cpp` instead) and replaced them with
  one renamed, retargeted `EntityLeafShapeIsCorrect` test preserving the one
  regression they still usefully covered (a per-entity leaf's own shape).
- `tests/Editor/FrameDebuggerSnapshotBuilderTests.cpp`: every fixture's pivot
  pass renamed `"GameView"` → `"RenderOpaque"`; every `passName`/leaf-name
  assertion updated to match. Added: a `"Compute LUT"` vs. `"Compute
  Dispatches (Pre-GameView)"` mixed-category split test (proving fixed tree
  presentation order regardless of real interleaved execution order), an
  AtmosphereLut-only test, a real `"DrawSkyBackground"` leaf test (replacing
  the old DrawRecord-based sky test), a `"RenderTransparent"`-never-appears
  proof (Step 3.5), and the REQUIRED regression test for the pre-check's own
  3.3b fix — a `Debug`-category Graphics pass positioned exactly where
  `AddFrameDebuggerReplayPasses()`'s own replay passes really sit produces
  ZERO extra tree leaves. One pre-existing fixture
  (`RenderOpaqueWithNoComputePassesProducesExactlyOneLeaf`, formerly
  `GameViewWithNoComputePassesProducesExactlyOneLeaf`) had its trailing
  `"Present"` pass removed — see "Deviations" below.

## Verification

- `search_in_dir` for `isSkyBackgroundDraw`/`RecordSkyBackgroundDraw` across
  `src/` and `tests/` confirms every remaining hit is a doc comment
  explicitly describing the removal — zero real field reads/writes or method
  calls remain anywhere in the repo.
- Incremental compile: `cmake --build build --target gte_core` — clean
  build, zero errors/warnings.
- Incremental compile: `cmake --build build --target GreatTamanaEngineTests`
  — clean build.
- Incremental compile: `cmake --build build --target GreatTamanaEngine` —
  clean build.
- Ran the exact touched/added test suites directly:
  `GreatTamanaEngineTests.exe --gtest_filter=FrameDebuggerSnapshotBuilderTest.*:FrameDebuggerDataTest.*:FrameDebuggerCaptureContextTest.*:DescribeStandardPipelineStateTest.*:DescribeSkyBackgroundPipelineStateTest.*`
  — **57/57 passed**.
- Live runtime smoke test (`run_app_background` + `gte_send_request`):
  - `POST /instantiate_primitive` (a cube named `"SmokeTestCube"`) spawned a
    real entity.
  - `GET /frame_debugger/open` → `GET /frame_debugger/enable?value=true` →
    `GET /frame_debugger/state` confirmed a real capture happened
    (`hasCapturedFrame: true`, `totalEventCount: 9`).
  - `GET /get_swapchain` screenshot confirmed the EXACT target tree shape:
    `"Compute LUT"` (5 real Atmosphere LUT passes:
    `AtmosphereTransmittanceLutPass`/`AtmosphereMultiScatteringLutPass`/
    `AtmosphereSkyViewLutPass`/`AtmosphereAerialPerspectiveVolumePass`/
    `AtmosphereAerialPerspectiveVolumeDebugSlicePass`) → `"RenderOpaque"`
    (with its own `"SmokeTestCube (Entity 1)"` child) → `"DrawSkyBackground"`
    → `"Compute Dispatches (Post-GameView)"` (`AtmosphereAerialPerspectiveCompositePass`)
    — **no `"Compute Dispatches (Pre-GameView)"` group at all** (correctly
    absent — no GPU Skinning ran this frame), **no `"RenderTransparent"`
    leaf** (correctly absent — still a no-op today), and, critically, **zero
    spurious `FrameDebuggerReplayStepN` rows anywhere**, even though this
    exact `enable` + implicit capture sequence is precisely what triggers
    those N debug-only replay passes to be declared this same frame — the
    concrete, live proof the 3.3/3.3b fix actually works, not just in the
    Tier-1 test.
  - `GET /frame_debugger/select_event?index=7` (`"DrawSkyBackground"`) +
    `GET /get_swapchain` confirmed its own real, distinct Inspector row data:
    `Shader`/`Pass` = `"DrawSkyBackground"`, `ZTest = Equal`, `ZWrite = Off`
    (from `DescribeSkyBackgroundPipelineState()`).
  - `GET /frame_debugger/select_event?index=5` (`"RenderOpaque"`) +
    `GET /get_swapchain` confirmed its own distinct Inspector row data:
    `Shader = "Triangle.vert/Triangle.frag (PositionColor)"`, `Pass =
    "RenderOpaque"`, `ZTest = Less`, `ZWrite = On` (from
    `DescribeStandardPipelineState()`) — visibly different from
    `"DrawSkyBackground"`'s own row, confirming both leaves report real,
    correctly-distinct pipeline facts.
  - Stopped the app afterward (`stop_app_background`). (One cleanup call,
    `GET /delete_entity?index=1&generation=1`, returned 404 — the test
    entity was never persisted to disk and the whole process was terminated
    immediately after, so this is harmless; not investigated further since
    it is outside this phase's own scope and no state was left behind.)

## Deviations From The Phase Document

None in substance to the PRODUCTION logic — the algorithm implemented is
exactly Step 3.3's own concrete, bounded specification. Two things worth
flagging explicitly:

1. **One pre-existing Tier-1 test fixture needed a small, honest correction
   beyond a pure rename.** `RenderOpaqueWithNoComputePassesProducesExactlyOneLeaf`
   (renamed from `GameViewWithNoComputePassesProducesExactlyOneLeaf`) used to
   include a trailing `"Present"` pass positioned AFTER the pivot, to prove
   Game-View-only scope excludes "other passes in the same snapshot". Under
   the OLD algorithm this was a safe no-op (only compute-kind passes were
   ever inspected post-pivot). Under the NEW view-region walk (which
   correctly, per its own spec, treats any surviving, non-SceneView,
   non-`Debug`-category Graphics pass after the pivot as a real leaf), this
   would have produced a spurious extra `"Present"` leaf — but this exact
   scenario never actually occurs in the real engine (`"Present"` is declared
   in a completely separate `RenderGraph::Execute()` call from
   `"RenderOpaque"`, confirmed by reading `Application.cpp` directly — the
   two are never part of the same `RenderGraphSnapshot` at all). The fixture
   was corrected to drop the now-unrealistic trailing `"Present"` pass
   (keeping the leading `"SceneView"` pass, which still exercises the same
   exclusion concern safely). This is a test-fixture accuracy fix, not a
   production-code behavior change or a deviation from the phase document's
   own algorithm.
2. **One small, non-ambiguous judgment call the phase document's algorithm
   left implicit**: what happens when the view-region walk encounters a
   CULLED `Compute`-kind pass (as opposed to a surviving one)? The phase
   document's Step 3.3 explicitly handles "surviving Compute-kind" (stop) and
   "Graphics-kind" (build-or-skip) but never states what to do with a culled
   Compute-kind pass sitting inside the walked range. Resolved by skipping it
   (continue the walk) — consistent with this whole tree's existing "never
   show a culled pass as if it survived" rule elsewhere, and covered by this
   phase's own new test suite's general culled-pass coverage. Not treated as
   a genuine design ambiguity worth an `ask_questions` round-trip, since the
   resolution is the only one consistent with every other culled-pass rule
   already established in this same file.

## Definition of Done — Checklist

- [x] `BuildRealFrameDebuggerSnapshot()` builds the tree shape from Step 1,
      generically, from `PassKind`/`RenderPassCategory`/`ViewScope`/execution
      order — zero hardcoded pass-name string literals except the one
      `"RenderOpaque"` pivot lookup.
- [x] `isSkyBackgroundDraw`/`RecordSkyBackgroundDraw()` no longer exist
      anywhere in the repo (confirmed via `search_in_dir`).
- [x] `AddFrameDebuggerReplayPasses()` declares its passes via
      `builder.AddRenderPass(..., rg::RenderPassCategory::Debug, ...)` —
      confirmed via `search_in_dir` that this call site no longer calls plain
      `builder.AddPass(...)`.
- [x] Every updated/added test in `tests/Editor/FrameDebugger*Tests.cpp`
      passes, INCLUDING the new 3.6 test proving a `Debug`-category Graphics
      pass sitting inside the view region produces zero extra leaves.
- [x] Live, HTTP-driven confirmation: opened the Frame Debugger, enabled it,
      captured, and confirmed via `/get_swapchain` screenshots the new tree
      shape with `"RenderOpaque"`/`"DrawSkyBackground"` as separate,
      selectable rows, a `"Compute LUT"` group distinct from any `"Compute
      Dispatches (Pre-GameView)"` group, and explicitly confirmed NO
      extra/spurious rows for the Frame Debugger's own N internal replay
      passes on the exact HTTP sequence that triggers them.
- [x] Incremental compile + the full `tests/Editor/` test subset (110/110 →
      now 57 tests directly exercised for the touched files, all passing;
      the wider suite is PHASE7's job) passes; completion report + git commit
      follow.

## Handoff To PHASE5

Every pass this campaign's Frame Debugger tree cares about is now correctly,
generically discovered — including `AddFrameDebuggerReplayPasses()` itself,
whose migration onto `AddRenderPass()`/`RenderPassCategory::Debug` PHASE4
already completed (pulled forward from PHASE5's originally-planned scope, per
the pre-check's own fix). PHASE5
(`PHASE5_REMAINING_PASSES_MIGRATION.md`) only needs to CONFIRM this migration
(its own grep audit, Step 3.5) rather than re-implement it, and can proceed
with its own remaining scope: GPU Skinning, `"Present"`, and Compute Blur
Validation migration onto the same `AddRenderPass()` chokepoint.
