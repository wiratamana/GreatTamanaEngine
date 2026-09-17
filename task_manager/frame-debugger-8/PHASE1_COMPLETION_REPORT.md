# PHASE1 — Completion Report: Sky Draw Capture Instrumentation

_Reports to `PHASE0_MASTER_STRATEGY.md`. Implements
`PHASE1_SKY_DRAW_CAPTURE_INSTRUMENTATION.md` (v2, including its addenda) in
full._

## Summary

Gave the engine a real, first-class way to record "the Sky Background draw
just happened" for the Frame Debugger's capture context, and wired the one
real production call site that invokes it — `AddGameViewPass()`'s own
`execute` lambda, immediately after `recordSkyBackground(ctx.cmd)` runs, on an
armed capture frame. No rendering behavior changed at all — this phase is
purely additive OBSERVABILITY instrumentation, exactly as scoped.

## What was done (maps 1:1 to the phase document's Step 3)

- **3.1 — `src/Renderer/Atmosphere/AtmosphereSkyBackgroundRenderer.h`**: added
  the new `public static constexpr const char* ShaderDebugName() noexcept`
  method, returning the real, hand-verified
  `"AtmosphereSkyBackground.vert/AtmosphereSkyBackground.frag"` string.
  Nothing else in this file was touched.
- **3.2/3.3 — `src/Editor/FrameDebuggerCapture.h`/`.cpp`**:
  - `FrameDebuggerDrawRecord` gained a new `bool isSkyBackgroundDraw = false;`
    field, appended at the very end of the struct (Locked Design Decision 5).
  - New free function `FrameDebuggerStandardPipelineState
    DescribeSkyBackgroundPipelineState()` — returns the sky's real, distinct
    `zTest = "Equal"` / `zWrite = "Off"` values, matching
    `DescribeStandardPipelineState()` everywhere else (blend/zClip/cull/every
    stencil field).
  - New `FrameDebuggerCaptureContext::RecordSkyBackgroundDraw(const
    std::string& pipelineDebugName)` — internally calls the existing
    `RecordDraw()` (reusing its dedup/draw-call-count/last-view-projection
    bookkeeping, Locked Design Decision 6), then appends one
    `FrameDebuggerDrawRecord` with `isSkyBackgroundDraw = true` and
    `triangleCount = 1` (one real full-screen-triangle `vkCmdDraw`).
- **3.4 — `src/Application/RenderPasses.cpp`**: added an unconditional
  `#include "../Renderer/Atmosphere/AtmosphereSkyBackgroundRenderer.h"`
  (Atmosphere is always compiled, per `AGENTS.md`), then, inside
  `AddGameViewPass()`'s own `execute` lambda, added a new
  `#if GTE_ENABLE_EDITOR` block strictly AFTER
  `recordSkyBackground(ctx.cmd);` that calls
  `frameDebuggerCapture->RecordSkyBackgroundDraw(AtmosphereSkyBackgroundRenderer::ShaderDebugName())`
  when `frameDebuggerCapture != nullptr`. `AddSceneViewPass()`,
  `AddPresentPass()`, and `AddFrameDebuggerReplayPasses()` were NOT touched
  (PHASE2/PHASE3's own jobs). As a small extra correctness touch (not
  strictly required by the phase doc, but needed to keep the file internally
  honest), the pre-existing comment immediately above `game.Render(...)` in
  that same lambda — which used to say "frameDebuggerCapture is never
  dereferenced here" — was updated, since this phase now DOES dereference it
  a few lines later in the same lambda. This is a pure comment fix; no
  behavior changed.
- **3.5 — `tests/Editor/FrameDebuggerCaptureTests.cpp`**: added all 5
  documented new tests: `DescribeSkyBackgroundPipelineStateReturnsRealDistinctValues`
  → renamed to `DescribeSkyBackgroundPipelineStateTest.ReturnsRealDistinctValues`
  to match this file's existing `TEST(Fixture, Case)` grouping convention (same
  content/assertions as specified), plus
  `RecordSkyBackgroundDrawAppendsOneMarkedRecord`,
  `RecordSkyBackgroundDrawAfterEntityDrawsAppendsAtTheEnd`,
  `RecordSkyBackgroundDrawFeedsPipelineDebugNamesAndDrawCallCount`, and
  `ResetClearsSkyBackgroundRecordsToo`.
