# PHASE4_FRAME_DEBUGGER_EVENT_PIVOT_FIX — Completion Report

_Child of `PHASE0_MASTER_STRATEGY.md`, `render-pass-3` campaign. Branch:
`feature/render-pass-impl` (unchanged, never switched)._

## Summary

Implemented PHASE4 exactly as scoped, PLUS one additional, user-approved
root-cause fix discovered by this phase's own required live verification
step (see "A real bug this phase's own live verification caught" below).

The literal `FindPassByName(graphSnapshot.passesInExecutionOrder,
"RenderOpaque")` pivot search in `BuildRealFrameDebuggerSnapshot()`
(`src/Editor/FrameDebuggerData.cpp`) is replaced by a new, structural,
name-free helper, `FindViewRegionPivot()`, added next to the pre-existing
`FindPassByName()` (which is untouched and stays in the file for its own,
separate, out-of-scope call site). `FindViewRegionPivot()` returns the
first pass, in true execution order, whose `rg::RenderPassEvent >=
RenderPassEvent::Opaques` — exactly the design doc's own Section 6
suggestion — with deliberately NO extra filtering by
`isCulled`/`kind`/`viewScope`/`category`, mirroring the OLD
`FindPassByName()` call's own behavior exactly.
`FindPostGameViewCompositePassExecutionIndex()`'s own, separate,
`"GameViewComposited"`-string-based logic is completely untouched (confirmed
byte-for-byte identical via direct read). `ComputeBlurValidation.cpp` and
`GpuSkinningValidation.h` were not touched at all (confirmed via
`git status` — neither file appears in the diff).

## A real bug this phase's own live verification caught (read this first)

Following the Definition of Done's own required live check (`GET
/frame_debugger/capture` + `/get_swapchain`), the very first live test
after implementing the literal, as-written fix revealed the resulting
Frame Debugger tree was **NOT** identical to before this phase — it was
badly broken: no `"RenderOpaque"` leaf, no `"DrawSkyBackground"` leaf, no
`"Compute LUT"` pre-view group at all; every real pass got shoved into a
single `"Compute Dispatches (Post-GameView)"` group instead.

**Root cause**: `PHASE0_MASTER_STRATEGY.md`'s Locked Design Decision 2 and
PHASE3's own completion report both assert every production pass already
carries a "REAL, correct" `RenderPassEvent` value (e.g. "AtmosphereViewLut
= PreOpaques"). This turned out to be **incorrect** for six specific real
pass declarations: `AtmosphereSharedLut`/`AtmosphereViewLut`/
`AtmosphereComposite` are all *immediate-declare* `RenderPipeline`
providers (per PHASE3's own Step 3.3b exception) — they call
`AddAtmosphereSharedLutPasses()`/`AddAtmosphereViewLutPasses()`/
`AddAtmosphereCompositePass()` (`AtmospherePassSequence.cpp`), which
themselves call straight into `AtmosphereLutRenderer.cpp`'s six
`builder.AddRenderPass(...)` call sites — none of which had ever been
updated to pass an explicit `renderPassEvent` argument to
`RenderGraphBuilder::AddRenderPass()`'s new trailing, defaulted parameter.
Every one of these six real passes (`AtmosphereTransmittanceLutPass`,
`AtmosphereMultiScatteringLutPass`, `AtmosphereSkyViewLutPass`,
`AtmosphereAerialPerspectiveVolumePass`,
`AtmosphereAerialPerspectiveVolumeDebugSlicePass`,
`AtmosphereAerialPerspectiveCompositePass`) therefore silently defaulted to
`RenderPassEvent::Opaques` — **the exact same value** `"RenderOpaque"`
itself carries. Since five of these six passes run strictly *before*
`"RenderOpaque"` in real execution order, the new name-free pivot search
(correctly, per its own literal spec) picked the *first* one of them as
the pivot instead — a genuinely different, worse bug than the one PHASE4
was created to fix, but one this phase's own live-testing requirement was
specifically designed to catch.

**Resolution (user-approved via `ask_questions`, option 1 of 3 offered)**:
fixed the actual root cause rather than working around it — added an
explicit trailing `RenderPassEvent` argument to all six
`builder.AddRenderPass(...)` call sites in
`src/Renderer/Atmosphere/AtmosphereLutRenderer.cpp`, matching
`PHASE0_MASTER_STRATEGY.md`'s own already-documented intended mapping:
`RenderPassEvent::PreOpaques` for the five shared/per-view LUT-and-volume
passes (`AddTransmittanceLutPass`/`AddMultiScatteringLutPass`/
`AddSkyViewLutPass`/`AddAerialPerspectiveVolumePass`/
`AddAerialPerspectiveVolumeDebugSlicePass`), and
`RenderPassEvent::AfterTransparents` for
`AddAerialPerspectiveCompositePass` ("the Aerial Perspective Composite
pass is `AfterTransparents`"). `drawKind` stays at its own default
(`DrawMesh`) at every one of these six call sites — irrelevant for a
Compute-kind pass (`GraphicsChildEventLabelFor()` only ever reads it for a
Graphics-kind pass). This is a small, additive, backward-compatible change
— `RenderPassEvent` is a purely descriptive sort hint (per its own header
comment; real ordering is still 100% enforced by
`RenderGraphCompiler`'s RAW/WAW dependency analysis, which never reads
this field at all), so this fix cannot change any actual rendering
behavior, resource lifetime, or barrier — confirmed by the full 323-test
targeted run (zero regressions) and the live re-verification below.

## Files changed

- **`src/Editor/FrameDebuggerData.cpp`** — added `FindViewRegionPivot()` in
  the file's own anonymous namespace, immediately after the pre-existing,
  untouched `FindPassByName()`. Swapped the one call site inside
  `BuildRealFrameDebuggerSnapshot()` from
  `FindPassByName(graphSnapshot.passesInExecutionOrder, "RenderOpaque")` to
  `FindViewRegionPivot(graphSnapshot.passesInExecutionOrder)`; updated the
  doc comment immediately above that call site. Nothing else in this file
  changed — every other `ViewScope`/`RenderPassCategory`/`RenderPassDrawKind`
  consumer (the `viewScope == SceneView` checks, the `category ==
  AtmosphereLut`/`category == Debug` checks, `GraphicsChildEventLabelFor()`)
  is untouched, and `FindPostGameViewCompositePassExecutionIndex()` is
  byte-for-byte identical to before this phase (confirmed by direct read).
- **`src/Renderer/Atmosphere/AtmosphereLutRenderer.cpp`** (root-cause fix,
  outside this phase's originally-scoped file, user-approved) — all six
  `builder.AddRenderPass(...)` call sites (`AddTransmittanceLutPass`,
  `AddMultiScatteringLutPass`, `AddSkyViewLutPass`,
  `AddAerialPerspectiveVolumePass`, `AddAerialPerspectiveCompositePass`,
  `AddAerialPerspectiveVolumeDebugSlicePass`) gained explicit trailing
  `rg::RenderPassDrawKind::DrawMesh, rg::RenderPassEvent::<value>` arguments
  (see mapping above), each with an inline comment explaining why.
- **`tests/Editor/FrameDebuggerSnapshotBuilderTests.cpp`** — per Step 3.3:
  - `MakeComputePass()` now stamps `pass.renderPassEvent =
    rg::RenderPassEvent::PreOpaques` by default (documented inline) — this
    mirrors how every real pre-view compute pass in this engine is
    actually tagged in production (post-fix), and is what keeps every
    pre-existing fixture's own `"RenderOpaque"` pass (built via the plain,
    unmodified `MakePass()` — default `renderPassEvent` is `Opaques`)
    correctly discovered as the pivot by the new
    `FindViewRegionPivot()` lookup, exactly like the OLD
    `FindPassByName()` search always found it. Needed because
    `RenderGraphPassSnapshot::renderPassEvent` defaults to `Opaques` for
    every hand-built fixture pass regardless of `MakePass()` vs.
    `MakeComputePass()`, and the new lookup is genuinely *value*-driven,
    not name-driven — a synthetic pre-view compute pass sharing
    `"RenderOpaque"`'s own default value would otherwise be found first
    purely because it sits earlier in the fixture's own
    `passesInExecutionOrder` vector.
  - Three individual fixtures that push a plain, non-compute pass (built
    via bare `MakePass()`, never `MakeComputePass()`) *before* the real
    `"RenderOpaque"` pass — `NoRenderOpaquePassProducesEmptyResult`
    (`"SceneView"`/`"Present"`),
    `RenderOpaqueWithNoComputePassesProducesExactlyOneLeaf` (`"SceneView"`),
    and `NonComputePassIsNeverTreatedAsComputeDispatch`
    (`"SomeOrdinaryGraphicsPass"`) — now explicitly stamp that one pass's
    own `renderPassEvent` to `RenderPassEvent::BeforeEverything`, for the
    exact same reason. **No `EXPECT`/`ASSERT` line in any of these three
    tests was touched** — only the fixture *construction* changed, per
    this phase's own Definition of Done ("if any [test-expectation edits]
    DO need edits, that is a signal... stop and re-examine" — these are
    fixture-setup additions, not expectation edits, and every pre-existing
    assertion in the file still passes unchanged).
  - Added one new regression test, per Step 3.3's explicit requirement:
    `ViewRegionPivotIsFoundStructurallyEvenWhenNotNamedRenderOpaque` —
    constructs a synthetic snapshot where a pass literally named
    `"RenderOpaque"` is tagged `RenderPassEvent::BeforeEverything` (a red
    herring, deliberately NOT eligible as the pivot), and a *differently*-
    named pass (`"MainOpaqueDrawPass"`) is tagged `RenderPassEvent::Opaques`
    and carries its own real draw stats + a per-entity draw record; asserts
    `BuildRealFrameDebuggerSnapshot()` correctly finds the differently-named
    pass as the pivot and uses *its* real data (draw stats, per-entity
    children) — proving the lookup is genuinely name-free in both
    directions (never matches on the literal string `"RenderOpaque"`, and
    never rejects a differently-named eligible pass).

## Verification performed

- **Incremental compile**: `gte_core` (`FrameDebuggerData.cpp`,
  `AtmosphereLutRenderer.cpp`) and `GreatTamanaEngineTests`
  (`FrameDebuggerSnapshotBuilderTests.cpp`) both built cleanly with zero
  warnings, multiple times across this phase's own iterative fix.
- **Full targeted test run**:
  `GreatTamanaEngineTests.exe
  --gtest_filter=FrameDebugger*:RenderGraph*:RenderPass*:Atmosphere*` —
  **323/323 passing**, zero failures, zero new skips (288 pre-existing +
  35 `FrameDebuggerSnapshotBuilderTest` cases, one of which is the new
  regression test above; every OTHER pre-existing test in this filter,
  including all 34 pre-existing `FrameDebuggerSnapshotBuilderTest` cases
  and every `AtmosphereMathTest`/`AtmosphereParametersTest` case, passed
  completely unchanged).
- **`git status`/direct-read verification**: confirmed exactly three files
  changed (`src/Editor/FrameDebuggerData.cpp`,
  `src/Renderer/Atmosphere/AtmosphereLutRenderer.cpp`,
  `tests/Editor/FrameDebuggerSnapshotBuilderTests.cpp`) — `ComputeBlurValidation.cpp`
  and `GpuSkinningValidation.h` do not appear in the diff at all;
  `FindPostGameViewCompositePassExecutionIndex()` is confirmed
  byte-for-byte identical to before this phase via direct read.
- **Live, HTTP-driven smoke test** (`run_app_background` + `gte_send_request`,
  `TestScene.gtscene` loaded via `POST /load_scene`):
  - **First attempt (literal fix only, before the root-cause fix)**:
    confirmed BROKEN — `GET /frame_debugger/capture` →
    `GET /get_swapchain` showed `"Game View"` with only a single
    `"Compute Dispatches (Post-GameView)"` child group (5 Atmosphere
    passes, including the SHARED `"AtmosphereTransmittanceLutPass"`
    incorrectly landing there too), with `"RenderOpaque"`,
    `"DrawSkyBackground"`, and the `"Compute LUT"` group entirely absent —
    `totalEventCount: 10`.
  - **After the root-cause fix**: `GET /frame_debugger/capture` →
    `GET /get_swapchain` now shows the FULL, correct tree —
    `totalEventCount: 17` — `"Compute LUT"` (▸ `AtmosphereTransmittanceLutPass`/
    `AtmosphereMultiScatteringLutPass`/`AtmosphereSkyViewLutPass`/
    `AtmosphereAerialPerspectiveVolumePass`/
    `AtmosphereAerialPerspectiveVolumeDebugSlicePass`, each ▸ `"Compute
    Dispatch"`) ▸ `"RenderOpaque"` (▸ `"SmokeTestCube (Entity 0)"`/`"Entity 2
    (Entity 2)"`) ▸ `"DrawSkyBackground"` (▸ `"Draw Quad"`) ▸ `"Compute
    Dispatches (Post-GameView)"` (▸ `"AtmosphereAerialPerspectiveCompositePass"`
    ▸ `"Compute Dispatch"`) — matching every prior campaign's own
    documented tree shape for this exact test scene.
  - `GET /frame_debugger/select_event?index=10` (the `"RenderOpaque"` row)
    → `GET /get_swapchain` confirmed the Inspector pane shows `"Event #10:
    Draw Mesh"`, `Pass: RenderOpaque`, `Blend: Opaque (no blend)`, `ZTest:
    Less`, `ZWrite: On`, `Cull: None` — real, correct pipeline-state data,
    identical in shape to every prior phase's own completion report.
  - App run across two separate sessions (one per fix iteration), each
    cleanly stopped via `stop_app_background` with zero crashes/assertion
    failures.