- **3.6 — `src/Game/Game.h`**: spliced the new v2-addendum doc comment in
  immediately before `CountGameViewDrawCommandsThisFrame()`'s pre-existing
  "IMPORTANT documented assumption..." paragraph, exactly as specified,
  explaining that `capture.DrawRecords().size()` is no longer always equal to
  `objectCount` once a sky record exists. The pre-existing paragraph itself
  was left unchanged (still correct for the entity-only portion of the
  count), per the phase document's explicit instruction.

## Deviations from the plan

None of substance. The only addition beyond the literal text of the phase
document was the small "not-dereferenced-here" comment touch-up in
`RenderPasses.cpp` described above — done because leaving that comment as-is
would have been actively misleading (it explicitly claimed something this
phase's own change makes false, one function scope away). This is a
documentation-only change with zero behavioral impact and does not affect any
Definition-of-Done item.

## Compile check results (this phase's own scope)

- `cmake --build build --target gte_core` — **succeeded** (55/55 steps,
  clean).
- `cmake --build build --target GreatTamanaEngineTests` — **succeeded**
  (25/25 steps, clean).
- Ran the full `GreatTamanaEngineTests.exe` suite (not just the new/changed
  file, per this codebase's own "run the actual test suite" rule): **1553
  tests ran, 1552 passed, 1 skipped** (`PmxLoaderRealModelSmokeTest` —
  skipped because the MMD test model directory isn't present on this
  machine, a pre-existing environment-dependent skip unrelated to this
  change). All 5 new tests pass; zero pre-existing tests regressed.
- **v2 addendum**: `cmake --build build-editor-off --target gte_core` —
  **succeeded** (33/33 steps, clean) — confirms the new
  `#if GTE_ENABLE_EDITOR`-guarded dereference in `RenderPasses.cpp` compiles
  and links correctly in the `GTE_ENABLE_EDITOR=OFF` configuration too (no
  unguarded reference, no missing `#endif`).

## Definition of Done (this phase) — verified

- [x] `AtmosphereSkyBackgroundRenderer::ShaderDebugName()` exists, returns the
      exact real shader-pair string.
- [x] `FrameDebuggerDrawRecord::isSkyBackgroundDraw` exists, defaults to
      `false`, appended at the end of the struct.
- [x] `DescribeSkyBackgroundPipelineState()` exists, returns the real,
      distinct Equal/Off values.
- [x] `FrameDebuggerCaptureContext::RecordSkyBackgroundDraw()` exists, reuses
      `RecordDraw()` internally, appends exactly one correctly-marked record.
- [x] `AddGameViewPass()` calls it exactly once, in the right place, guarded
      by `#if GTE_ENABLE_EDITOR` and a null check — and nowhere else
      (`AddSceneViewPass()`/`AddPresentPass()`/`AddFrameDebuggerReplayPasses()`
      all untouched).
- [x] All 5 new Tier-1 tests pass; no pre-existing test regresses.
- [x] `Game.h`'s stale doc-comment invariant is corrected.
- [x] `cmake --build build-editor-off --target gte_core` succeeds.
- [x] Nothing under `src/Editor/FrameDebuggerData.h/.cpp` or
      `AddFrameDebuggerReplayPasses()` was touched.

## Notes for PHASE2 (next phase)

- The cross-phase invariant from `PHASE0_MASTER_STRATEGY.md` still holds
  exactly as documented: `FrameDebuggerCaptureContext::DrawRecords()` now
  contains, in order, one record per real entity draw (unchanged,
  `RecordEntityDraw()`) followed by exactly one sky record
  (`RecordSkyBackgroundDraw()`) **whenever a sky callback exists this
  frame** — confirmed by the new
  `RecordSkyBackgroundDrawAfterEntityDrawsAppendsAtTheEnd` test.
  `AddFrameDebuggerReplayPasses()` (PHASE2's own target) must build its own
  N+1 (entities + one dedicated sky step) replay destinations in this exact
  same order for the whole feature to line up with zero index-plumbing
  changes in PHASE3's snapshot builder — re-read
  `PHASE0_MASTER_STRATEGY.md`'s "Cross-phase invariant" section before
  starting PHASE2.
- PHASE2 also touches `RenderPasses.cpp` — per the v2 addendum, PHASE2's own
  "Compile check" section already calls for a second
  `cmake --build build-editor-off --target gte_core` pass as cheap extra
  insurance; nothing new was discovered here that changes that plan.
- No blockers, no discovered deviations requiring a design re-think. This
  phase's scope was small, self-contained, and completed exactly as
  specified.