- No full build, no full `ctest` run was performed — correctly out of
  scope for this phase per `PHASE0_MASTER_STRATEGY.md`'s cross-cutting
  rules ("No full build/regression test until PHASE5").

## Deviation from the phase doc (user-approved, not a silent scope change)

**The phase doc's own "What We Will NOT Do" section does not explicitly
authorize touching `AtmosphereLutRenderer.cpp`.** This phase's own literal
plan (Step 3.1–3.3) was implemented FIRST, exactly as written, with zero
deviation. Only after that literal implementation's own REQUIRED live
verification step (Definition of Done: "shows the exact same Game View
event tree shape as before this phase") caught a real, confirmed
regression did this phase's implementer stop and use `ask_questions` to
ask the user how to proceed, rather than silently expanding scope or
silently accepting a broken result. The user explicitly chose (option 1
of 3 offered) to fix the real root cause. This is documented here in full,
per this campaign's own "always document deviations plainly" convention —
future phases/campaigns should treat
`AtmosphereLutRenderer.cpp`'s six `builder.AddRenderPass(...)` call sites
as now carrying CORRECT, load-bearing `RenderPassEvent` values (not just
a cosmetic default), and should double-check any FUTURE new immediate-declare
`RenderPipeline` provider (per PHASE3's own Step 3.3b pattern) explicitly
sets its own real passes' `renderPassEvent` too, rather than assuming the
trailing parameter's own built-in default is ever safe to leave unset once
more than one such provider/pass exists in the same graph.

## Notes for PHASE5's implementer

- **The literal PHASE4 scope (the `FindViewRegionPivot()` rewrite in
  `FrameDebuggerData.cpp`) is complete and independently correct** — the
  root-cause fix above was a NECESSARY, separate, additive correction to
  make its own precondition (Locked Design Decision 2's claim that every
  production pass already carries a correct `RenderPassEvent`) actually
  true, not a sign the PHASE4 rewrite itself was wrong.
- **Every real pass this campaign covers now carries a genuinely correct,
  load-bearing `RenderPassEvent` value** — re-confirm this stays true if
  PHASE5's own full regression pass touches Atmosphere/Render Graph code
  again for any reason.
- **The literal `"RenderOpaque"` string no longer appears anywhere in a
  pass-lookup/search context in `FrameDebuggerData.cpp`** — confirmed via
  `search_in_dir`; every remaining occurrence is either a plain comment or
  a hardcoded DISPLAY label (`leaf.name = "RenderOpaque"`/`details.passName
  = "RenderOpaque"` inside `BuildRenderOpaqueLeaf()`, which stamps that
  literal label onto WHATEVER pass object the pivot search found — this is
  pre-existing, unchanged, and intentional; see that function's own doc
  comment).
- **A live, HTTP-driven check confirms the Game View event tree shape is
  now correct** for `TestScene.gtscene` (a scene with two mesh entities,
  `SmokeTestCube`/`Entity 2`) — this was NOT reachable in PHASE2's own
  live-testing session (that scene had zero mesh entities at the time),
  so this is genuinely new, valuable verification this campaign had not
  performed before.

## Definition of Done — checklist

- [x] `FindViewRegionPivot()` exists and is the ONLY thing that decides
      "where does the Game View region start" in `FrameDebuggerData.cpp` —
      the literal string `"RenderOpaque"` no longer appears anywhere in a
      pass-lookup/search context in this file.
- [x] Every pre-existing test in
      `tests/Editor/FrameDebuggerSnapshotBuilderTests.cpp` still passes,
      with zero test-EXPECTATION edits needed (fixture-construction-only
      additions were needed and are fully documented above).
- [x] One new regression test
      (`ViewRegionPivotIsFoundStructurallyEvenWhenNotNamedRenderOpaque`)
      exists and passes, proving the lookup is genuinely name-free.
- [x] `FindPostGameViewCompositePassExecutionIndex()`'s own, separate,
      `"GameViewComposited"`-string-based logic is untouched — confirmed
      via direct read showing zero changes to that function.
- [x] An incremental compile of `gte_core` and `GreatTamanaEngineTests`
      succeeds, and the updated/added Frame Debugger tests pass when run
      directly.
- [x] A live, HTTP-driven check (`GET /frame_debugger/capture` +
      `/get_swapchain`) shows the correct Game View event tree shape,
      matching every prior campaign's own documented shape for this test
      scene, INCLUDING real mesh entities this time (a strictly stronger
      verification than PHASE2's own, which had no mesh entities
      available) — achieved only after the root-cause fix; the literal,
      as-written fix alone was confirmed BROKEN by this same check first,
      then fixed, per this phase's own "stop and re-examine" DoD clause.

## What This Phase Did NOT Do (confirmed, matching its own "What We Will NOT Do")

- Did NOT touch `ComputeBlurValidation.cpp` or `GpuSkinningValidation.h` —
  confirmed via `git status` (neither file appears in the diff at all).
- Did NOT touch any OTHER `ViewScope`/`RenderPassCategory`/`RenderPassDrawKind`
  consumer inside `FrameDebuggerData.cpp` itself.
- Did NOT touch `FindPostGameViewCompositePassExecutionIndex()`'s own
  `"GameViewComposited"` string match — confirmed byte-for-byte identical.
- Did NOT run a full build or full `ctest` regression suite — incremental
  compile + the specific, already-existing (plus one new) Frame Debugger
  test, run directly, plus a live HTTP smoke test, per this phase's own
  scope. PHASE5 is the only phase that runs the full suite.
